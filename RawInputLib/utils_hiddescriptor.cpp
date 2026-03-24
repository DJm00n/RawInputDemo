#include "pch.h"
#include "utils_hiddescriptor.h"

#include <windows.h>
#include <hidsdi.h>   // HIDP_PREPARSED_DATA, HIDP_CAPS, etc.
#include <hidpi.h>

#include <vector>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <algorithm>

 /*
 * Reconstructs a HID Report Descriptor from Windows HIDP_PREPARSED_DATA.
 *
 * The result is FUNCTIONALLY EQUIVALENT to the original — it will parse back
 * into the same preparsed data — but is NOT byte-identical to the original
 * because the following information is lost during parsing (see descript.c):
 *
 *   - PUSH/POP items (global state stack is flattened per-channel)
 *   - Original byte-width of encoded values (e.g. 1- vs 2-byte LogicalMin)
 *   - Order of Usage items within a single Main item (stored reversed)
 *   - CONST padding fields with no usage (silently dropped by parser)
 *   - Long Items (parser returns STATUS_COULD_NOT_INTERPRET for them)
 *   - Unknown LOCAL items (parser also rejects them)
 *   - Original sibling-collection ordering within a parent collection
 */


 // ---------------------------------------------------------------------------
 // Internal layout of HIDP_PREPARSED_DATA (from hidparse.h in WDK sources)
 // ---------------------------------------------------------------------------

#define HIDP_PREPARSED_DATA_SIGNATURE1  'PdiH'
#define HIDP_PREPARSED_DATA_SIGNATURE2  'RDK '
#define HIDP_MAX_UNKNOWN_ITEMS          4

#pragma pack(push, 1)

//struct HIDP_UNKNOWN_TOKEN {
//    UCHAR  Token;       // original item tag byte (tag | type | size)
//    UCHAR  Reserved[3];
//    ULONG  BitField;    // up to 4 bytes of item data, LE
//};

struct HIDP_CHANNEL_DESC {
    USHORT  UsagePage;
    UCHAR   ReportID;
    UCHAR   BitOffset;      // 0-7: bit offset within ByteOffset
    USHORT  ReportSize;     // bits per single field
    USHORT  ReportCount;    // number of fields in this channel
    USHORT  ByteOffset;     // byte offset of first field in report packet
    // (includes the ReportID byte when ReportID != 0)
    USHORT  BitLength;      // ReportSize * ReportCount
    ULONG   BitField;       // Main item data byte (flags: Data/Const, Array/Var, ...)
    USHORT  ByteEnd;
    USHORT  LinkCollection;
    USHORT  LinkUsagePage;
    USHORT  LinkUsage;

    ULONG   MoreChannels : 1; // array: more channels describe same field
    ULONG   IsConst : 1;
    ULONG   IsButton : 1;
    ULONG   IsAbsolute : 1;
    ULONG   IsRange : 1;
    ULONG   IsAlias : 1; // this channel is an alias of the next one
    ULONG   IsStringRange : 1;
    ULONG   IsDesignatorRange : 1;
    ULONG   Reserved : 20;
    ULONG   NumGlobalUnknowns : 4;

    HIDP_UNKNOWN_TOKEN GlobalUnknowns[HIDP_MAX_UNKNOWN_ITEMS];

    union {
        struct {
            USHORT UsageMin, UsageMax;
            USHORT StringMin, StringMax;
            USHORT DesignatorMin, DesignatorMax;
            USHORT DataIndexMin, DataIndexMax;
        } Range;
        struct {
            USHORT Usage, Reserved1;
            USHORT StringIndex, Reserved2;
            USHORT DesignatorIndex, Reserved3;
            USHORT DataIndex, Reserved4;
        } NotRange;
    };

    union {
        struct { LONG LogicalMin, LogicalMax; } button;
        struct {
            BOOLEAN HasNull;
            UCHAR   Reserved[3];
            LONG    LogicalMin, LogicalMax;
            LONG    PhysicalMin, PhysicalMax;
        } Data;
    };

    ULONG   Units;
    ULONG   UnitExp;
};

struct CHANNEL_REPORT_HEADER {
    USHORT Offset;   // index of first HIDP_CHANNEL_DESC in Data[]
    USHORT Size;     // count of HIDP_CHANNEL_DESC entries
    USHORT Index;
    USHORT ByteLen;  // report byte length including ReportID byte
};

struct HIDP_SYS_POWER_INFO { ULONG PowerButtonMask; };

struct HIDP_PREPARSED_DATA_HDR {
    LONG   Signature1, Signature2;
    USHORT Usage;
    USHORT UsagePage;
    HIDP_SYS_POWER_INFO PowerInfo;
    CHANNEL_REPORT_HEADER Input;
    CHANNEL_REPORT_HEADER Output;
    CHANNEL_REPORT_HEADER Feature;
    USHORT LinkCollectionArrayOffset; // byte offset from start of Data[] union
    USHORT LinkCollectionArrayLength;
    // Data[] (HIDP_CHANNEL_DESC array) follows immediately after this header
    // LinkCollection array follows at LinkCollectionArrayOffset bytes into Data[]
};

struct HIDP_PRIVATE_LINK_COLLECTION_NODE {
    USHORT  LinkUsage;
    USHORT  LinkUsagePage;
    USHORT  Parent;
    USHORT  NumberOfChildren;
    USHORT  NextSibling;
    USHORT  FirstChild;
    ULONG   CollectionType : 8;
    ULONG   IsAlias : 1;
    ULONG   Reserved : 23;
};

#pragma pack(pop)

// ---------------------------------------------------------------------------
// HID short-item tag byte constants (HID 1.11 spec, table 6.2.2)
// Lower 2 bits are size: 0=0B 1=1B 2=2B 3=4B — filled at emit time.
// ---------------------------------------------------------------------------

static constexpr UCHAR HID_USAGE_PAGE = 0x04; // global
static constexpr UCHAR HID_LOG_MIN = 0x14; // global
static constexpr UCHAR HID_LOG_MAX = 0x24; // global
static constexpr UCHAR HID_PHY_MIN = 0x34; // global
static constexpr UCHAR HID_PHY_MAX = 0x44; // global
static constexpr UCHAR HID_UNIT_EXP = 0x54; // global
static constexpr UCHAR HID_UNIT = 0x64; // global
static constexpr UCHAR HID_REPORT_SIZE = 0x74; // global
static constexpr UCHAR HID_REPORT_ID = 0x84; // global
static constexpr UCHAR HID_REPORT_COUNT = 0x94; // global

static constexpr UCHAR HID_USAGE = 0x08; // local
static constexpr UCHAR HID_USAGE_MIN = 0x18; // local
static constexpr UCHAR HID_USAGE_MAX = 0x28; // local
static constexpr UCHAR HID_DESIGNATOR_MIN = 0x38; // local
static constexpr UCHAR HID_DESIGNATOR_MAX = 0x48; // local
static constexpr UCHAR HID_STRING_MIN = 0x78; // local
static constexpr UCHAR HID_STRING_MAX = 0x88; // local
static constexpr UCHAR HID_DELIMITER = 0xA8; // local

static constexpr UCHAR HID_INPUT = 0x80; // main
static constexpr UCHAR HID_OUTPUT = 0x90; // main
static constexpr UCHAR HID_FEATURE = 0xB0; // main
static constexpr UCHAR HID_COLLECTION = 0xA0; // main
static constexpr UCHAR HID_END_COLLECTION = 0xC0; // main (0-byte)

// ---------------------------------------------------------------------------
// Descriptor byte-stream builder
// ---------------------------------------------------------------------------

class DescriptorWriter {
public:
    // Emit a short item with an unsigned value.
    // force_bytes: 0 = choose minimal, 1/2/4 = forced width.
    void itemU(UCHAR tag, ULONG val, int force_bytes = 0) {
        if (force_bytes == 0) force_bytes = minUnsignedBytes(val);
        emitTagAndData(tag & 0xFC, val, force_bytes);
    }

    // Emit a short item with a signed value (sign-extended minimal encoding).
    void itemS(UCHAR tag, LONG val) {
        emitTagAndData(tag & 0xFC, static_cast<ULONG>(val), minSignedBytes(val));
    }

    // Emit raw bytes (for unknown global tokens stored verbatim).
    void raw(UCHAR b) { buf_.push_back(b); }
    void raw(const void* p, size_t n) {
        const auto* b = static_cast<const UCHAR*>(p);
        for (size_t i = 0; i < n; ++i) buf_.push_back(b[i]);
    }

    const std::vector<UCHAR>& bytes() const { return buf_; }

private:
    std::vector<UCHAR> buf_;

    void emitTagAndData(UCHAR base, ULONG val, int nb) {
        switch (nb) {
        case 0: buf_.push_back(base | 0); break;
        case 1:
            buf_.push_back(base | 1);
            buf_.push_back(static_cast<UCHAR>(val));
            break;
        case 2:
            buf_.push_back(base | 2);
            buf_.push_back(static_cast<UCHAR>(val));
            buf_.push_back(static_cast<UCHAR>(val >> 8));
            break;
        default: // 4
            buf_.push_back(base | 3);
            buf_.push_back(static_cast<UCHAR>(val));
            buf_.push_back(static_cast<UCHAR>(val >> 8));
            buf_.push_back(static_cast<UCHAR>(val >> 16));
            buf_.push_back(static_cast<UCHAR>(val >> 24));
            break;
        }
    }

    static int minUnsignedBytes(ULONG v) {
        if (v == 0)      return 0;
        if (v <= 0xFF)   return 1;
        if (v <= 0xFFFF) return 2;
        return 4;
    }

    static int minSignedBytes(LONG v) {
        if (v == 0)                     return 0;
        if (v >= -128 && v <= 127)    return 1;
        if (v >= -32768 && v <= 32767)  return 2;
        return 4;
    }
};

// ---------------------------------------------------------------------------
// Global item state — tracks what was last emitted to suppress redundant items
// ---------------------------------------------------------------------------

struct GlobalState {
    USHORT UsagePage = 0;
    USHORT ReportSize = 0;
    USHORT ReportCount = 0;
    UCHAR  ReportID = 0xFF; // 0xFF = sentinel "nothing emitted yet"
    LONG   LogMin = 0;
    LONG   LogMax = 0;
    LONG   PhyMin = 0;
    LONG   PhyMax = 0;
    ULONG  UnitExp = 0;
    ULONG  Unit = 0;
};

// Emit only the globals that differ from prev; update prev in-place.
static void emitChangedGlobals(DescriptorWriter& w,
    GlobalState& prev,
    const GlobalState& cur)
{
    if (cur.ReportID != prev.ReportID) {
        w.itemU(HID_REPORT_ID, cur.ReportID, 1);
        prev.ReportID = cur.ReportID;
    }
    if (cur.UsagePage != prev.UsagePage) {
        w.itemU(HID_USAGE_PAGE, cur.UsagePage, cur.UsagePage > 0xFF ? 2 : 1);
        prev.UsagePage = cur.UsagePage;
    }
    if (cur.LogMin != prev.LogMin) {
        w.itemS(HID_LOG_MIN, cur.LogMin);
        prev.LogMin = cur.LogMin;
    }
    if (cur.LogMax != prev.LogMax) {
        w.itemS(HID_LOG_MAX, cur.LogMax);
        prev.LogMax = cur.LogMax;
    }
    if (cur.PhyMin != prev.PhyMin) {
        w.itemS(HID_PHY_MIN, cur.PhyMin);
        prev.PhyMin = cur.PhyMin;
    }
    if (cur.PhyMax != prev.PhyMax) {
        w.itemS(HID_PHY_MAX, cur.PhyMax);
        prev.PhyMax = cur.PhyMax;
    }
    if (cur.UnitExp != prev.UnitExp) {
        w.itemU(HID_UNIT_EXP, cur.UnitExp);
        prev.UnitExp = cur.UnitExp;
    }
    if (cur.Unit != prev.Unit) {
        w.itemU(HID_UNIT, cur.Unit);
        prev.Unit = cur.Unit;
    }
    if (cur.ReportSize != prev.ReportSize) {
        w.itemU(HID_REPORT_SIZE, cur.ReportSize);
        prev.ReportSize = cur.ReportSize;
    }
    if (cur.ReportCount != prev.ReportCount) {
        w.itemU(HID_REPORT_COUNT, cur.ReportCount);
        prev.ReportCount = cur.ReportCount;
    }
}

// Build a GlobalState snapshot from a channel descriptor.
static GlobalState globalsOf(const HIDP_CHANNEL_DESC& ch)
{
    GlobalState g;
    g.UsagePage = ch.UsagePage;
    g.ReportSize = ch.ReportSize;
    g.ReportCount = ch.ReportCount;
    g.ReportID = ch.ReportID;
    g.UnitExp = ch.UnitExp;
    g.Unit = ch.Units;

    if (ch.IsButton) {
        // Parser stores LogMin/Max in the button union; Physical is always 0
        g.LogMin = ch.button.LogicalMin;
        g.LogMax = ch.button.LogicalMax;
        g.PhyMin = 0;
        g.PhyMax = 0;
    }
    else {
        g.LogMin = ch.Data.LogicalMin;
        g.LogMax = ch.Data.LogicalMax;
        g.PhyMin = ch.Data.PhysicalMin;
        g.PhyMax = ch.Data.PhysicalMax;
    }
    return g;
}

// ---------------------------------------------------------------------------
// Emit the unknown global items stored in a channel (max 4, see descript.c).
// Re-emitted verbatim before every Main item that carries them.
// ---------------------------------------------------------------------------

static void emitUnknownGlobals(DescriptorWriter& w, const HIDP_CHANNEL_DESC& ch)
{
    for (ULONG i = 0; i < ch.NumGlobalUnknowns; ++i) {
        const HIDP_UNKNOWN_TOKEN& t = ch.GlobalUnknowns[i];
        UCHAR sz = t.Token & 0x03; // size field: 0=0B 1=1B 2=2B 3=4B
        w.raw(t.Token);
        switch (sz) {
        case 1: w.raw(static_cast<UCHAR>(t.BitField)); break;
        case 2: w.raw(&t.BitField, 2);                 break;
        case 3: w.raw(&t.BitField, 4);                 break;
        default: break;
        }
    }
}

// ---------------------------------------------------------------------------
// Emit local items (Usage/UsageMin+Max, Designator, String) for one channel.
// Does NOT emit delimiters — caller is responsible for wrapping with
// Delimiter(1)…Delimiter(0) when the channel is an alias.
// ---------------------------------------------------------------------------

static void emitLocals(DescriptorWriter& w, const HIDP_CHANNEL_DESC& ch)
{
    // Usage or UsageMin/UsageMax
    if (ch.IsRange) {
        w.itemU(HID_USAGE_MIN, ch.Range.UsageMin, ch.Range.UsageMin > 0xFF ? 2 : 1);
        w.itemU(HID_USAGE_MAX, ch.Range.UsageMax, ch.Range.UsageMax > 0xFF ? 2 : 1);
    }
    else {
        USHORT u = ch.NotRange.Usage;
        w.itemU(HID_USAGE, u, u > 0xFF ? 2 : 1);
    }

    // Designator index/range (only when non-zero)
    if (ch.IsDesignatorRange) {
        if (ch.Range.DesignatorMin || ch.Range.DesignatorMax) {
            w.itemU(HID_DESIGNATOR_MIN, ch.Range.DesignatorMin, 1);
            w.itemU(HID_DESIGNATOR_MAX, ch.Range.DesignatorMax, 1);
        }
    }
    else if (ch.NotRange.DesignatorIndex) {
        w.itemU(HID_DESIGNATOR_MIN, ch.NotRange.DesignatorIndex, 1);
        w.itemU(HID_DESIGNATOR_MAX, ch.NotRange.DesignatorIndex, 1);
    }

    // String index/range (only when non-zero)
    if (ch.IsStringRange) {
        if (ch.Range.StringMin || ch.Range.StringMax) {
            w.itemU(HID_STRING_MIN, ch.Range.StringMin, 1);
            w.itemU(HID_STRING_MAX, ch.Range.StringMax, 1);
        }
    }
    else if (ch.NotRange.StringIndex) {
        w.itemU(HID_STRING_MIN, ch.NotRange.StringIndex, 1);
        w.itemU(HID_STRING_MAX, ch.NotRange.StringIndex, 1);
    }
}

// ---------------------------------------------------------------------------
// Emit padding (Constant field, no meaningful usage) to fill a bit gap.
//
// The parser silently drops CONST fields with no usage (descript.c:1819),
// so gaps in bit positions must be covered by explicit padding Main items.
// We use Usage(0) + Constant flag.  Prefer 8-bit granularity; emit a
// separate 1-bit item for the remainder when the gap is not byte-aligned.
// ---------------------------------------------------------------------------

static void emitPadding(DescriptorWriter& w,
    GlobalState& gs,
    ULONG gapBits,
    UCHAR mainTag,
    UCHAR reportID)
{
    if (gapBits == 0) return;

    auto emitPad = [&](USHORT rSize, USHORT rCount) {
        GlobalState pad = {};
        pad.ReportID = reportID;
        pad.UsagePage = 0x01; // Generic Desktop (arbitrary for a Const field)
        pad.ReportSize = rSize;
        pad.ReportCount = rCount;
        // LogMin, LogMax, Phy*, Unit* left 0 — will be emitted only if changed
        emitChangedGlobals(w, gs, pad);
        w.itemU(HID_USAGE, 0, 1);    // Usage(0) — anonymous constant
        w.itemU(mainTag, 0x01, 1); // Constant
        };

    ULONG byteAligned = (gapBits / 8) * 8;
    ULONG remainder = gapBits % 8;

    if (byteAligned)
        emitPad(8, static_cast<USHORT>(byteAligned / 8));
    if (remainder)
        emitPad(1, static_cast<USHORT>(remainder));
}

// ---------------------------------------------------------------------------
// Emit one logical Main item: globals → unknown globals → locals → Main tag.
//
// channels[0..count-1] all belong to the same original Main item:
//   - Array buttons:  MoreChannels=TRUE on [0..count-2], FALSE on [count-1].
//                     All channels share the same bit position and ReportCount.
//   - Alias values:   IsAlias=TRUE on [0..count-2], FALSE on [count-1].
//   - Single field:   count == 1, IsAlias=FALSE, MoreChannels=FALSE.
//
// Alias encoding per HID spec 6.2.2.8 — each alias gets its own Delimiter pair:
//   Delimiter(1), Usage(A), Delimiter(0),
//   Delimiter(1), Usage(B), Delimiter(0),
//   Usage(preferred)
//   <Main item>
//
// Array button encoding: list all usages without delimiters (descript.c:1686
// explicitly rejects delimiters in array declarations).
// ---------------------------------------------------------------------------

static void emitMainGroup(DescriptorWriter& w,
    GlobalState& gs,
    const HIDP_CHANNEL_DESC* channels,
    int count,
    UCHAR mainTag)
{
    const HIDP_CHANNEL_DESC& first = channels[0];

    // 1. Unknown global items (re-emitted verbatim before the Main item)
    emitUnknownGlobals(w, first);

    // 2. Standard global items.
    //    All channels in a group share the same globals (same bit position,
    //    same ReportSize, same logical/physical ranges).  Use first channel.
    GlobalState cur = globalsOf(first);
    emitChangedGlobals(w, gs, cur);

    // 3. Local items.
    if (first.MoreChannels) {
        // Array button: emit all usages consecutively, no delimiters.
        for (int i = 0; i < count; ++i)
            emitLocals(w, channels[i]);
    }
    else {
        // Value item or single button, with optional alias chain.
        // channels[0..count-2] have IsAlias=TRUE → each wrapped in Delimiter pair.
        // channels[count-1] has IsAlias=FALSE → preferred usage, no delimiter.
        for (int i = 0; i < count; ++i) {
            if (channels[i].IsAlias) {
                w.itemU(HID_DELIMITER, 1, 1); // Delimiter open
                emitLocals(w, channels[i]);
                w.itemU(HID_DELIMITER, 0, 1); // Delimiter close
            }
            else {
                emitLocals(w, channels[i]);   // preferred — no delimiter
            }
        }
    }

    // 4. Main item tag.  BitField holds the 8-bit flags byte from the original
    //    descriptor (Data/Const, Array/Var, Abs/Rel, Wrap, NonLinear, etc.).
    w.itemU(mainTag, first.BitField, 1);
}

// ---------------------------------------------------------------------------
// Absolute bit position of a channel's first bit within the report data
// (excluding the ReportID byte, which is an overhead byte not a data field).
//
// ByteOffset counts from the start of the full report packet.  When
// ReportID != 0 the first byte of the packet is the ReportID byte, so data
// starts at byte 1.  We subtract that to get the data-relative offset.
// ---------------------------------------------------------------------------

static ULONG chanBitStart(const HIDP_CHANNEL_DESC& ch)
{
    ULONG byteOff = ch.ByteOffset;
    if (ch.ReportID != 0 && byteOff > 0)
        byteOff -= 1;
    return byteOff * 8 + ch.BitOffset;
}

// ---------------------------------------------------------------------------
// Emit all channels of one report type (Input/Output/Feature) that belong
// to a given LinkCollection, in order of their bit position within each report.
//
// indices[] is pre-sorted by (ReportID, chanBitStart).
// We walk the sorted list, insert padding for gaps, and group alias/array
// chains into single emitMainGroup calls.
// ---------------------------------------------------------------------------

static void emitChannelSlice(DescriptorWriter& w,
    GlobalState& gs,
    const HIDP_CHANNEL_DESC* arr,
    const std::vector<int>& indices,
    UCHAR mainTag)
{
    UCHAR prevReportID = 0xFF; // sentinel: no report seen yet
    ULONG prevBitEnd = 0;

    size_t i = 0;
    while (i < indices.size()) {
        const HIDP_CHANNEL_DESC& ch = arr[indices[i]];

        // Reset bit-tracking at each report boundary
        if (ch.ReportID != prevReportID) {
            prevBitEnd = 0;
            prevReportID = ch.ReportID;
        }

        ULONG bitStart = chanBitStart(ch);

        // Fill any gap before this channel with padding
        if (bitStart > prevBitEnd)
            emitPadding(w, gs, bitStart - prevBitEnd, mainTag, ch.ReportID);

        // Determine group extent:
        //   MoreChannels: advance until the terminating FALSE entry (inclusive)
        //   IsAlias:      advance until the non-alias preferred entry (inclusive)
        //   otherwise:    group of 1
        size_t groupEnd = i;

        if (ch.MoreChannels) {
            while (groupEnd < indices.size() && arr[indices[groupEnd]].MoreChannels)
                ++groupEnd;
            if (groupEnd < indices.size())
                ++groupEnd; // include the MoreChannels=FALSE terminator
        }
        else if (ch.IsAlias) {
            while (groupEnd < indices.size() && arr[indices[groupEnd]].IsAlias)
                ++groupEnd;
            ++groupEnd; // include the IsAlias=FALSE preferred usage
        }
        else {
            groupEnd = i + 1;
        }

        // Copy the group into a contiguous buffer for emitMainGroup
        std::vector<HIDP_CHANNEL_DESC> group;
        group.reserve(groupEnd - i);
        for (size_t k = i; k < groupEnd; ++k)
            group.push_back(arr[indices[k]]);

        emitMainGroup(w, gs, group.data(), static_cast<int>(group.size()), mainTag);

        prevBitEnd = bitStart + ch.BitLength;
        i = groupEnd;
    }
}

// ---------------------------------------------------------------------------
// Recursive collection emitter.
//
// Emits: COLLECTION open → direct channels → child collections → END_COLLECTION.
// Children are traversed in NextSibling order as stored in the tree.
// ---------------------------------------------------------------------------

static void emitCollection(DescriptorWriter& w,
    GlobalState& gs,
    USHORT colIdx,
    const HIDP_PRIVATE_LINK_COLLECTION_NODE* nodes,
    const HIDP_CHANNEL_DESC* inputCh, int inputCount,
    const HIDP_CHANNEL_DESC* outputCh, int outputCount,
    const HIDP_CHANNEL_DESC* featCh, int featCount)
{
    const HIDP_PRIVATE_LINK_COLLECTION_NODE& node = nodes[colIdx];

    // --- Emit COLLECTION open ---
    // The Usage and UsagePage before COLLECTION are local+global items for this
    // collection itself.  We emit them directly (not via emitChangedGlobals so
    // that UsagePage is always present even if it matches the last channel's page).
    w.itemU(HID_USAGE_PAGE, node.LinkUsagePage,
        node.LinkUsagePage > 0xFF ? 2 : 1);
    gs.UsagePage = node.LinkUsagePage; // keep GlobalState in sync

    w.itemU(HID_USAGE, node.LinkUsage,
        node.LinkUsage > 0xFF ? 2 : 1);
    w.itemU(HID_COLLECTION, node.CollectionType, 1);

    // --- Gather indices of channels directly owned by this collection ---
    std::vector<int> inIdx, outIdx, featIdx;

    for (int k = 0; k < inputCount; ++k)
        if (inputCh[k].LinkCollection == colIdx) inIdx.push_back(k);
    for (int k = 0; k < outputCount; ++k)
        if (outputCh[k].LinkCollection == colIdx) outIdx.push_back(k);
    for (int k = 0; k < featCount; ++k)
        if (featCh[k].LinkCollection == colIdx) featIdx.push_back(k);

    // Sort by (ReportID, chanBitStart) for correct padding computation
    auto byPos = [](const HIDP_CHANNEL_DESC* a) {
        return [a](int x, int y) {
            if (a[x].ReportID != a[y].ReportID)
                return a[x].ReportID < a[y].ReportID;
            return chanBitStart(a[x]) < chanBitStart(a[y]);
            };
        };
    std::sort(inIdx.begin(), inIdx.end(), byPos(inputCh));
    std::sort(outIdx.begin(), outIdx.end(), byPos(outputCh));
    std::sort(featIdx.begin(), featIdx.end(), byPos(featCh));

    // --- Emit channels directly belonging to this collection ---
    if (!inIdx.empty())
        emitChannelSlice(w, gs, inputCh, inIdx, HID_INPUT);
    if (!outIdx.empty())
        emitChannelSlice(w, gs, outputCh, outIdx, HID_OUTPUT);
    if (!featIdx.empty())
        emitChannelSlice(w, gs, featCh, featIdx, HID_FEATURE);

    // --- Recurse into child collections (in sibling-list order) ---
    for (USHORT child = node.FirstChild;
        child != 0 && child != 0xFFFF;
        child = nodes[child].NextSibling)
    {
        emitCollection(w, gs, child, nodes,
            inputCh, inputCount,
            outputCh, outputCount,
            featCh, featCount);
    }

    // --- END_COLLECTION (0-byte main item, no data) ---
    w.raw(HID_END_COLLECTION);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/**
 * ReconstructDescriptor
 *
 * Reconstructs a HID Report Descriptor from a HIDP_PREPARSED_DATA blob.
 * The descriptor is functionally equivalent to the original: parsing it
 * with HidP_GetCollectionDescription produces the same channel layout.
 *
 * @param ppd     Pointer to preparsed data (from HidD_GetPreparsedData).
 * @param outDesc Receives the reconstructed descriptor bytes on success.
 * @return        true on success, false if the blob is invalid or empty.
 */
bool ReconstructDescriptor(const PHIDP_PREPARSED_DATA ppd,
    std::vector<UCHAR>& outDesc)
{
    if (!ppd) return false;

    const auto* hdr = reinterpret_cast<const HIDP_PREPARSED_DATA_HDR*>(ppd);
    if (hdr->Signature1 != HIDP_PREPARSED_DATA_SIGNATURE1 ||
        hdr->Signature2 != HIDP_PREPARSED_DATA_SIGNATURE2)
        return false;

    // Channel array starts immediately after the fixed-size header
    const auto* allCh = reinterpret_cast<const HIDP_CHANNEL_DESC*>(hdr + 1);

    const HIDP_CHANNEL_DESC* inputCh = allCh + hdr->Input.Offset;
    const HIDP_CHANNEL_DESC* outputCh = allCh + hdr->Output.Offset;
    const HIDP_CHANNEL_DESC* featCh = allCh + hdr->Feature.Offset;
    int inputCount = hdr->Input.Size;
    int outputCount = hdr->Output.Size;
    int featCount = hdr->Feature.Size;

    // Link-collection array: byte offset measured from the start of allCh
    const auto* lcNodes = reinterpret_cast<const HIDP_PRIVATE_LINK_COLLECTION_NODE*>(
        reinterpret_cast<const UCHAR*>(allCh) + hdr->LinkCollectionArrayOffset);
    int lcCount = hdr->LinkCollectionArrayLength;

    if (lcCount == 0) return false;

    DescriptorWriter w;
    GlobalState gs; // zero-initialized; ReportID sentinel = 0xFF

    // Node 0 is always the top-level application collection
    emitCollection(w, gs, 0, lcNodes,
        inputCh, inputCount,
        outputCh, outputCount,
        featCh, featCount);

    outDesc = w.bytes();
    return !outDesc.empty();
}

/**
 * ReconstructDescriptorFromDevice
 *
 * Obtains preparsed data from an open HID device handle and reconstructs
 * its descriptor.  The handle needs FILE_SHARE_READ access at minimum.
 */
bool ReconstructDescriptorFromDevice(HANDLE hDevice,
    std::vector<UCHAR>& outDesc)
{
    PHIDP_PREPARSED_DATA ppd = nullptr;
    if (!HidD_GetPreparsedData(hDevice, &ppd))
        return false;
    bool ok = ReconstructDescriptor(ppd, outDesc);
    HidD_FreePreparsedData(ppd);
    return ok;
}

// ---------------------------------------------------------------------------
// Smoke test — define HIDDESC_SELFTEST to compile a standalone executable
// ---------------------------------------------------------------------------
#ifdef HIDDESC_SELFTEST

#include <cstdio>

int main()
{
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO devInfo = SetupDiGetClassDevs(
        &hidGuid, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) {
        printf("SetupDiGetClassDevs failed: %lu\n", GetLastError());
        return 1;
    }

    SP_DEVICE_INTERFACE_DATA ifaceData = {};
    ifaceData.cbSize = sizeof(ifaceData);

    for (DWORD idx = 0;
        SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, idx, &ifaceData);
        ++idx)
    {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetail(
            devInfo, &ifaceData, nullptr, 0, &needed, nullptr);

        std::vector<BYTE> detailBuf(needed);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA*>(
            detailBuf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

        if (!SetupDiGetDeviceInterfaceDetail(
            devInfo, &ifaceData, detail, needed, nullptr, nullptr))
            continue;

        HANDLE h = CreateFile(
            detail->DevicePath,
            GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;

        std::vector<UCHAR> desc;
        if (ReconstructDescriptorFromDevice(h, desc)) {
            printf("Device: %s\n", detail->DevicePath);
            printf("Reconstructed %zu bytes:\n", desc.size());
            for (size_t j = 0; j < desc.size(); ++j)
                printf("%02X%c", desc[j], (j + 1) % 16 == 0 ? '\n' : ' ');
            printf("\n\n");
        }

        CloseHandle(h);
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return 0;
}

#endif // HIDDESC_SELFTEST
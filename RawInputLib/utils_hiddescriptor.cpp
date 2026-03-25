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

#define HIDP_MAX_UNKNOWN_ITEMS 4

// Signature bytes in memory: "HidP KDR"
static const UCHAR kPPDMagic[8] = { 'H','i','d','P',' ','K','D','R' };

#pragma pack(push, 1)

//struct HIDP_UNKNOWN_TOKEN {
//    UCHAR Token;        // original item tag byte (tag | type | size bits)
//    UCHAR Reserved[3];
//    ULONG BitField;     // up to 4 bytes of item data, little-endian
//};

struct HIDP_CHANNEL_DESC {
    USHORT  UsagePage;
    UCHAR   ReportID;
    UCHAR   BitOffset;      // 0-7: bit offset within ByteOffset byte
    USHORT  ReportSize;     // bits per single field
    USHORT  ReportCount;    // number of fields in this channel
    // ByteOffset is relative to the start of the full report packet.
    // The parser always reserves a 1-byte slot for ReportID at offset 0,
    // regardless of whether a REPORT_ID item was present.  So data always
    // starts at ByteOffset >= 1.
    USHORT  ByteOffset;
    USHORT  BitLength;      // ReportSize * ReportCount
    ULONG   BitField;       // Main item flags (Data/Const, Array/Var, Abs/Rel…)
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

    ULONG Units;
    ULONG UnitExp;
};

struct CHANNEL_REPORT_HEADER {
    USHORT Offset;   // absolute index of first HIDP_CHANNEL_DESC in Data[]
    USHORT Size;     // total allocated slots (may include unused trailing ones)
    USHORT Index;    // write cursor used during parsing
    USHORT ByteLen;  // report byte length INCLUDING the ReportID byte
};

struct HIDP_SYS_POWER_INFO { ULONG PowerButtonMask; };

struct HIDP_PREPARSED_DATA_HDR {
    UCHAR  MagicKey[8];     // "HidP KDR"
    USHORT Usage;
    USHORT UsagePage;
    HIDP_SYS_POWER_INFO PowerInfo;
    CHANNEL_REPORT_HEADER Input;
    CHANNEL_REPORT_HEADER Output;
    CHANNEL_REPORT_HEADER Feature;
    USHORT LinkCollectionArrayOffset; // byte offset from start of Data[] union
    USHORT LinkCollectionArrayLength;
    // Immediately followed by: HIDP_CHANNEL_DESC Data[]
    // LinkCollection array is at (UCHAR*)Data + LinkCollectionArrayOffset
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
// HID 1.11 short-item tag constants (table 6.2.2)
// Lower 2 bits encode data size: 0=0B 1=1B 2=2B 3=4B — we fill them at emit.
// ---------------------------------------------------------------------------

// Global items
static constexpr UCHAR HID_USAGE_PAGE = 0x04;
static constexpr UCHAR HID_LOG_MIN = 0x14;
static constexpr UCHAR HID_LOG_MAX = 0x24;
static constexpr UCHAR HID_PHY_MIN = 0x34;
static constexpr UCHAR HID_PHY_MAX = 0x44;
static constexpr UCHAR HID_UNIT_EXP = 0x54;
static constexpr UCHAR HID_UNIT = 0x64;
static constexpr UCHAR HID_REPORT_SIZE = 0x74;
static constexpr UCHAR HID_REPORT_ID = 0x84;
static constexpr UCHAR HID_REPORT_COUNT = 0x94;

// Local items
static constexpr UCHAR HID_USAGE = 0x08;
static constexpr UCHAR HID_USAGE_MIN = 0x18;
static constexpr UCHAR HID_USAGE_MAX = 0x28;
static constexpr UCHAR HID_DESIGNATOR_INDEX = 0x38; // single designator
static constexpr UCHAR HID_DESIGNATOR_MIN = 0x48; // designator range start
static constexpr UCHAR HID_DESIGNATOR_MAX = 0x58; // designator range end
static constexpr UCHAR HID_STRING_INDEX = 0x78; // single string
static constexpr UCHAR HID_STRING_MIN = 0x88; // string range start
static constexpr UCHAR HID_STRING_MAX = 0x98; // string range end
static constexpr UCHAR HID_DELIMITER = 0xA8;

// Main items
static constexpr UCHAR HID_INPUT = 0x80;
static constexpr UCHAR HID_OUTPUT = 0x90;
static constexpr UCHAR HID_FEATURE = 0xB0;
static constexpr UCHAR HID_COLLECTION = 0xA0;
static constexpr UCHAR HID_END_COLLECTION = 0xC0; // 0-byte item

// BitField flag: bit 1 clear = Array, bit 1 set = Variable
static constexpr ULONG BITFIELD_VARIABLE = 0x02;

// ---------------------------------------------------------------------------
// Descriptor byte-stream builder
// ---------------------------------------------------------------------------

class DescriptorWriter {
public:
    // Unsigned item — always emits at least 1 data byte (val=0 → one 0x00 byte).
    // force_bytes: 0=minimal(≥1), 1/2/4=exact width.
    void itemU(UCHAR tag, ULONG val, int force_bytes = 0) {
        int nb = (force_bytes > 0) ? force_bytes : minUnsignedBytes(val);
        emitTagAndData(tag & 0xFC, val, nb);
    }

    // Signed item — sign-extended minimal encoding (0 → one 0x00 byte).
    void itemS(UCHAR tag, LONG val) {
        emitTagAndData(tag & 0xFC, static_cast<ULONG>(val), minSignedBytes(val));
    }

    // Raw bytes — for verbatim re-emission of unknown global tokens.
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
        case 0:
            // Special case: 0-byte item (only valid for END_COLLECTION).
            // For all other items we always emit ≥1 data byte.
            buf_.push_back(base | 0);
            break;
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

    // Minimum bytes to represent v unsigned (always ≥ 1).
    static int minUnsignedBytes(ULONG v) {
        if (v <= 0xFF)   return 1;
        if (v <= 0xFFFF) return 2;
        return 4;
    }

    // Minimum bytes to represent v signed (always ≥ 1).
    static int minSignedBytes(LONG v) {
        if (v >= -128 && v <= 127)   return 1;
        if (v >= -32768 && v <= 32767) return 2;
        return 4;
    }
};

// ---------------------------------------------------------------------------
// Global item state — tracks last-emitted values to suppress redundant items.
// Initialised to "nothing ever emitted" so first channel forces all globals.
// ---------------------------------------------------------------------------

struct GlobalState {
    USHORT UsagePage = 0;
    USHORT ReportSize = 0;
    USHORT ReportCount = 0;
    UCHAR  ReportID = 0xFF; // 0xFF = sentinel (never emitted)
    LONG   LogMin = 1;    // intentionally ≠ any real 0 so first emit fires
    LONG   LogMax = 0;
    LONG   PhyMin = 1;
    LONG   PhyMax = 0;
    ULONG  UnitExp = 0xFFFFFFFF;
    ULONG  Unit = 0xFFFFFFFF;
};

// Emit only globals that changed; update prev in-place.
// Report_ID(0) is never emitted — value 0 is reserved (HID 1.11 §6.2.2.7)
// and the Windows parser explicitly rejects it (descript.c:1430).
static void emitChangedGlobals(DescriptorWriter& w,
    GlobalState& prev,
    const GlobalState& cur)
{
    if (cur.ReportID != 0 && cur.ReportID != prev.ReportID) {
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

// Extract a GlobalState snapshot from a channel descriptor.
//
// Two corrections applied here (not to the preparsed data itself):
//
// 1. Button LogMin/LogMax: the parser stores (0, 0) for simple 1-bit buttons.
//    HID spec requires a non-zero range; (0, 1) is the canonical choice.
//
// 2. Value-array ReportCount: when BitField indicates Array mode (bit 1 clear),
//    the original descriptor's Report Count equals the number of distinct values
//    the array can report, which is DataIndexMax - DataIndexMin + 1.
//    The parser stores something different in ReportCount for array channels.
static GlobalState globalsOf(const HIDP_CHANNEL_DESC& ch)
{
    GlobalState g;
    g.UsagePage = ch.UsagePage;
    g.ReportSize = ch.ReportSize;
    g.ReportID = ch.ReportID;
    g.UnitExp = ch.UnitExp;
    g.Unit = ch.Units;

    if (ch.IsButton) {
        // Variable 1-bit buttons: parser zeroes LogMin/LogMax for simple on/off
        // buttons.  HID spec §5.9 says binary controls use 0=off, 1=on, so
        // LogMin(0) / LogMax(1) is the canonical form.
        // Guard: only normalise non-array (Variable) buttons, not array buttons
        // (MoreChannels) whose LogMin/LogMax carry meaningful array range info.
        if (!ch.MoreChannels
            && ch.button.LogicalMin == 0
            && ch.button.LogicalMax == 0) {
            g.LogMin = 0;
            g.LogMax = 1;
        }
        else {
            g.LogMin = ch.button.LogicalMin;
            g.LogMax = ch.button.LogicalMax;
        }
        g.PhyMin = 0;
        g.PhyMax = 0;
        g.ReportCount = ch.ReportCount;
    }
    else {
        g.LogMin = ch.Data.LogicalMin;
        g.LogMax = ch.Data.LogicalMax;
        g.PhyMin = ch.Data.PhysicalMin;
        g.PhyMax = ch.Data.PhysicalMax;

        // Value array: reconstruct ReportCount from DataIndex span.
        if (!(ch.BitField & BITFIELD_VARIABLE) && ch.IsRange) {
            g.ReportCount = static_cast<USHORT>(
                ch.Range.DataIndexMax - ch.Range.DataIndexMin + 1);
        }
        else {
            g.ReportCount = ch.ReportCount;
        }
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
        UCHAR sz = t.Token & 0x03; // 0=0B 1=1B 2=2B 3=4B
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
// Emit local items (Usage/range, Designator, String) for one channel.
// Delimiter wrapping is handled by the caller.
//
// Tag corrections vs previous version:
//   single Designator → HID_DESIGNATOR_INDEX (0x38), not a Min/Max pair
//   single String     → HID_STRING_INDEX     (0x78), not a Min/Max pair
// ---------------------------------------------------------------------------

static void emitLocals(DescriptorWriter& w, const HIDP_CHANNEL_DESC& ch)
{
    // Usage or UsageMin / UsageMax
    if (ch.IsRange) {
        w.itemU(HID_USAGE_MIN, ch.Range.UsageMin, ch.Range.UsageMin > 0xFF ? 2 : 1);
        w.itemU(HID_USAGE_MAX, ch.Range.UsageMax, ch.Range.UsageMax > 0xFF ? 2 : 1);
    }
    else {
        USHORT u = ch.NotRange.Usage;
        w.itemU(HID_USAGE, u, u > 0xFF ? 2 : 1);
    }

    // Designator
    // Index 0 refers to the physical descriptor set count record and is not a
    // useful control reference — skip it (same rationale as HIDAPI).
    if (ch.IsDesignatorRange) {
        if (ch.Range.DesignatorMin || ch.Range.DesignatorMax) {
            w.itemU(HID_DESIGNATOR_MIN, ch.Range.DesignatorMin, 1);
            w.itemU(HID_DESIGNATOR_MAX, ch.Range.DesignatorMax, 1);
        }
    }
    else if (ch.NotRange.DesignatorIndex) {
        w.itemU(HID_DESIGNATOR_INDEX, ch.NotRange.DesignatorIndex, 1);
    }

    // String
    // Index 0 is the language-list descriptor — skip it.
    if (ch.IsStringRange) {
        if (ch.Range.StringMin || ch.Range.StringMax) {
            w.itemU(HID_STRING_MIN, ch.Range.StringMin, 1);
            w.itemU(HID_STRING_MAX, ch.Range.StringMax, 1);
        }
    }
    else if (ch.NotRange.StringIndex) {
        w.itemU(HID_STRING_INDEX, ch.NotRange.StringIndex, 1);
    }
}

// ---------------------------------------------------------------------------
// Emit padding (Constant, no usage) to fill a bit gap.
//
// Per HID 1.11 §6.2.2.9: "Reports can be padded to byte-align fields by
// declaring the appropriately sized main item and NOT declaring a usage for
// the main item."
//
// The Windows parser (descript.c:1804) explicitly takes a "skip it" path
// when it sees a Constant item with no usage — it advances bitPos correctly
// but creates NO channel.  Emitting a Usage(0) before the Constant item
// would cause the parser to take a DIFFERENT path and create an extra channel
// with IsConst=TRUE, which would change the preparsed data layout.
//
// Therefore: emit ONLY the global items + main item, no Usage local item.
// Prefer 8-bit granularity; emit a separate 1-bit item for the remainder.
// ---------------------------------------------------------------------------

static void emitPadding(DescriptorWriter& w,
    GlobalState& gs,
    ULONG gapBits,
    UCHAR mainTag,
    UCHAR reportID)
{
    if (gapBits == 0) return;

    auto emitPad = [&](USHORT rSize, USHORT rCount) {
        GlobalState pad{};
        pad.ReportID = reportID;
        pad.UsagePage = gs.UsagePage; // keep current page — no Usage emitted
        pad.ReportSize = rSize;
        pad.ReportCount = rCount;
        pad.LogMin = 0;
        pad.LogMax = 0;
        pad.PhyMin = 0;
        pad.PhyMax = 0;
        pad.UnitExp = 0;
        pad.Unit = 0;
        emitChangedGlobals(w, gs, pad);
        // No Usage item — parser skips const-with-no-usage, advances bitPos only
        w.itemU(mainTag, 0x03, 1); // Constant | Absolute
        };

    ULONG byteAligned = (gapBits / 8) * 8;
    ULONG remainder = gapBits % 8;
    if (byteAligned)
        emitPad(8, static_cast<USHORT>(byteAligned / 8));
    if (remainder)
        emitPad(1, static_cast<USHORT>(remainder));
}

// ---------------------------------------------------------------------------
// chanBitStart: absolute bit position of a channel's first bit within the
// report DATA (excluding the ReportID overhead byte).
//
// The parser always initialises bitPos to 8 (one byte reserved for ReportID)
// at the start of every collection, even when no REPORT_ID item is present
// (descript.c:1040).  Therefore ByteOffset is always ≥ 1 and always includes
// the ReportID slot.  We subtract 1 unconditionally.
// ---------------------------------------------------------------------------

static ULONG chanBitStart(const HIDP_CHANNEL_DESC& ch)
{
    ULONG byteOff = (ch.ByteOffset > 0u) ? ch.ByteOffset - 1u : 0u;
    return byteOff * 8u + ch.BitOffset;
}

// ---------------------------------------------------------------------------
// Globals-equality test used for button compaction.
// Two consecutive single-bit button channels can be merged into one Main item
// with accumulated ReportCount when all their global items are identical.
// ---------------------------------------------------------------------------

static bool sameGlobals(const HIDP_CHANNEL_DESC& a, const HIDP_CHANNEL_DESC& b)
{
    return a.UsagePage == b.UsagePage
        && a.ReportID == b.ReportID
        && a.ReportSize == b.ReportSize
        && a.BitField == b.BitField
        && a.Units == b.Units
        && a.UnitExp == b.UnitExp
        && a.button.LogicalMin == b.button.LogicalMin
        && a.button.LogicalMax == b.button.LogicalMax;
}

// ---------------------------------------------------------------------------
// Emit one logical Main item: unknown globals → globals → locals → Main tag.
//
// channels[0..count-1] all belong to the same original Main item:
//   Array buttons:   MoreChannels=TRUE on [0..N-2], FALSE on [N-1].
//   Alias values:    IsAlias=TRUE on [0..N-2], FALSE on [N-1].
//   Single field:    count == 1.
//
// Alias encoding (HID spec §6.2.2.8):
//   Delimiter(1), Usage(alias), Delimiter(0)  — for each alias
//   Usage(preferred)                           — no delimiter
//   <Main item>
//
// Array button encoding: all usages listed flat, no delimiters
// (descript.c:1686 explicitly rejects delimiters inside array declarations).
//
// The extra `reportCount` parameter allows the caller to pass an accumulated
// count when multiple adjacent single-bit variable button channels have been
// compacted into this single Main item emission.
// ---------------------------------------------------------------------------

static void emitMainGroup(DescriptorWriter& w,
    GlobalState& gs,
    const HIDP_CHANNEL_DESC* channels,
    int count,
    UCHAR mainTag,
    USHORT extraReportCount = 0)
{
    const HIDP_CHANNEL_DESC& first = channels[0];

    // 1. Unknown global items stored verbatim in preparsed data.
    emitUnknownGlobals(w, first);

    // 2. Standard globals.
    GlobalState cur = globalsOf(first);
    cur.ReportCount = static_cast<USHORT>(cur.ReportCount + extraReportCount);
    emitChangedGlobals(w, gs, cur);

    // 3. Local items.
    if (first.MoreChannels) {
        // Array button: list all possible usages, no delimiter sets.
        for (int i = 0; i < count; ++i)
            emitLocals(w, channels[i]);
    }
    else {
        // Value/button — channels[0..count-2] have IsAlias=TRUE (stored aliases),
        // channels[count-1] has IsAlias=FALSE (stored preferred usage).
        //
        // The parser stored usages in REVERSE order of declaration (usage stack).
        // channels[count-1] = FIRST declared in descriptor = MOST PREFERRED.
        // channels[0..count-2] = later-declared aliases.
        //
        // HID spec §6.2.2.8: "the first usage declared is the most preferred usage
        // for the control" — so preferred must appear FIRST in the output.
        //
        // Correct output order per spec:
        //   Usage(preferred)                             ← channels[count-1], no delimiter
        //   Delimiter(1), Usage(alias), Delimiter(0)    ← channels[count-2..0]
        if (count == 1) {
            // No aliases — just emit the single usage
            emitLocals(w, channels[0]);
        }
        else {
            // Preferred usage first (no delimiter)
            emitLocals(w, channels[count - 1]);
            // Aliases in reverse stored order (restores original declaration order)
            for (int i = count - 2; i >= 0; --i) {
                w.itemU(HID_DELIMITER, 1, 1); // open
                emitLocals(w, channels[i]);
                w.itemU(HID_DELIMITER, 0, 1); // close
            }
        }
    }

    // 4. Main item tag — BitField carries the original 8-bit flags byte.
    w.itemU(mainTag, first.BitField, 1);
}

// ---------------------------------------------------------------------------
// Emit all channels of one report type that belong to a given LinkCollection,
// sorted by (ReportID, chanBitStart).
//
// Key behaviours:
//   • Gaps between channels are filled with Const padding.
//   • MoreChannels chains (array buttons) are grouped into one Main item.
//   • IsAlias chains are grouped into one Main item with Delimiter sets.
//   • Adjacent single-bit variable button channels with identical globals are
//     compacted into one Main item by accumulating ReportCount (button compaction).
// ---------------------------------------------------------------------------

static void emitChannelSlice(DescriptorWriter& w,
    GlobalState& gs,
    const HIDP_CHANNEL_DESC* arr,
    const std::vector<int>& indices,
    UCHAR mainTag)
{
    UCHAR prevReportID = 0xFF; // sentinel
    ULONG prevBitEnd = 0;

    size_t i = 0;
    while (i < indices.size()) {
        const HIDP_CHANNEL_DESC& ch = arr[indices[i]];

        // Reset bit-tracking at each report-ID boundary.
        if (ch.ReportID != prevReportID) {
            prevBitEnd = 0;
            prevReportID = ch.ReportID;
        }

        ULONG bitStart = chanBitStart(ch);

        // Fill gap before this channel.
        if (bitStart > prevBitEnd)
            emitPadding(w, gs, bitStart - prevBitEnd, mainTag, ch.ReportID);

        // Determine group extent.
        size_t groupEnd = i;

        if (ch.MoreChannels) {
            // Array button group: scan to the MoreChannels=FALSE terminator.
            while (groupEnd < indices.size() && arr[indices[groupEnd]].MoreChannels)
                ++groupEnd;
            if (groupEnd < indices.size())
                ++groupEnd; // include the terminator
        }
        else if (ch.IsAlias) {
            // Alias group: scan to IsAlias=FALSE preferred usage.
            while (groupEnd < indices.size() && arr[indices[groupEnd]].IsAlias)
                ++groupEnd;
            ++groupEnd;
        }
        else {
            groupEnd = i + 1;
        }

        // Button compaction: after a group of 1 non-aliased, non-array,
        // single-bit variable button, absorb subsequent identical channels.
        //
        // Conditions (all must hold for compaction):
        //   • group is a single channel (no aliases, no array)
        //   • IsButton and ReportSize == 1 (single bit)
        //   • BitField has Variable flag set (not an array button)
        //   • IsRange is FALSE (individual usage, not a usage range)
        //   • next channel at the same bit position + 1 (contiguous)
        //   • next channel has identical globals
        USHORT extraCount = 0;
        if (groupEnd == i + 1
            && ch.IsButton
            && ch.ReportSize == 1
            && (ch.BitField & BITFIELD_VARIABLE)
            && !ch.IsRange
            && !ch.MoreChannels
            && !ch.IsAlias)
        {
            while (groupEnd < indices.size()) {
                const HIDP_CHANNEL_DESC& next = arr[indices[groupEnd]];
                if (!next.IsButton
                    || next.ReportSize != 1
                    || !(next.BitField & BITFIELD_VARIABLE)
                    || next.IsRange
                    || next.IsAlias
                    || next.MoreChannels
                    || next.ReportID != ch.ReportID
                    || chanBitStart(next) != bitStart + ch.ReportSize * (extraCount + 1)
                    || !sameGlobals(ch, next))
                    break;
                ++extraCount;
                ++groupEnd;
            }
        }

        // Collect the primary group (alias/array chains).
        std::vector<HIDP_CHANNEL_DESC> group;
        group.reserve(groupEnd - i - extraCount);
        size_t primaryEnd = groupEnd - extraCount;
        for (size_t k = i; k < primaryEnd; ++k)
            group.push_back(arr[indices[k]]);

        emitMainGroup(w, gs, group.data(), static_cast<int>(group.size()),
            mainTag, extraCount);

        prevBitEnd = bitStart + ch.BitLength
            + static_cast<ULONG>(ch.ReportSize) * extraCount;
        i = groupEnd;
    }
}

// ---------------------------------------------------------------------------
// Recursive collection emitter.
//
// Order of emission for each node:
//   UsagePage (global) + Usage (local) + COLLECTION(type)
//   → channels belonging directly to this node
//   → child collections in NextSibling order
//   END_COLLECTION
// ---------------------------------------------------------------------------

// Forward declaration so emitChannelSlice can call chanBitStart before it.
static void emitCollection(DescriptorWriter& w,
    GlobalState& gs,
    USHORT colIdx,
    const HIDP_PRIVATE_LINK_COLLECTION_NODE* nodes,
    const HIDP_CHANNEL_DESC* inputCh, int inputCount,
    const HIDP_CHANNEL_DESC* outputCh, int outputCount,
    const HIDP_CHANNEL_DESC* featCh, int featCount,
    bool hasReportIDs,
    const CHANNEL_REPORT_HEADER& inHdr,
    const CHANNEL_REPORT_HEADER& outHdr,
    const CHANNEL_REPORT_HEADER& featHdr);

static void emitCollection(DescriptorWriter& w,
    GlobalState& gs,
    USHORT colIdx,
    const HIDP_PRIVATE_LINK_COLLECTION_NODE* nodes,
    const HIDP_CHANNEL_DESC* inputCh, int inputCount,
    const HIDP_CHANNEL_DESC* outputCh, int outputCount,
    const HIDP_CHANNEL_DESC* featCh, int featCount,
    bool hasReportIDs,
    const CHANNEL_REPORT_HEADER& inHdr,
    const CHANNEL_REPORT_HEADER& outHdr,
    const CHANNEL_REPORT_HEADER& featHdr)
{
    const HIDP_PRIVATE_LINK_COLLECTION_NODE& node = nodes[colIdx];

    // --- COLLECTION open ---
    // Always emit UsagePage explicitly before the collection usage —
    // even if it matches the previous channel's page — because a parser
    // reading from scratch needs it in context.
    w.itemU(HID_USAGE_PAGE, node.LinkUsagePage,
        node.LinkUsagePage > 0xFF ? 2 : 1);
    gs.UsagePage = node.LinkUsagePage; // sync so first channel won't re-emit

    w.itemU(HID_USAGE, node.LinkUsage, node.LinkUsage > 0xFF ? 2 : 1);
    w.itemU(HID_COLLECTION, node.CollectionType, 1);

    // --- Gather + sort channels directly owned by this collection ---
    std::vector<int> inIdx, outIdx, featIdx;
    for (int k = 0; k < inputCount; ++k)
        if (inputCh[k].LinkCollection == colIdx) inIdx.push_back(k);
    for (int k = 0; k < outputCount; ++k)
        if (outputCh[k].LinkCollection == colIdx) outIdx.push_back(k);
    for (int k = 0; k < featCount; ++k)
        if (featCh[k].LinkCollection == colIdx) featIdx.push_back(k);

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

    // --- Emit direct channels ---
    if (!inIdx.empty())
        emitChannelSlice(w, gs, inputCh, inIdx, HID_INPUT);
    if (!outIdx.empty())
        emitChannelSlice(w, gs, outputCh, outIdx, HID_OUTPUT);
    if (!featIdx.empty())
        emitChannelSlice(w, gs, featCh, featIdx, HID_FEATURE);

    // --- Trailing report padding (only for devices without Report IDs) ---
    //
    // For devices with Report IDs, ByteLen is shared across all reports and
    // the per-report trailing padding cannot be recovered from preparsed data.
    // For devices without Report IDs there is exactly one report per type and
    // ByteLen (minus 1 for the absent ReportID byte) gives us the total size.
    //
    // We only emit this at the top-level collection (colIdx == 0) because
    // ByteLen covers the entire report, not just one sub-collection.
    if (!hasReportIDs && colIdx == 0) {
        // Helper: emit trailing padding for one report type.
        auto trailingPad = [&](const std::vector<int>& idx,
            const HIDP_CHANNEL_DESC* arr,
            const CHANNEL_REPORT_HEADER& hdr,
            UCHAR mainTag)
            {
                if (idx.empty() || hdr.ByteLen < 2) return;
                // ByteLen includes the (absent) ReportID byte, so data is ByteLen-1 bytes.
                ULONG totalBits = (static_cast<ULONG>(hdr.ByteLen) - 1u) * 8u;
                // Find last bit used.
                ULONG lastBit = 0;
                for (int k : idx) {
                    ULONG end = chanBitStart(arr[k]) + arr[k].BitLength;
                    if (end > lastBit) lastBit = end;
                }
                if (totalBits > lastBit)
                    emitPadding(w, gs, totalBits - lastBit, mainTag, 0);
            };
        trailingPad(inIdx, inputCh, inHdr, HID_INPUT);
        trailingPad(outIdx, outputCh, outHdr, HID_OUTPUT);
        trailingPad(featIdx, featCh, featHdr, HID_FEATURE);
    }

    // --- Recurse into child collections (NextSibling order) ---
    for (USHORT child = node.FirstChild;
        child != 0 && child != 0xFFFF;
        child = nodes[child].NextSibling)
    {
        emitCollection(w, gs, child, nodes,
            inputCh, inputCount,
            outputCh, outputCount,
            featCh, featCount,
            hasReportIDs, inHdr, outHdr, featHdr);
    }

    // --- END_COLLECTION (0-byte main item) ---
    w.raw(HID_END_COLLECTION);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/**
 * ReconstructDescriptor
 *
 * Reconstructs a HID Report Descriptor from a HIDP_PREPARSED_DATA blob.
 * The result is functionally equivalent to the original: feeding it back to
 * HidP_GetCollectionDescription produces the same channel layout.
 *
 * The input blob is treated as read-only and is never modified.
 *
 * @param ppd     Pointer to preparsed data obtained from HidD_GetPreparsedData.
 * @param outDesc Receives the reconstructed descriptor bytes on success.
 * @return        true on success, false if the blob is invalid or empty.
 */
bool ReconstructDescriptor(const PHIDP_PREPARSED_DATA ppd,
    std::vector<UCHAR>& outDesc)
{
    if (!ppd) return false;

    const auto* hdr = reinterpret_cast<const HIDP_PREPARSED_DATA_HDR*>(ppd);
    if (memcmp(hdr->MagicKey, kPPDMagic, 8) != 0)
        return false;

    // Channel array starts immediately after the fixed-size header.
    const auto* allCh = reinterpret_cast<const HIDP_CHANNEL_DESC*>(hdr + 1);

    // Use the write-cursor (Index) as the count of actually populated entries,
    // identical to HIDAPI's use of LastCap, to skip unused trailing slots.
    int inputCount = hdr->Input.Index;
    int outputCount = hdr->Output.Index;
    int featCount = hdr->Feature.Index;

    const HIDP_CHANNEL_DESC* inputCh = allCh + hdr->Input.Offset;
    const HIDP_CHANNEL_DESC* outputCh = allCh + hdr->Output.Offset;
    const HIDP_CHANNEL_DESC* featCh = allCh + hdr->Feature.Offset;

    // Link-collection array: byte offset measured from the start of allCh.
    const auto* lcNodes =
        reinterpret_cast<const HIDP_PRIVATE_LINK_COLLECTION_NODE*>(
            reinterpret_cast<const UCHAR*>(allCh) + hdr->LinkCollectionArrayOffset);

    if (hdr->LinkCollectionArrayLength == 0) return false;

    // Detect whether any channel carries a non-zero Report ID.
    bool hasReportIDs = false;
    for (int k = 0; k < inputCount && !hasReportIDs; ++k)
        if (inputCh[k].ReportID != 0) hasReportIDs = true;
    for (int k = 0; k < outputCount && !hasReportIDs; ++k)
        if (outputCh[k].ReportID != 0) hasReportIDs = true;
    for (int k = 0; k < featCount && !hasReportIDs; ++k)
        if (featCh[k].ReportID != 0) hasReportIDs = true;

    DescriptorWriter w;
    GlobalState gs; // sentinel-initialised; forces emission of all globals

    // Node 0 is the top-level application collection.
    emitCollection(w, gs, 0, lcNodes,
        inputCh, inputCount,
        outputCh, outputCount,
        featCh, featCount,
        hasReportIDs,
        hdr->Input, hdr->Output, hdr->Feature);

    outDesc = w.bytes();
    return !outDesc.empty();
}

/**
 * ReconstructDescriptorFromDevice
 *
 * Convenience wrapper: obtains preparsed data from an open HID device handle
 * and reconstructs its descriptor.  Requires at least FILE_SHARE_READ access.
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
// Smoke test — define HIDDESC_SELFTEST to build a standalone executable.
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

    SP_DEVICE_INTERFACE_DATA ifaceData{};
    ifaceData.cbSize = sizeof(ifaceData);

    for (DWORD idx = 0;
        SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, idx, &ifaceData);
        ++idx)
    {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetail(
            devInfo, &ifaceData, nullptr, 0, &needed, nullptr);

        std::vector<BYTE> buf(needed);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA*>(buf.data());
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
                printf("%02X%c", desc[j], ((j + 1) % 16 == 0) ? '\n' : ' ');
            printf("\n\n");
        }
        CloseHandle(h);
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return 0;
}

#endif // HIDDESC_SELFTEST
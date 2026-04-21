#include "pch.h"
#include "framework.h"

#include "RawInputDeviceHid.h"
//#include "RawInputDeviceGamepad.h"
//#include "RawInputDeviceWheel.h"
#include "CfgMgr32Wrapper.h"
#include "utils_hiddescriptor.h"

#include <hidusage.h>
#include <winioctl.h>
#include <usbioctl.h>

namespace
{
    bool IsPOV(const HIDP_VALUE_CAPS& vc)
    {
        if (vc.IsRange)
            return false;
        const auto u = static_cast<uint16_t>(vc.NotRange.Usage);
        return (vc.UsagePage == HID_USAGE_PAGE_GENERIC && u == HID_USAGE_GENERIC_HATSWITCH)
            || (vc.UsagePage == HID_USAGE_PAGE_GAME && u == HID_USAGE_GAME_POV);
    }

    struct ParsedRange { int32_t logicalMin, logicalMax; uint8_t bitSize; bool isSigned; };

    ParsedRange ParseLogicalRange(const HIDP_VALUE_CAPS& vc)
    {
        const uint8_t bitSize = static_cast<uint8_t>(vc.BitSize);

        // Invalid or unsupported bit size
        if (bitSize == 0 || bitSize > 32)
            return { 0, 1, 1, false };

        const int32_t unsignedMax =
            (bitSize == 32) ? INT32_MAX : ((1u << bitSize) - 1);

        // Firmware sometimes leaves logical range unset
        if (vc.LogicalMin == 0 && vc.LogicalMax == 0)
        {
            return { 0, unsignedMax, bitSize, false };
        }

        const int32_t min = std::max(static_cast<int32_t>(vc.LogicalMin), -unsignedMax);
        const int32_t max = std::min(static_cast<int32_t>(vc.LogicalMax), unsignedMax);
        const bool isSigned = (min < 0);

        return { min, max, bitSize, isSigned };
    }

    uint32_t ExtractBits(const uint8_t* buf, size_t bufSize, uint32_t bitOffset, uint32_t bitCount)
    {
        const uint32_t byteOffset = bitOffset / 8;
        const uint32_t bitShift = bitOffset % 8;

        uint32_t raw = 0;
        std::memcpy(&raw, buf + byteOffset, std::min<size_t>(4, bufSize - byteOffset));

        raw >>= bitShift;
        raw &= (bitCount < 32) ? ((1u << bitCount) - 1u) : ~0u;
        return raw;
    }
} // namespace

// static
std::unique_ptr<RawInputDevice> RawInputDeviceHid::Create(HANDLE handle)
{
    PreparsedData preparsedData;
    if (!preparsedData.Load(handle))
        return nullptr;

    HIDP_CAPS caps{};
    if (HidP_GetCaps(preparsedData.data, &caps) != HIDP_STATUS_SUCCESS)
        return nullptr;

    /*
    if (caps.UsagePage == HID_USAGE_PAGE_GENERIC)
    {
        switch (caps.Usage)
        {
        case HID_USAGE_GENERIC_GAMEPAD:
        case HID_USAGE_GENERIC_JOYSTICK:
            return std::unique_ptr<RawInputDevice>(new RawInputDeviceGamepad(handle));
        default:
            break;
        }
    }
    else if (caps.UsagePage == HID_USAGE_PAGE_SIMULATION)
    {
        // Steering axis present → wheel; otherwise treat as gamepad.
        USHORT n = 0;
        HidP_GetSpecificValueCaps(HidP_Input,
            HID_USAGE_PAGE_SIMULATION, 0,
            HID_USAGE_SIMULATION_STEERING,
            nullptr, &n, preparsedData.data);

        return std::unique_ptr<RawInputDevice>(
            n > 0 ? new RawInputDeviceWheel(handle)
                  : new RawInputDeviceGamepad(handle));
    }
    */

    auto* device = new RawInputDeviceHid(handle);
    device->Initialize();
    return std::unique_ptr<RawInputDevice>(device);
}

bool RawInputDeviceHid::PreparsedData::Load(HANDLE handle)
{
    UINT size = 0;
    if (::GetRawInputDeviceInfoW(handle, RIDI_PREPARSEDDATA, nullptr, &size) != 0 || size == 0)
        return false;

    buffer = std::make_unique<uint8_t[]>(size);
    data = reinterpret_cast<PHIDP_PREPARSED_DATA>(buffer.get());

    if (::GetRawInputDeviceInfoW(handle, RIDI_PREPARSEDDATA, buffer.get(), &size) != size)
    {
        buffer.reset();
        data = nullptr;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------

RawInputDeviceHid::RawInputDeviceHid(HANDLE handle)
    : RawInputDevice(handle)
{
    //DBGPRINT("New HID device Interface: %s", GetInterfacePath().c_str());
}

RawInputDeviceHid::~RawInputDeviceHid() = default;

// ---------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------

bool RawInputDeviceHid::Initialize()
{
    if (!RawInputDevice::Initialize())
        return false;

    if (!QueryDeviceCapabilities())
        return false;

    return true;
}

// ---------------------------------------------------------------------------
// OnInput
// ---------------------------------------------------------------------------

void RawInputDeviceHid::OnInput(const RAWINPUT* input)
{
    if (!input || input->header.dwType != RIM_TYPEHID)
    {
        DBGPRINT("Wrong HID input.");
        return;
    }

    const RAWHID& raw = input->data.hid;
    if (m_InputReport.parsedData.empty())
        return;

    for (uint32_t ri = 0; ri < raw.dwCount; ++ri)
    {
        const uint8_t* src = raw.bRawData + static_cast<size_t>(ri) * raw.dwSizeHid;
        const uint32_t len = raw.dwSizeHid;

        size_t count = m_InputReport.parsedData.size();
        NTSTATUS status = HidP_GetData(
            HidP_Input,
            m_InputReport.parsedData.data(),
            reinterpret_cast<ULONG*>(&count),
            m_PreparsedData.data,
            const_cast<PCHAR>(reinterpret_cast<const char*>(src)), // read-only, API wart
            static_cast<ULONG>(len));

        if (status != HIDP_STATUS_SUCCESS && status != HIDP_STATUS_BUFFER_TOO_SMALL)
            continue;

        // Buttons / switches absent from the report are released / centred;
        // axes keep their previous value.
        //
        // Only clear buttons that belong to this specific report.
        // Other reports' buttons stay as-is.
        const uint8_t reportId = src[0]; // Report ID is always the first byte
        if (auto it = m_ButtonReportMasks.find(reportId); it != m_ButtonReportMasks.end())
        {
            for (size_t i = 0; i < m_ButtonCount; ++i)
                if (it->second[i])
                    m_Buttons[i].value = false;
        }

        for (size_t i = 0; i < m_SwitchCount; ++i)
            m_Switches[i].value = SwitchPosition::Center;

        // Dispatch controls that have a DataIndex (regular axes, switches, buttons).
        // Kind::ValueArray and Kind::ButtonArray have a DataIndex too but carry
        // no useful per-element data in HIDP_DATA — they are handled below.
        for (uint32_t i = 0; i < count; ++i)
        {
            const HIDP_DATA& d = m_InputReport.parsedData[i];
            if (d.DataIndex >= m_DataIndexTable.size())
                continue;

            const DataIndexEntry& e = m_DataIndexTable[d.DataIndex];

            switch (e.kind)
            {
            case Kind::Axis:
            {
                AxisState& ax = m_Axis[e.index];
                int32_t lv = static_cast<int32_t>(d.RawValue);
                ax.value = NormaliseAxis(lv, ax);
                break;
            }
            case Kind::Switch:
            {
                SwitchState& ss = m_Switches[e.index];
                const int32_t lv = static_cast<int32_t>(d.RawValue);
                ss.value = NormaliseSwitch(lv, ss);
                break;
            }
            case Kind::Button:
            {
                m_Buttons[e.index].value = (d.On != 0);
                break;
            }
            }
        }

        // ---- Value arrays (HidP_GetUsageValueArray) -------------------------
        // Value arrays have no DataIndex per element — HidP_GetData skips them.
        // Iterate m_Axes looking for first elements (reportCount > 1).
        if (!m_InputReport.valueArrayBuffer.empty())
        {
            for (size_t i = 0; i < m_AxisCount; )
            {
                const AxisState& ax = m_Axis[i];
                // reportCount == 1: regular axis, handled above.
                // reportCount == 0: non-first element, handled with its first element.
                // reportCount  > 1: first element of a value array — handle here.
                if (ax.reportCount <= 1) { ++i; continue; }

                status = HidP_GetUsageValueArray(
                    HidP_Input,
                    ax.usagePage, 0, ax.usage,
                    reinterpret_cast<PCHAR>(m_InputReport.valueArrayBuffer.data()),
                    static_cast<USHORT>(m_InputReport.valueArrayBuffer.size()),
                    m_PreparsedData.data,
                    const_cast<PCHAR>(reinterpret_cast<const char*>(src)),
                    static_cast<ULONG>(len));

                if (status == HIDP_STATUS_SUCCESS)
                {
                    for (uint16_t j = 0; j < ax.reportCount; ++j)
                    {
                        if (i + j >= kAxesLengthCap) break;

                        const uint32_t lv = ExtractBits(
                            m_InputReport.valueArrayBuffer.data(),
                            m_InputReport.valueArrayBuffer.size(),
                            j * ax.bitSize, ax.bitSize);

                        AxisState& ax2 = m_Axis[i + j];
                        ax2.value = NormaliseAxis(lv, ax2);
                    }
                }

                i += ax.reportCount;
            }
        }

        // ---- Button arrays (HidP_GetButtonArray, Windows 11+) ---------------
        // Button arrays have a single DataIndex for the whole array; individual
        // element state is only available via HidP_GetButtonArray.
        if (!m_InputReport.buttonArrayBuffer.empty())
        {
            for (size_t i = 0; i < m_ButtonCount; )
            {
                const ButtonState& btn = m_Buttons[i];
                // reportCount == 1: regular button, handled above.
                // reportCount == 0: non-first element, handled with its first element.
                // reportCount  > 1: first element of a button array — handle here.
                if (btn.reportCount <= 1) { ++i; continue; }

                // Clear all elements of this array before reading fresh state.
                for (uint16_t j = 0; j < btn.reportCount && i + j < m_ButtonCount; ++j)
                    m_Buttons[i + j].value = false;

                uint16_t reportCount = btn.reportCount;
                status = HidP_GetButtonArray(
                    HidP_Input,
                    btn.usagePage, 0, btn.usage,
                    m_InputReport.buttonArrayBuffer.data(),
                    &reportCount,
                    m_PreparsedData.data,
                    const_cast<PCHAR>(reinterpret_cast<const char*>(src)),
                    static_cast<ULONG>(len));

                if (status == HIDP_STATUS_SUCCESS)
                {
                    for (uint16_t j = 0; j < reportCount; ++j)
                    {
                        const HIDP_BUTTON_ARRAY_DATA& bad = m_InputReport.buttonArrayBuffer[j];
                        if (bad.ArrayIndex < btn.reportCount)
                        {
                            size_t slot = i + bad.ArrayIndex;
                            if (slot < m_ButtonCount)
                                m_Buttons[slot].value = (bad.On != 0);
                        }
                    }
                }

                i += btn.reportCount;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// QueryDeviceCapabilities
// ---------------------------------------------------------------------------

bool RawInputDeviceHid::QueryDeviceCapabilities()
{
    if (!m_PreparsedData.Load(m_Handle))
        return false;

    //if (!ReconstructDescriptor(m_PreparsedData.data, m_UsbInfo->m_HidReportDescriptor))
    //    return false;

    HIDP_CAPS caps;
    if (HidP_GetCaps(m_PreparsedData.data, &caps) != HIDP_STATUS_SUCCESS)
        return false;

    m_UsagePage = caps.UsagePage;
    m_UsageId = caps.Usage;

    m_DataIndexTable.assign(caps.NumberInputDataIndices, {});

    if (caps.NumberInputButtonCaps > 0)
        QueryButtonCapabilities(caps.NumberInputButtonCaps);

    if (caps.NumberInputValueCaps > 0)
        QueryAxisCapabilities(caps.NumberInputValueCaps);

    const size_t maxCount = static_cast<size_t>(
        HidP_MaxDataListLength(HidP_Input, m_PreparsedData.data));
    m_InputReport.parsedData.resize(maxCount);

    return true;
}

// ---------------------------------------------------------------------------
// QueryButtonCapabilities
// ---------------------------------------------------------------------------

void RawInputDeviceHid::QueryButtonCapabilities(uint16_t count)
{
    auto caps = std::make_unique<HIDP_BUTTON_CAPS[]>(count);
    DCHECK_EQ(HIDP_STATUS_SUCCESS,
        HidP_GetButtonCaps(HidP_Input, caps.get(), &count, m_PreparsedData.data));

    // Register each button / button array in the dispatch table.
    size_t maxButtonArrayCount = 0;

    for (uint16_t i = 0; i < count; ++i)
    {
        const HIDP_BUTTON_CAPS& bc = caps[i];
        if (bc.UsagePage != HID_USAGE_PAGE_BUTTON)
            continue;

        // Button array: single Usage (IsRange == FALSE), ReportCount > 1.
        // HidP assigns ONE DataIndex for the whole array.
        // Individual element state requires HidP_GetButtonArray (Windows 11+).
        if (!bc.IsRange && bc.ReportCount > 1)
        {
            const uint16_t di = bc.NotRange.DataIndex;
            const size_t   firstSlot = m_ButtonCount;

            for (uint16_t j = 0;
                j < bc.ReportCount && m_ButtonCount < kButtonsLengthCap;
                ++j)
            {
                ButtonState& btn = m_Buttons[m_ButtonCount++];
                btn.usagePage = bc.UsagePage;
                btn.usage = bc.NotRange.Usage;
                btn.reportCount = (j == 0) ? bc.ReportCount : 0;
            }

            if (di < m_DataIndexTable.size())
                m_DataIndexTable[di] = { Kind::ButtonArray,
                                         static_cast<uint8_t>(firstSlot),
                                         bc.ReportID };


            auto& mask = m_ButtonReportMasks[bc.ReportID];
            for (size_t j = 0; j < bc.ReportCount && firstSlot + j < kButtonsLengthCap; ++j)
            {
                if (mask.size() <= firstSlot + j)
                    mask.resize(firstSlot + j + 1, false);
                mask[firstSlot + j] = true;
            }

            maxButtonArrayCount = std::max(maxButtonArrayCount, static_cast<size_t>(bc.ReportCount));
            continue;
        }

        // Regular buttons (IsRange or single button).
        const uint16_t uMin = bc.IsRange ? bc.Range.UsageMin : bc.NotRange.Usage;
        const uint16_t uMax = bc.IsRange ? bc.Range.UsageMax : bc.NotRange.Usage;
        const uint16_t diMin = bc.IsRange ? bc.Range.DataIndexMin : bc.NotRange.DataIndex;
        const uint16_t diMax = bc.IsRange ? bc.Range.DataIndexMax : bc.NotRange.DataIndex;

        if (uMin == 0 || uMax == 0)
            continue;

        for (uint16_t di = diMin; di <= diMax; ++di)
        {
            const size_t slot = static_cast<size_t>(uMin - 1)
                + static_cast<size_t>(di - diMin);

            if (slot >= kButtonsLengthCap || di >= m_DataIndexTable.size())
                continue;

            ButtonState& btn = m_Buttons[slot];
            btn.usagePage = bc.UsagePage;
            btn.usage = static_cast<uint16_t>(uMin + (di - diMin));

            m_DataIndexTable[di] = { Kind::Button,
                                     static_cast<uint8_t>(slot),
                                     bc.ReportID };
            m_ButtonCount = std::max(m_ButtonCount, slot + 1);

            auto& mask = m_ButtonReportMasks[bc.ReportID];
            if (mask.size() <= slot)
                mask.resize(slot + 1, false);
            mask[slot] = true;
        }
    }

    // Ensure all masks cover the full button range.
    for (auto& [id, mask] : m_ButtonReportMasks)
        mask.resize(m_ButtonCount, false);

    // Allocate button array buffer only when HidP_GetButtonArray is available
    // (requires HidP version >= 2, i.e. Windows 11+).
    if (maxButtonArrayCount > 0)
    {
        ULONG hidpVersion = 0;
        if (HidP_GetVersion(&hidpVersion) == HIDP_STATUS_SUCCESS && hidpVersion >= 2)
            m_InputReport.buttonArrayBuffer.resize(maxButtonArrayCount);
    }
}

// ---------------------------------------------------------------------------
// QueryAxisCapabilities
// ---------------------------------------------------------------------------

void RawInputDeviceHid::QueryAxisCapabilities(uint16_t count)
{
    auto rawCaps = std::make_unique<HIDP_VALUE_CAPS[]>(count);
    DCHECK_EQ(HIDP_STATUS_SUCCESS,
        HidP_GetValueCaps(HidP_Input, rawCaps.get(), &count, m_PreparsedData.data));

    std::bitset<kAxesLengthCap> axisSlotUsed;
    size_t nextFreeSwitch = 0;

    for (uint16_t i = 0; i < count; ++i)
    {
        const HIDP_VALUE_CAPS& vc = rawCaps[i];
        const auto [logicalMin, logicalMax, bitSize, isSigned] = ParseLogicalRange(vc);

        // Value array: single Usage (IsRange == FALSE), ReportCount > 1.
        // HidP_GetData does not report individual elements — use HidP_GetUsageValueArray.
        if (!vc.IsRange && vc.ReportCount > 1)
        {
            const uint16_t di = vc.NotRange.DataIndex;
            const size_t   firstSlot = m_AxisCount;

            for (uint16_t j = 0;
                j < vc.ReportCount && m_AxisCount < kAxesLengthCap;
                ++j)
            {
                axisSlotUsed.set(m_AxisCount);

                AxisState& ax = m_Axis[m_AxisCount++];
                ax.logicalMin = logicalMin;
                ax.logicalMax = logicalMax;
                ax.bitSize = bitSize;
                ax.isSigned = isSigned;
                ax.isAbsolute = (vc.IsAbsolute != FALSE);
                ax.usagePage = vc.UsagePage;
                ax.usage = static_cast<uint16_t>(vc.NotRange.Usage);
                ax.physicalMin = vc.PhysicalMin;
                ax.physicalMax = vc.PhysicalMax;
                ax.units = vc.Units;
                ax.unitsExp = static_cast<int16_t>(vc.UnitsExp);
                ax.reportCount = (j == 0) ? vc.ReportCount : 0;
            }

            if (di < m_DataIndexTable.size())
                m_DataIndexTable[di] = { Kind::ValueArray,
                                         static_cast<uint8_t>(firstSlot),
                                         vc.ReportID };
            continue;
        }

        // ---- Switch (POV) ----
        if (IsPOV(vc) && nextFreeSwitch < kSwitchLengthCap)
        {
            const size_t switchIdx = nextFreeSwitch++;

            SwitchState& ss = m_Switches[switchIdx];
            ss.usagePage = vc.UsagePage;
            ss.usage = static_cast<uint16_t>(vc.NotRange.Usage);
            ss.logicalMin = logicalMin;
            ss.logicalMax = logicalMax;

            const int32_t lUnits = logicalMax - logicalMin + 1;
            ss.granularity = (lUnits > 0)
                ? static_cast<uint16_t>(36000u / static_cast<uint32_t>(lUnits))
                : 0u;

            m_SwitchCount = nextFreeSwitch;

            const uint16_t di = static_cast<uint16_t>(
                vc.IsRange ? vc.Range.DataIndexMin : vc.NotRange.DataIndex);

            if (di < m_DataIndexTable.size())
                m_DataIndexTable[di] = { Kind::Switch,
                                         static_cast<uint8_t>(switchIdx),
                                         vc.ReportID };
            continue;
        }

        // ---- Regular axis ----
        const uint16_t usageMin = static_cast<uint16_t>(
            vc.IsRange ? vc.Range.UsageMin : vc.NotRange.Usage);

        // Prefer the slot that matches the HID generic desktop usage offset
        // (X=0, Y=1, Z=2, Rx=3, Ry=4, Rz=5, Slider=6, Dial=7).
        const size_t prefSlot = (usageMin >= HID_USAGE_GENERIC_X)
            ? static_cast<size_t>(usageMin - HID_USAGE_GENERIC_X)
            : kAxesLengthCap;

        size_t slot = kAxesLengthCap;
        if (prefSlot < kAxesLengthCap && !axisSlotUsed[prefSlot])
            slot = prefSlot;
        else
            for (size_t s = 0; s < kAxesLengthCap; ++s)
                if (!axisSlotUsed[s]) { slot = s; break; }

        if (slot >= kAxesLengthCap)
            continue;

        axisSlotUsed.set(slot);

        AxisState& ax = m_Axis[slot];
        ax.logicalMin = logicalMin;
        ax.logicalMax = logicalMax;
        ax.bitSize = bitSize;
        ax.isSigned = isSigned;
        ax.isAbsolute = (vc.IsAbsolute != FALSE);
        ax.usagePage = vc.UsagePage;
        ax.usage = usageMin;
        ax.physicalMin = vc.PhysicalMin;
        ax.physicalMax = vc.PhysicalMax;
        ax.units = vc.Units;
        ax.unitsExp = static_cast<int16_t>(vc.UnitsExp);

        m_AxisCount = std::max(m_AxisCount, slot + 1);

        const uint16_t diMin = static_cast<uint16_t>(
            vc.IsRange ? vc.Range.DataIndexMin : vc.NotRange.DataIndex);
        const uint16_t diMax = static_cast<uint16_t>(
            vc.IsRange ? vc.Range.DataIndexMax : vc.NotRange.DataIndex);

        for (uint16_t di = diMin; di <= diMax; ++di)
            if (di < m_DataIndexTable.size())
                m_DataIndexTable[di] = { Kind::Axis,
                                         static_cast<uint8_t>(slot),
                                         vc.ReportID };
    }

    // Allocate value array buffer sized for the largest array.
    size_t maxArrayBytes = 0;
    for (size_t i = 0; i < m_AxisCount; ++i)
        if (m_Axis[i].reportCount > 1)
            maxArrayBytes = std::max(maxArrayBytes, static_cast<size_t>((m_Axis[i].bitSize * m_Axis[i].reportCount + 7) / 8));

    m_InputReport.valueArrayBuffer.resize(maxArrayBytes);
}

float RawInputDeviceHid::NormaliseAxis(int32_t lv, const AxisState& ax)
{
    // Sign-extend value based on bit size
    if (ax.isSigned && ax.bitSize < 32)
    {
        const int32_t shift = 32 - ax.bitSize;
        lv = (lv << shift) >> shift;
    }

    // Relative axes accumulate deltas
    if (!ax.isAbsolute)
        return ax.value + static_cast<float>(lv);

    // Linear map [logicalMin, logicalMax] → [-1, +1]
    const float min = static_cast<float>(ax.logicalMin);
    const float max = static_cast<float>(ax.logicalMax);
    const float range = max - min;

    if (range == 0.f)
        return 0.f;

    const float t = (static_cast<float>(lv) - min) / range;

    return std::clamp(2.f * t - 1.f, -1.f, 1.f);
}

SwitchPosition RawInputDeviceHid::NormaliseSwitch(int32_t lv, const SwitchState& ss)
{
    if (lv < ss.logicalMin || lv > ss.logicalMax)
        return SwitchPosition::Center;

    // Convert to hundredths of a degree
    const int32_t angle = (lv - ss.logicalMin) * ss.granularity;

    // 8 directions, each spanning 45° = 4500 hundredths
    // Add half-step (2250) before dividing to get nearest direction
    const int32_t index = ((angle + 2250) % 36000) / 4500;

    // index 0..7 → Up, UpRight, Right, DownRight, Down, DownLeft, Left, UpLeft
    static constexpr SwitchPosition kMap[8] = {
        SwitchPosition::Up,
        SwitchPosition::UpRight,
        SwitchPosition::Right,
        SwitchPosition::DownRight,
        SwitchPosition::Down,
        SwitchPosition::DownLeft,
        SwitchPosition::Left,
        SwitchPosition::UpLeft,
    };
    return kMap[index];
}

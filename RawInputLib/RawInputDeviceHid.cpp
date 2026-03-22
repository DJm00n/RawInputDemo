#include "pch.h"
#include "framework.h"

#include "RawInputDeviceHid.h"
//#include "RawInputDeviceGamepad.h"
//#include "RawInputDeviceWheel.h"
#include "CfgMgr32Wrapper.h"

#include <hidusage.h>
#include <winioctl.h>
#include <usbioctl.h>

namespace
{
    // DI: lMask = ~((1 << (BitSize-1)) - 1)
    int32_t SignMask(uint16_t bitSize)
    {
        if (bitSize == 0 || bitSize >= 32)
            return 0;
        return ~((1 << (bitSize - 1)) - 1);
    }

    // Linear map [logMin, logMax] → [-1, +1].
    float NormaliseAxis(int32_t v, int32_t logMin, int32_t logMax)
    {
        const int32_t range = logMax - logMin;
        if (range == 0)
            return 0.f;
        return std::clamp(2.f * static_cast<float>(v - logMin) / range - 1.f,
            -1.f, 1.f);
    }

    // DI: hat iff single usage == HatSwitch or Game POV.
    constexpr uint16_t kGamePOVUsage = 0x20;

    bool IsPOV(const HIDP_VALUE_CAPS& vc)
    {
        if (vc.IsRange)
            return false;
        const auto u = static_cast<uint16_t>(vc.NotRange.Usage);
        return (vc.UsagePage == HID_USAGE_PAGE_GENERIC && u == HID_USAGE_GENERIC_HATSWITCH)
            || (vc.UsagePage == HID_USAGE_PAGE_GAME && u == kGamePOVUsage);
    }

    struct ParsedRange { int32_t lMin, lMax, mask; bool isSigned; };

    ParsedRange ParseLogicalRange(const HIDP_VALUE_CAPS& vc)
    {
        const int32_t signMask = SignMask(vc.BitSize);
        const int32_t unsignedMax = std::max(1, (1 << vc.BitSize) - 1);

        if (vc.LogicalMin == 0 && vc.LogicalMax == 0)
            return { 0, unsignedMax, unsignedMax, false };

        if (vc.LogicalMin >= signMask && vc.LogicalMax <= ~signMask)
            return { vc.LogicalMin, vc.LogicalMax, signMask, true };

        if (vc.LogicalMin >= 0 && vc.LogicalMax <= unsignedMax)
            return { vc.LogicalMin, vc.LogicalMax, unsignedMax, false };

        return { signMask, ~signMask, signMask, true }; // bad firmware
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
            nullptr, &n, ppd);

        if (n > 0)
            return std::unique_ptr<RawInputDevice>(new RawInputDeviceWheel(handle));
        else
            return std::unique_ptr<RawInputDevice>(new RawInputDeviceGamepad(handle));
    }*/

    return std::unique_ptr<RawInputDevice>(new RawInputDeviceHid(handle));
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
    m_IsValid = QueryDeviceInfo();

    //DBGPRINT("New HID device Interface: %s", GetInterfacePath().c_str());
}

RawInputDeviceHid::~RawInputDeviceHid() = default;

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
    if (m_InputReport.maxCount == 0)
        return;

    for (uint32_t ri = 0; ri < raw.dwCount; ++ri)
    {
        const uint8_t* src = raw.bRawData + static_cast<size_t>(ri) * raw.dwSizeHid;
        const uint32_t len = raw.dwSizeHid;

        uint32_t count = m_InputReport.maxCount;
        const NTSTATUS st = HidP_GetData(
            HidP_Input,
            m_InputReport.parsedData.data(),
            reinterpret_cast<ULONG*>(&count),
            m_PreparsedData.data,
            const_cast<PCHAR>(reinterpret_cast<const char*>(src)), // read-only, API wart
            static_cast<ULONG>(len));

        if (st != HIDP_STATUS_SUCCESS && st != HIDP_STATUS_BUFFER_TOO_SMALL)
            continue;

        // DI rule: buttons / hats absent from the report are released/centred;
        // axes keep their previous value.
        //
        // When multiple Report IDs exist, only clear buttons that belong to
        // this specific report — other reports' buttons stay as-is.
        // DI: rgpbButtonMasks[] (dihidini.c).
        const uint8_t reportId = src[0]; // Report ID is always first byte
        if (m_ButtonReportMasks.empty())
        {
            // Fast path: single Report ID, clear all buttons.
            std::fill(std::begin(m_Buttons), std::begin(m_Buttons) + m_ButtonsLength, false);
        }
        else if (auto it = m_ButtonReportMasks.find(reportId); it != m_ButtonReportMasks.end())
        {
            const std::vector<bool>& mask = it->second;
            for (size_t i = 0; i < m_ButtonsLength; ++i)
                if (mask[i])
                    m_Buttons[i] = false;
        }

        for (size_t i = 0; i < m_HatsLength; ++i)
            m_Hats[i].value = -1;

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
                AxisState& ax = m_Axes[e.index];
                int32_t lv = static_cast<int32_t>(d.RawValue);

                // Sign-extend if top bit of BitSize-wide field is set.
                // DI: if (lValue & lMask) { isSigned ? lv |= mask : lv &= ~mask }
                if (ax.signMask && (lv & ax.signMask))
                    lv = ax.isSigned ? (lv | ax.signMask) : (lv & ~ax.signMask);

                ax.value = ax.isAbsolute
                    ? NormaliseAxis(lv, ax.logicalMin, ax.logicalMax)
                    : ax.value + static_cast<float>(lv);
                break;
            }
            case Kind::Hat:
            {
                HatState& hat = m_Hats[e.index];
                const int32_t lv = static_cast<int32_t>(d.RawValue);

                hat.value = (lv < hat.logicalMin || lv > hat.logicalMax)
                    ? -1
                    : (lv - hat.logicalMin) * hat.povGranularity;
                break;
            }
            case Kind::Button:
                m_Buttons[e.index] = (d.On != 0);
                break;

            case Kind::None:
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// QueryDeviceInfo
// ---------------------------------------------------------------------------

bool RawInputDeviceHid::QueryDeviceInfo()
{
    if (!RawInputDevice::QueryDeviceInfo())
        return false;

    if (!QueryDeviceCapabilities())
        return false;

    return true;
}

// ---------------------------------------------------------------------------
// QueryDeviceCapabilities
// ---------------------------------------------------------------------------

bool RawInputDeviceHid::QueryDeviceCapabilities()
{
	if (!m_PreparsedData.Load(m_Handle))
		return false;

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

    m_InputReport.maxCount = static_cast<uint32_t>(
        HidP_MaxDataListLength(HidP_Input, m_PreparsedData.data));
    m_InputReport.parsedData.resize(m_InputReport.maxCount);

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

    // First pass: collect unique Report IDs to decide if we need per-ID masks.
    // If all buttons share the same Report ID (common case), we skip the mask.
    uint8_t firstReportId = 0;
    bool    multipleReportIds = false;

    for (uint16_t i = 0; i < count; ++i)
    {
        const HIDP_BUTTON_CAPS& bc = caps[i];
        if (bc.UsagePage != HID_USAGE_PAGE_BUTTON)
            continue;

        if (firstReportId == 0)
            firstReportId = bc.ReportID;
        else if (bc.ReportID != firstReportId)
            multipleReportIds = true;
    }

    // Second pass: register each button in the dispatch table.
    for (uint16_t i = 0; i < count; ++i)
    {
        const HIDP_BUTTON_CAPS& bc = caps[i];
        if (bc.UsagePage != HID_USAGE_PAGE_BUTTON)
            continue;

        const uint16_t uMin = bc.IsRange ? bc.Range.UsageMin : bc.NotRange.Usage;
        const uint16_t uMax = bc.IsRange ? bc.Range.UsageMax : bc.NotRange.Usage;
        const uint16_t diMin = bc.Range.DataIndexMin;
        const uint16_t diMax = bc.Range.DataIndexMax;

        if (uMin == 0 || uMax == 0)
            continue;

        for (uint16_t di = diMin; di <= diMax; ++di)
        {
            const size_t slot = static_cast<size_t>(uMin - 1)
                + static_cast<size_t>(di - diMin);

            if (slot >= kButtonsLengthCap || di >= m_DataIndexTable.size())
                continue;

            m_DataIndexTable[di] = { Kind::Button,
                                     static_cast<uint8_t>(slot),
                                     bc.ReportID };
            m_ButtonsLength = std::max(m_ButtonsLength, slot + 1);

            // Build the per-Report-ID reset mask if needed.
            // DI: rgpbButtonMasks[] — each mask is a bitset over the button array.
            // On receive of report R, we AND m_Buttons[i] with ~mask[R][i],
            // zeroing only the buttons that belong to R.
            if (multipleReportIds)
            {
                auto& mask = m_ButtonReportMasks[bc.ReportID];
                if (mask.size() <= slot)
                    mask.resize(slot + 1, false);
                mask[slot] = true;
            }
        }
    }

    // Make sure all masks cover the full button range.
    if (multipleReportIds)
    {
        for (auto& [id, mask] : m_ButtonReportMasks)
            mask.resize(m_ButtonsLength, false);
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
    size_t nextFreeHat = 0;

    for (uint16_t i = 0; i < count; ++i)
    {
        const HIDP_VALUE_CAPS& vc = rawCaps[i];

        // ---- hat ----
        if (IsPOV(vc) && nextFreeHat < kHatsLengthCap)
        {
            const auto [lMin, lMax, mask, signed_] = ParseLogicalRange(vc);
            const size_t hatIdx = nextFreeHat++;

            HatState& hat = m_Hats[hatIdx];
            hat.logicalMin = lMin;
            hat.logicalMax = lMax;
            hat.value = -1;

            const int32_t lUnits = lMax - lMin + 1;
            hat.povGranularity = (lUnits > 0)
                ? static_cast<uint16_t>(36000u / static_cast<uint32_t>(lUnits))
                : 0u;

            m_HatsLength = nextFreeHat;

            const uint16_t di = static_cast<uint16_t>(
                vc.IsRange ? vc.Range.DataIndexMin : vc.NotRange.DataIndex);

            if (di < m_DataIndexTable.size())
                m_DataIndexTable[di] = { Kind::Hat,
                                         static_cast<uint8_t>(hatIdx),
                                         vc.ReportID };
            continue;
        }

        // ---- axis ----
        const uint16_t usageMin = static_cast<uint16_t>(
            vc.IsRange ? vc.Range.UsageMin : vc.NotRange.Usage);

        const size_t prefSlot = (usageMin >= HID_USAGE_GENERIC_X)
            ? static_cast<size_t>(usageMin - HID_USAGE_GENERIC_X)
            : kAxesLengthCap;

        size_t slot = kAxesLengthCap;
        if (prefSlot < kAxesLengthCap && !axisSlotUsed[prefSlot])
        {
            slot = prefSlot;
        }
        else
        {
            for (size_t s = 0; s < kAxesLengthCap; ++s)
                if (!axisSlotUsed[s]) { slot = s; break; }
        }

        if (slot >= kAxesLengthCap)
            continue;

        axisSlotUsed.set(slot);

        const auto [lMin, lMax, mask, isSigned] = ParseLogicalRange(vc);

        AxisState& ax = m_Axes[slot];
        ax.logicalMin = lMin;
        ax.logicalMax = lMax;
        ax.signMask = mask;
        ax.isSigned = isSigned;
        ax.isAbsolute = (vc.IsAbsolute != FALSE);
        ax.value = 0.f;

        // Metadata for subclasses
        ax.usagePage = vc.UsagePage;
        ax.usage = usageMin;
        ax.physicalMin = vc.PhysicalMin;
        ax.physicalMax = vc.PhysicalMax;
        ax.units = vc.Units;
        ax.unitsExp = static_cast<int16_t>(vc.UnitsExp);

        m_AxesLength = std::max(m_AxesLength, slot + 1);

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
}

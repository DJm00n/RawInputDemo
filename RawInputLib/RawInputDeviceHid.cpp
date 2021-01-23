#include "pch.h"
#include "framework.h"

#include "RawInputDeviceHid.h"

#include <hidusage.h>

namespace
{
    unsigned long GetBitmask(unsigned short bits)
    {
        return (1 << bits) - 1;
    }
}

RawInputDeviceHid::RawInputDeviceHid(HANDLE handle)
    : RawInputDevice(handle)
{
    m_IsValid = QueryDeviceInfo();

    DBGPRINT("New HID device[VID:%04X,PID:%04X][UP:%04X,U:%04X]: '%s', Interface: `%s`", GetVendorId(), GetProductId(), GetUsagePage(), GetUsageId(), GetProductString().c_str(), GetInterfacePath().c_str());
}

RawInputDeviceHid::~RawInputDeviceHid()
{
    DBGPRINT("Removed HID device: '%s', Interface: `%s`", GetProductString().c_str(), GetInterfacePath().c_str());
}

void RawInputDeviceHid::OnInput(const RAWINPUT* /*input*/)
{
    //TODO
}

bool RawInputDeviceHid::QueryDeviceInfo()
{
    if (!RawInputDevice::QueryDeviceInfo())
        return false;

    // We can now use the name to query the OS for a file handle that is used to
    // read the product string from the device. If the OS does not return a valid
    // handle this device is invalid.
    if (!IsValidHandle(m_InterfaceHandle.get()))
        return false;

    // Fetch information about the buttons and axes on this device. This sets
    // |m_ButtonsLength| and |m_AxesLength| to their correct values and populates
    // |m_Axes| with capabilities info.
    if (!QueryDeviceCapabilities())
        return false;

    // Gamepads must have at least one button or axis.
    //if (m_ButtonsLength == 0 && m_AxesLength == 0)
    //    return false;

    return true;
}

bool RawInputDeviceHid::QueryDeviceCapabilities()
{
    UINT size = 0;

    UINT result = ::GetRawInputDeviceInfo(m_Handle, RIDI_PREPARSEDDATA, nullptr, &size);
    if (result == static_cast<UINT>(-1))
    {
        //PLOG(ERROR) << "GetRawInputDeviceInfo() failed";
        return false;
    }
    DCHECK_EQ(0u, result);

    m_PPDBuffer = std::make_unique<uint8_t[]>(size);
    m_PreparsedData = reinterpret_cast<PHIDP_PREPARSED_DATA>(m_PPDBuffer.get());
    result = ::GetRawInputDeviceInfo(m_Handle, RIDI_PREPARSEDDATA, m_PPDBuffer.get(), &size);
    if (result == static_cast<UINT>(-1))
    {
        //PLOG(ERROR) << "GetRawInputDeviceInfo() failed";
        return false;
    }
    DCHECK_EQ(size, result);

    HIDP_CAPS caps;
    NTSTATUS status = HidP_GetCaps(m_PreparsedData, &caps);
    DCHECK_EQ(HIDP_STATUS_SUCCESS, status);

    m_UsagePage = caps.UsagePage;
    m_UsageId = caps.Usage;

    if (caps.NumberInputButtonCaps > 0)
        QueryButtonCapabilities(caps.NumberInputButtonCaps);

    if (caps.NumberInputValueCaps > 0)
        QueryAxisCapabilities(caps.NumberInputValueCaps);

    return true;
}

void RawInputDeviceHid::QueryButtonCapabilities(uint16_t button_count)
{
    if (button_count > 0)
    {
        auto button_caps = std::make_unique<HIDP_BUTTON_CAPS[]>(button_count);

        NTSTATUS status = HidP_GetButtonCaps(HidP_Input, button_caps.get(), &button_count, m_PreparsedData);
        DCHECK_EQ(HIDP_STATUS_SUCCESS, status);

        // Keep track of which button indices are in use.
        std::vector<bool> button_indices_used(kButtonsLengthCap, false);

        // Collect all inputs from the Button usage page.
        QueryNormalButtonCapabilities(button_caps.get(), button_count, &button_indices_used);
    }
}

void RawInputDeviceHid::QueryNormalButtonCapabilities(HIDP_BUTTON_CAPS button_caps[], uint16_t button_count, std::vector<bool> * button_indices_used)
{
    DCHECK(button_caps);
    DCHECK(button_indices_used);

    // Collect all inputs from the Button usage page and assign button indices
    // based on the usage value.
    for (size_t i = 0; i < button_count; ++i)
    {
        uint16_t usage_page = button_caps[i].UsagePage;
        uint16_t usage_min = button_caps[i].Range.UsageMin;
        uint16_t usage_max = button_caps[i].Range.UsageMax;

        if (usage_min == 0 || usage_max == 0)
            continue;

        size_t button_index_min = usage_min - 1;
        size_t button_index_max = usage_max - 1;
        if (usage_page == HID_USAGE_PAGE_BUTTON && button_index_min < kButtonsLengthCap)
        {
            button_index_max = std::min(kButtonsLengthCap - 1, button_index_max);
            m_ButtonsLength = std::max(m_ButtonsLength, button_index_max + 1);
            for (size_t j = button_index_min; j <= button_index_max; ++j)
                (*button_indices_used)[j] = true;
        }
    }
}

void RawInputDeviceHid::QueryAxisCapabilities(uint16_t axis_count)
{
    auto axes = std::make_unique<HIDP_VALUE_CAPS[]>(axis_count);

    NTSTATUS status = HidP_GetValueCaps(HidP_Input, axes.get(), &axis_count, m_PreparsedData);

    DCHECK_EQ(HIDP_STATUS_SUCCESS, status);

    bool mapped_all_axes = true;

    for (size_t i = 0; i < axis_count; i++)
    {
        size_t axis_index = axes[i].Range.UsageMin - HID_USAGE_GENERIC_X;
        if (axis_index < kAxesLengthCap && !m_Axes[axis_index].active)
        {
            m_Axes[axis_index].caps = axes[i];
            m_Axes[axis_index].value = 0;
            m_Axes[axis_index].active = true;
            m_Axes[axis_index].bitmask = GetBitmask(axes[i].BitSize);
            m_AxesLength = std::max(m_AxesLength, axis_index + 1);
        }
        else
        {
            mapped_all_axes = false;
        }
    }

    if (!mapped_all_axes)
    {
        // For axes whose usage puts them outside the standard axesLengthCap range.
        size_t next_index = 0;
        for (size_t i = 0; i < axis_count; i++)
        {
            size_t usage = axes[i].Range.UsageMin - HID_USAGE_GENERIC_X;
            if (usage >= kAxesLengthCap &&
                axes[i].UsagePage <= HID_USAGE_PAGE_GAME)
            {
                for (; next_index < kAxesLengthCap; ++next_index)
                {
                    if (!m_Axes[next_index].active)
                        break;
                }
                if (next_index < kAxesLengthCap)
                {
                    m_Axes[next_index].caps = axes[i];
                    m_Axes[next_index].value = 0;
                    m_Axes[next_index].active = true;
                    m_Axes[next_index].bitmask = GetBitmask(axes[i].BitSize);
                    m_AxesLength = std::max(m_AxesLength, next_index + 1);
                }
            }

            if (next_index >= kAxesLengthCap)
                break;
        }
    }
}

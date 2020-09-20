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
}

RawInputDeviceHid::~RawInputDeviceHid() = default;

void RawInputDeviceHid::OnInput(const RAWINPUT* /*input*/)
{
    //TODO
}

bool RawInputDeviceHid::QueryDeviceInfo()
{
    // Fetch the device's |name_| (RIDI_DEVICENAME).
    if (!QueryRawDeviceName())
        return false;

    // Fetch HID properties (RID_DEVICE_INFO_HID) for this device. This includes
    // |m_VendorId|, |m_ProductId|, |m_VersionNumber|, and |usage_|.
    if (!QueryHidInfo())
        return false;

    // We can now use the name to query the OS for a file handle that is used to
    // read the product string from the device. If the OS does not return a valid
    // handle this device is invalid.
    auto device_handle = OpenHidDevice();
    if (!IsValidHandle(device_handle.get()))
        return false;

    // Fetch the human-friendly |m_ManufacturerString|, if available.
    if (!QueryManufacturerString(device_handle))
        m_ManufacturerString = "Unknown Vendor";

    // Fetch the human-friendly |m_ProductString|, if available.
    if (!QueryProductString(device_handle))
        m_ProductString = "Unknown HID Device";

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

ScopedHandle RawInputDeviceHid::OpenHidDevice() const
{
    return ScopedHandle(::CreateFile(
        utf8::widen(m_Name).c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, /*lpSecurityAttributes=*/nullptr,
        OPEN_EXISTING, /*dwFlagsAndAttributes=*/0, /*hTemplateFile=*/nullptr));
}

bool RawInputDeviceHid::QueryHidInfo()
{
    RID_DEVICE_INFO device_info;

    if (!QueryRawDeviceInfo(m_Handle, &device_info))
        return false;

    DCHECK_EQ(device_info.dwType, static_cast<DWORD>(RIM_TYPEHID));

    std::memcpy(&m_HidInfo, &device_info.hid, sizeof(m_HidInfo));

    return true;
}

bool RawInputDeviceHid::QueryManufacturerString(ScopedHandle& device_handle)
{
    DCHECK(IsValidHandle(device_handle.get()));

    std::wstring manufacturerString;
    manufacturerString.resize(RawInputDevice::kIdLengthCap);

    if (!HidD_GetManufacturerString(device_handle.get(), &manufacturerString.front(), RawInputDevice::kIdLengthCap))
        return false;

    m_ManufacturerString = utf8::narrow(manufacturerString);

    return true;
}

bool RawInputDeviceHid::QueryProductString(ScopedHandle & device_handle)
{
    DCHECK(IsValidHandle(device_handle.get()));

    std::wstring productString;
    productString.resize(RawInputDevice::kIdLengthCap);

    if (!HidD_GetProductString(device_handle.get(), &productString.front(), RawInputDevice::kIdLengthCap))
        return false;

    m_ProductString = utf8::narrow(productString);

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

    m_PPDBuffer.reset(new uint8_t[size]);
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

    QueryButtonCapabilities(caps.NumberInputButtonCaps);
    QueryAxisCapabilities(caps.NumberInputValueCaps);

    return true;
}

void RawInputDeviceHid::QueryButtonCapabilities(uint16_t button_count)
{
    if (button_count > 0)
    {
        std::unique_ptr<HIDP_BUTTON_CAPS[]> button_caps(new HIDP_BUTTON_CAPS[button_count]);

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
    std::unique_ptr<HIDP_VALUE_CAPS[]> axes(new HIDP_VALUE_CAPS[axis_count]);
    HidP_GetValueCaps(HidP_Input, axes.get(), &axis_count, m_PreparsedData);

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

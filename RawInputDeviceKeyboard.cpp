#include "framework.h"

#include "RawInputDeviceKeyboard.h"

#include <winioctl.h>
#include <ntddkbd.h>

#include <string>

RawInputDeviceKeyboard::RawInputDeviceKeyboard(HANDLE handle)
    : RawInputDevice(handle)
{
    m_IsValid = QueryDeviceInfo();
}

RawInputDeviceKeyboard::~RawInputDeviceKeyboard() = default;

void RawInputDeviceKeyboard::OnInput(const RAWINPUT* input)
{

}

bool RawInputDeviceKeyboard::QueryDeviceInfo()
{
    // Fetch the device's |name_| (RIDI_DEVICENAME).
    if (!QueryRawDeviceName())
        return false;

    if (!QueryKeyboardInfo())
        return false;

    auto keyboard_handle = OpenKeyboardHandle();
    if (!IsValidHandle(keyboard_handle.get()))
        return false;

    if (!KeyboardSetLeds(keyboard_handle))
        return false;

    // Fetch the human-friendly |m_ProductString|, if available.
    //if (!QueryKeyboardString(keyboard_handle))
    //    m_ProductString = "Unknown HID Device";

    //input |= (leds << 16);
    //if (!DeviceIoControl(kbd, IOCTL_KEYBOARD_SET_INDICATORS, &input, sizeof(input), NULL, 0, &len, NULL))
    //{
    //    printf("Error writing to LEDs!\n");
    //}

    return true;
}

ScopedHandle RawInputDeviceKeyboard::OpenKeyboardHandle() const
{
    // keyboard is write-only device
    return ScopedHandle(::CreateFile(fromUtf8(m_Name).c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
}

bool RawInputDeviceKeyboard::QueryKeyboardInfo()
{
    RID_DEVICE_INFO device_info;

    if (!QueryRawDeviceInfo(m_Handle, &device_info))
        return false;

    //DCHECK_EQ(device_info.dwType, static_cast<DWORD>(RIM_TYPEKEYBOARD));

    std::memcpy(&m_KeyboardInfo, &device_info.keyboard, sizeof(m_KeyboardInfo));

    return true;
}

bool RawInputDeviceKeyboard::KeyboardSetLeds(ScopedHandle& hid_handle)
{
    KEYBOARD_INDICATOR_PARAMETERS indicator_parameters;
    indicator_parameters.UnitId = 0;
    indicator_parameters.LedFlags = KEYBOARD_SCROLL_LOCK_ON | KEYBOARD_NUM_LOCK_ON | KEYBOARD_CAPS_LOCK_ON;

    DWORD len;
    if (!DeviceIoControl(hid_handle.get(), IOCTL_KEYBOARD_SET_INDICATORS, &indicator_parameters, sizeof(indicator_parameters), nullptr, 0, &len, nullptr))
    {
        auto error = GetLastError();
        return false;
    }

    return true;
}

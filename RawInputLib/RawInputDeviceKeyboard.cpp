#include "pch.h"
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

void RawInputDeviceKeyboard::OnInput(const RAWINPUT* /*input*/)
{
    //TODO
}

bool RawInputDeviceKeyboard::QueryDeviceInfo()
{
    // Fetch the device's |name_| (RIDI_DEVICENAME).
    if (!QueryRawDeviceName())
        return false;

    if (!QueryKeyboardInfo())
        return false;

    auto keyboard_handle = OpenKeyboardDevice();
    if (!IsValidHandle(keyboard_handle.get()))
        return false;

    //if (!KeyboardSetLeds(keyboard_handle))
    //   return false;

    // Fetch the human-friendly |m_ProductString|, if available.
    if (!QueryProductString())
        m_ProductString = "Unknown Keyboard";

    //input |= (leds << 16);
    //if (!DeviceIoControl(kbd, IOCTL_KEYBOARD_SET_INDICATORS, &input, sizeof(input), NULL, 0, &len, NULL))
    //{
    //    printf("Error writing to LEDs!\n");
    //}

    return true;
}

ScopedHandle RawInputDeviceKeyboard::OpenKeyboardDevice() const
{
    // Keyboard is write-only device
    return ScopedHandle(::CreateFile(
        utf8::widen(m_Name).c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, /*lpSecurityAttributes=*/nullptr,
        OPEN_EXISTING, /*dwFlagsAndAttributes=*/0, /*hTemplateFile=*/nullptr));
}

bool RawInputDeviceKeyboard::QueryKeyboardInfo()
{
    RID_DEVICE_INFO device_info;

    if (!QueryRawDeviceInfo(m_Handle, &device_info))
        return false;

    DCHECK_EQ(device_info.dwType, static_cast<DWORD>(RIM_TYPEKEYBOARD));

    std::memcpy(&m_KeyboardInfo, &device_info.keyboard, sizeof(m_KeyboardInfo));

    return true;
}

bool RawInputDeviceKeyboard::QueryProductString()
{
    if (m_KeyboardInfo.dwNumberOfKeysTotal == 0)
        return false;

    m_ProductString = fmt::format("Keyboard ({}-key)", m_KeyboardInfo.dwNumberOfKeysTotal);

    return true;
}

bool RawInputDeviceKeyboard::KeyboardSetLeds(ScopedHandle& keyboard_handle)
{
    DCHECK(IsValidHandle(keyboard_handle.get()));

    KEYBOARD_INDICATOR_PARAMETERS indicator_parameters;
    indicator_parameters.UnitId = 0;
    indicator_parameters.LedFlags = KEYBOARD_SCROLL_LOCK_ON | KEYBOARD_NUM_LOCK_ON | KEYBOARD_CAPS_LOCK_ON;

    DWORD len;
    if (!DeviceIoControl(keyboard_handle.get(), IOCTL_KEYBOARD_SET_INDICATORS, &indicator_parameters, sizeof(indicator_parameters), nullptr, 0, &len, nullptr))
    {
        //auto error = GetLastError();
        return false;
    }

    return true;
}

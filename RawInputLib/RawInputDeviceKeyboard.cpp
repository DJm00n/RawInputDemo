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

void RawInputDeviceKeyboard::OnInput(const RAWINPUT* input)
{
    if (input == nullptr || input->header.dwType != RIM_TYPEKEYBOARD)
    {
        DBGPRINT("Wrong keyboard input.");
        return;
    }

    const RAWKEYBOARD& rawKeyboard = input->data.keyboard;

    const bool keyPressed = (rawKeyboard.Flags & RI_KEY_BREAK) == RI_KEY_MAKE;
    const bool keyExtended = (rawKeyboard.Flags & RI_KEY_E0) == RI_KEY_E0;

    // update pressed state
    m_KeyState[rawKeyboard.VKey] = keyPressed;

    auto mapLeftRightKeys = [](uint8_t vk, uint8_t scanCode, bool e0) -> uint8_t
    {
        switch(vk)
        {
    case VK_SHIFT:
        return ::MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX);
    case VK_CONTROL:
        return e0 ? VK_RCONTROL : VK_LCONTROL;
    case VK_MENU:
        return e0 ? VK_RMENU : VK_LMENU;
    default:
        return vk;
        }
    };

    uint8_t vk = mapLeftRightKeys(rawKeyboard.VKey, rawKeyboard.MakeCode, keyExtended);

    // update extended VK key state
    if(vk != rawKeyboard.VKey)
        m_KeyState[vk] = keyPressed;

    if (!keyPressed)
        return;

    // 16 wchar buffer as in xxxTranslateMessage in WinXP source code
    std::array<wchar_t, 16> uniChars;

    const HKL keyboardLayout = GetKeyboardLayout(0);
    const UINT scanCode = rawKeyboard.MakeCode & (16 << (1 & keyPressed));

    int ret = ::ToUnicodeEx(vk,
        scanCode,
        m_KeyState.data(),
        uniChars.data(),
        static_cast<int>(uniChars.size()),
        0,
        keyboardLayout);

    // nothing to print
    if (!ret || ret < 0)
        return;

    for (int i = 0; i < ret; ++i)
    {
        std::string utf8char = utf8::narrow(&uniChars[i], 1);
            DBGPRINT("Keyboard Char=%s\n", utf8char.c_str());
    }
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

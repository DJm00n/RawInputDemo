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
    m_KeyState[rawKeyboard.VKey] = keyPressed ? 0x80 : 0x00;

    auto mapLeftRightKeys = [](USHORT vk, USHORT scanCode, bool e0) -> USHORT
    {
        switch (vk)
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

    USHORT vk = mapLeftRightKeys(rawKeyboard.VKey, rawKeyboard.MakeCode, keyExtended);

    // update extended VK key state
    if(vk != rawKeyboard.VKey)
        m_KeyState[vk] = keyPressed ? 0x80 : 0x00;
    
    // not pressed or repeat press
    if (!keyPressed || m_KeyState[rawKeyboard.VKey])
        return;

    // 16 wchar buffer as in xxxTranslateMessage in WinXP source code
    std::array<wchar_t, 16> uniChars;

    const HKL keyboardLayout = GetKeyboardLayout(0);
    const UINT scanCode = rawKeyboard.MakeCode & (16 << (rawKeyboard.Flags & RI_KEY_BREAK));

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
    if (!RawInputDevice::QueryDeviceInfo())
        return false;

    if (!QueryKeyboardInfo())
        return false;

    auto keyboard_handle = OpenKeyboardDevice();
    if (!IsValidHandle(keyboard_handle.get()))
        return false;

    //if (!KeyboardSetLeds(keyboard_handle))
    //   return false;

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
        utf8::widen(m_DeviceInterfaceName).c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, /*lpSecurityAttributes=*/nullptr,
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

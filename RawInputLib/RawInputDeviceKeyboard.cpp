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

    DBGPRINT("New Keyboard device[VID:%04X,PID:%04X]: '%s', Interface: `%s`, HID: %d", GetVendorId(), GetProductId(), GetProductString().c_str(), GetInterfacePath().c_str(), IsHidDevice());
}

RawInputDeviceKeyboard::~RawInputDeviceKeyboard()
{
    DBGPRINT("Removed Keyboard device: '%s', Interface: `%s`", GetProductString().c_str(), GetInterfacePath().c_str());
}

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
        //DBGPRINT("Keyboard Char=%s\n", utf8char.c_str());
    }
}

bool RawInputDeviceKeyboard::QueryDeviceInfo()
{
    if (!RawInputDevice::QueryDeviceInfo())
        return false;

    if (!m_KeyboardInfo.QueryInfo(m_Handle))
    {
        DBGPRINT("Cannot get Raw Input Keyboard info from: %s", m_InterfacePath.c_str());
        return false;
    }

    // Seems only HID keyboard does support this
    if (IsHidDevice() && !m_ExtendedKeyboardInfo.QueryInfo(m_InterfaceHandle))
    {
        DBGPRINT("Cannot get Extended Keyboard info from: %s", m_InterfacePath.c_str());
        return false;

    }

    return true;
}

bool RawInputDeviceKeyboard::KeyboardInfo::QueryInfo(HANDLE handle)
{
    // https://docs.microsoft.com/windows/win32/api/ntddkbd/ns-ntddkbd-keyboard_attributes
    // https://docs.microsoft.com/windows/win32/api/winuser/ns-winuser-rid_device_info_keyboard

    RID_DEVICE_INFO device_info;

    if (!RawInputDevice::QueryRawDeviceInfo(handle, &device_info))
        return false;

    DCHECK_EQ(device_info.dwType, static_cast<DWORD>(RIM_TYPEKEYBOARD));

    const RID_DEVICE_INFO_KEYBOARD &keyboardInfo = device_info.keyboard;

    Type = static_cast<uint16_t>(keyboardInfo.dwType);
    SubType = static_cast<uint16_t>(keyboardInfo.dwSubType);
    KeyboardMode = static_cast<uint8_t>(keyboardInfo.dwKeyboardMode);
    NumberOfFunctionKeys = static_cast<uint16_t>(keyboardInfo.dwNumberOfFunctionKeys);
    NumberOfIndicators = static_cast<uint16_t>(keyboardInfo.dwNumberOfIndicators);
    NumberOfKeysTotal = static_cast<uint16_t>(keyboardInfo.dwNumberOfKeysTotal);

    return true;
}

bool RawInputDeviceKeyboard::ExtendedKeyboardInfo::QueryInfo(const ScopedHandle& interfaceHandle)
{
    // https://docs.microsoft.com/windows/win32/api/ntddkbd/ns-ntddkbd-keyboard_extended_attributes

    KEYBOARD_EXTENDED_ATTRIBUTES extended_attributes{ KEYBOARD_EXTENDED_ATTRIBUTES_STRUCT_VERSION_1 };
    DWORD len = 0;

    if (!DeviceIoControl(interfaceHandle.get(), IOCTL_KEYBOARD_QUERY_EXTENDED_ATTRIBUTES, nullptr, 0, &extended_attributes, sizeof(extended_attributes), &len, nullptr))
        return false;

    DCHECK_EQ(len, sizeof(extended_attributes));

    FormFactor = extended_attributes.FormFactor;
    KeyType = extended_attributes.IETFLanguageTagIndex;
    PhysicalLayout = extended_attributes.PhysicalLayout;
    VendorSpecificPhysicalLayout = extended_attributes.VendorSpecificPhysicalLayout;
    IETFLanguageTagIndex = extended_attributes.IETFLanguageTagIndex;
    ImplementedInputAssistControls = extended_attributes.ImplementedInputAssistControls;

    return true;
}

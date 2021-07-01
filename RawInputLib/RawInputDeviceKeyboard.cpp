#include "pch.h"
#include "framework.h"

#include "RawInputDeviceKeyboard.h"

#pragma warning(push, 0)
#include <winioctl.h>
#include <ntddkbd.h>
#pragma warning(pop)

#include <string>

#include "keycodeconverter.h"

RawInputDeviceKeyboard::RawInputDeviceKeyboard(HANDLE handle)
    : RawInputDevice(handle)
{
    m_IsValid = QueryDeviceInfo();

    DBGPRINT("New Keyboard device: '%s', Interface: `%s`", GetProductString().c_str(), GetInterfacePath().c_str());
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

    const RAWKEYBOARD& keyboard = input->data.keyboard;

    bool keyBreak = (keyboard.Flags & RI_KEY_BREAK) == RI_KEY_BREAK;

    uint16_t nativeKeyCode = keyboard.MakeCode;
    if ((keyboard.Flags & RI_KEY_E0) == RI_KEY_E0)
        nativeKeyCode |= (0xe0 << 8);
    else if ((keyboard.Flags & RI_KEY_E1) == RI_KEY_E1)
        nativeKeyCode |= (0xe1 << 8);

    uint32_t usbKeyCode = KeycodeConverter::NativeKeycodeToUsbKeycode(nativeKeyCode);

    DBGPRINT("Keyboard %s '%s'\n", keyBreak ? "press" : "release", KeycodeConverter::UsbKeycodeToCodeString(usbKeyCode));

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

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

    //DBGPRINT("New Keyboard device: '%s', Interface: `%s`", GetProductString().c_str(), GetInterfacePath().c_str());
}

RawInputDeviceKeyboard::~RawInputDeviceKeyboard()
{
    //DBGPRINT("Removed Keyboard device: '%s', Interface: `%s`", GetProductString().c_str(), GetInterfacePath().c_str());
}

// Get keyboard layout specific localized key name
static std::string GetScanCodeName(uint16_t scanCode)
{
    const bool isExtendedKey = (scanCode >> 8) != 0;

    // Some extended keys doesn't work properly with GetKeyNameTextW API
    if (isExtendedKey)
    {
        const uint16_t vkCode = LOWORD(MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX));
        switch (vkCode)
        {
        case VK_BROWSER_BACK:
            return "Browser Back";
        case VK_BROWSER_FORWARD:
            return "Browser Forward";
        case VK_BROWSER_REFRESH:
            return "Browser Refresh";
        case VK_BROWSER_STOP:
            return "Browser Stop";
        case VK_BROWSER_SEARCH:
            return "Browser Search";
        case VK_BROWSER_FAVORITES:
            return "Browser Favorites";
        case VK_BROWSER_HOME:
            return "Browser Home";
        case VK_VOLUME_MUTE:
            return "Volume Mute";
        case VK_VOLUME_DOWN:
            return "Volume Down";
        case VK_VOLUME_UP:
            return "Volume Up";
        case VK_MEDIA_NEXT_TRACK:
            return "Next Track";
        case VK_MEDIA_PREV_TRACK:
            return "Previous Track";
        case VK_MEDIA_STOP:
            return "Media Stop";
        case VK_MEDIA_PLAY_PAUSE:
            return "Media Play/Pause";
        case VK_LAUNCH_MAIL:
            return "Launch Mail";
        case VK_LAUNCH_MEDIA_SELECT:
            return "Launch Media Selector";
        case VK_LAUNCH_APP1:
            return "Launch App 1";
        case VK_LAUNCH_APP2:
            return "Launch App 2";
        }
    }

    const LONG lParam = MAKELONG(0, (isExtendedKey ? KF_EXTENDED : 0) | (scanCode & 0xff));
    wchar_t name[128] = {};
    size_t nameSize = ::GetKeyNameTextW(lParam, name, sizeof(name));

    return utf8::narrow(name, nameSize);
}

void RawInputDeviceKeyboard::OnInput(const RAWINPUT* input)
{
    if (input == nullptr || input->header.dwType != RIM_TYPEKEYBOARD)
    {
        DBGPRINT("Wrong keyboard input.");
        return;
    }

    const RAWKEYBOARD& keyboard = input->data.keyboard;

    const bool keyUp = (keyboard.Flags & RI_KEY_BREAK) == RI_KEY_BREAK;
    const bool keyHasE0Prefix = (keyboard.Flags & RI_KEY_E0) == RI_KEY_E0;
    const bool keyHasE1Prefix = (keyboard.Flags & RI_KEY_E1) == RI_KEY_E1;

    uint16_t vkCode = keyboard.VKey;
    uint16_t scanCode = keyboard.MakeCode;

    scanCode |= keyHasE0Prefix ? 0xe000 : 0;
    scanCode |= keyHasE1Prefix ? 0xe100 : 0;

    if (scanCode == KEYBOARD_OVERRUN_MAKE_CODE || vkCode == 0xff)
        return;

    switch (vkCode)
    {
    case VK_SHIFT:   // -> VK_LSHIFT or VK_RSHIFT
    case VK_CONTROL: // -> VK_LCONTROL or VK_RCONTROL
    case VK_MENU:    // -> VK_LMENU or VK_RMENU
        vkCode = LOWORD(MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX));
        break;
    }

    constexpr uint16_t ctrlNumLockScanCode = 0xe11d;
    constexpr uint16_t numLockScanCode = 0xe045;
    constexpr uint16_t pauseScanCode = 0x0045;

    // Some scancodes are known to be wrong (obscure Windows bug?)
    switch (scanCode)
    {
    case ctrlNumLockScanCode:
        scanCode = pauseScanCode;
        break;
    case pauseScanCode:
        scanCode = numLockScanCode;
        break;
    }

    uint32_t usbKeyCode = KeycodeConverter::NativeKeycodeToUsbKeycode(scanCode);
    const char* keyName = KeycodeConverter::UsbKeycodeToCodeString(usbKeyCode);
    std::string scanCodeName = GetScanCodeName(scanCode);

    DBGPRINT("Keyboard '%s': %s `%s` Usage(%04x: %04x), ScanCode(0x%04x), VirtualKeyCode(0x%02x), ScanCodeName(`%s`)\n",
        GetInterfacePath().c_str(),
        keyUp ? "release" : "press",
        keyName,
        HIWORD(usbKeyCode),
        LOWORD(usbKeyCode),
        scanCode,
        vkCode,
        scanCodeName.c_str());
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

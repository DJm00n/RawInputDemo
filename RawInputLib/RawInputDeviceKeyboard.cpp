#include "pch.h"
#include "framework.h"

#include "RawInputDeviceKeyboard.h"
#include "printvk.h"

#pragma warning(push, 0)
#include <winioctl.h>
#include <ntddkbd.h>
#pragma warning(pop)

#include <string>

#define DIRECTINPUT_VERSION 0x800
#include <dinput.h>
#include <codecvt>

namespace DirectInput
{
    static LPDIRECTINPUT8 directInput8 = nullptr;
    static LPDIRECTINPUTDEVICE8 directInputKeyboard = nullptr;

    void Init()
    {
        if (!directInputKeyboard)
        {
            ::DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)&directInput8, NULL);
            CHECK(directInput8);

            directInput8->CreateDevice(GUID_SysKeyboard, &directInputKeyboard, NULL);
            CHECK(directInputKeyboard);

            directInputKeyboard->SetDataFormat(&c_dfDIKeyboard);
        }
    }

    // DIK_* codes are almost same thing as scancode
    // but packed into one byte with high-order bit set for extended keys
    static uint8_t ScanCodeToDIKCode(uint16_t scanCode)
    {
        // Only key down (make) scan codes are supported
        DCHECK_EQ(scanCode & 0x80, 0);

        uint8_t dikCode = scanCode & 0x7f;
        dikCode |= (scanCode & 0xe000) ? 0x80 : 0;

        // Silly keyboard driver - as said in DirectInput source code :)
        if (dikCode == DIK_NUMLOCK)
            dikCode = DIK_PAUSE;
        else if (dikCode == DIK_PAUSE)
            dikCode = DIK_NUMLOCK;

        return dikCode;
    }

    // Unpack DIK_* code to two-byte scan code
    static uint16_t DIKCodeToScanCode(uint8_t dikCode)
    {
        // Silly keyboard driver - as said in DirectInput source code :)
        if (dikCode == DIK_NUMLOCK)
            dikCode = DIK_PAUSE;
        else if (dikCode == DIK_PAUSE)
            dikCode = DIK_NUMLOCK;

        uint16_t scanCode = dikCode & 0x7f;
        scanCode |= (dikCode & 0x80) ? 0xe000 : 0;

        return scanCode;
    }

    // Get keyboard layout specific localized DIK_* key name
    static std::string DIKCodeToString(uint8_t dikCode)
    {
        Init();

        DIPROPSTRING keyName;
        keyName.diph.dwSize = sizeof(DIPROPSTRING);
        keyName.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        keyName.diph.dwObj = dikCode;
        keyName.diph.dwHow = DIPH_BYOFFSET;
        /*HRESULT res = */directInputKeyboard->GetProperty(DIPROP_KEYNAME, &keyName.diph);
        //CHECK(SUCCEEDED(res));

        return utf8::narrow(keyName.wsz);
    }
}

namespace HID
{
    // 256 bytes
    static const uint8_t scan2usage[] =
    {
        0x00, 0x29, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2d, 0x2e, 0x2a, 0x2b,
        0x14, 0x1a, 0x08, 0x15, 0x17, 0x1c, 0x18, 0x0c, 0x12, 0x13, 0x2f, 0x30, 0x28, 0xe0, 0x04, 0x16,
        0x07, 0x09, 0x0a, 0x0b, 0x0d, 0x0e, 0x0f, 0x33, 0x34, 0x35, 0xe1, 0x31, 0x1d, 0x1b, 0x06, 0x19,
        0x05, 0x11, 0x10, 0x36, 0x37, 0x38, 0xe5, 0x55, 0xe2, 0x2c, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e,
        0x3f, 0x40, 0x41, 0x42, 0x43, 0x48, 0x47, 0x5f, 0x60, 0x61, 0x56, 0x5c, 0x5d, 0x5e, 0x57, 0x59,
        0x5a, 0x5b, 0x62, 0x63, 0x00, 0x00, 0x64, 0x44, 0x45, 0x67, 0x00, 0x00, 0x8c, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x00,
        0x88, 0x91, 0x90, 0x87, 0x00, 0x00, 0x73, 0x93, 0x92, 0x8a, 0x00, 0x8b, 0x00, 0x89, 0x85, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x58, 0xe4, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x54, 0x00, 0x46, 0xe6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x53, 0x00, 0x4a, 0x52, 0x4b, 0x00, 0x50, 0x00, 0x4f, 0x00, 0x4d,
        0x51, 0x4e, 0x49, 0x4c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe3, 0xe7, 0x65, 0x66, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
    };

    // 256 bytes
    static const uint8_t usage2scan[] =
    {
        0x00, 0x00, 0x00, 0x00, 0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26,
        0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c, 0x02, 0x03,
        0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x1c, 0x01, 0x0e, 0x0f, 0x39, 0x0c, 0x0d, 0x1a,
        0x1b, 0x2b, 0x2b, 0x27, 0x28, 0x29, 0x33, 0x34, 0x35, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40,
        0x41, 0x42, 0x43, 0x44, 0x57, 0x58, 0xb7, 0x46, 0x45, 0xd2, 0xc7, 0xc9, 0xd3, 0xcf, 0xd1, 0xcd,
        0xcb, 0xd0, 0xc8, 0xc5, 0xb5, 0x37, 0x4a, 0x4e, 0x9c, 0x4f, 0x50, 0x51, 0x4b, 0x4c, 0x4d, 0x47,
        0x48, 0x49, 0x52, 0x53, 0x56, 0xdd, 0xde, 0x59, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b,
        0x6c, 0x6d, 0x6e, 0x76, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x00, 0x73, 0x70, 0x7d, 0x79, 0x7b, 0x5c, 0x00, 0x00, 0x00,
        0x72, 0x71, 0x78, 0x77, 0x76, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x1d, 0x2a, 0x38, 0xdb, 0x9d, 0x36, 0xb8, 0xdc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    // additional keys
    static const struct ScanCode2HID {
        uint16_t scanCode;
        uint32_t hidUsage;
    } additionalMappings[] = {
        { 0xe010, MAKELONG(HID_USAGE_CONSUMER_SCAN_PREV_TRACK, HID_USAGE_PAGE_CONSUMER) },
        { 0xe019, MAKELONG(HID_USAGE_CONSUMER_SCAN_NEXT_TRACK, HID_USAGE_PAGE_CONSUMER) },
        { 0xe020, MAKELONG(HID_USAGE_CONSUMER_MUTE, HID_USAGE_PAGE_CONSUMER)  },
        { 0xe021, MAKELONG(HID_USAGE_CONSUMER_AL_CALCULATOR, HID_USAGE_PAGE_CONSUMER) },
        { 0xe022, MAKELONG(HID_USAGE_CONSUMER_PLAY_PAUSE, HID_USAGE_PAGE_CONSUMER)},
        { 0xe024, MAKELONG(HID_USAGE_CONSUMER_STOP, HID_USAGE_PAGE_CONSUMER) },
        { 0xe02e, MAKELONG(HID_USAGE_CONSUMER_VOLUME_DECREMENT, HID_USAGE_PAGE_CONSUMER) },
        { 0xe030, MAKELONG(HID_USAGE_CONSUMER_VOLUME_INCREMENT, HID_USAGE_PAGE_CONSUMER) },
        { 0xe032, MAKELONG(HID_USAGE_CONSUMER_AC_HOME, HID_USAGE_PAGE_CONSUMER) },
        { 0xe05e, MAKELONG(HID_USAGE_GENERIC_SYSCTL_POWER, HID_USAGE_PAGE_GENERIC) },
        { 0xe05f, MAKELONG(HID_USAGE_GENERIC_SYSCTL_SLEEP, HID_USAGE_PAGE_GENERIC) },
        { 0xe063, MAKELONG(HID_USAGE_GENERIC_SYSCTL_WAKE, HID_USAGE_PAGE_GENERIC) },
        { 0xe065, MAKELONG(HID_USAGE_CONSUMER_AC_SEARCH, HID_USAGE_PAGE_CONSUMER) },
        { 0xe066, MAKELONG(HID_USAGE_CONSUMER_AC_BOOKMARKS, HID_USAGE_PAGE_CONSUMER) },
        { 0xe067, MAKELONG(HID_USAGE_CONSUMER_AC_REFRESH, HID_USAGE_PAGE_CONSUMER) },
        { 0xe068, MAKELONG(HID_USAGE_CONSUMER_AC_STOP, HID_USAGE_PAGE_CONSUMER) },
        { 0xe069, MAKELONG(HID_USAGE_CONSUMER_AC_FORWARD, HID_USAGE_PAGE_CONSUMER) },
        { 0xe06a, MAKELONG(HID_USAGE_CONSUMER_AC_BACK, HID_USAGE_PAGE_CONSUMER) },
        { 0xe06b, MAKELONG(HID_USAGE_CONSUMER_AL_BROWSER, HID_USAGE_PAGE_CONSUMER) },
        { 0xe06c, MAKELONG(HID_USAGE_CONSUMER_AL_EMAIL, HID_USAGE_PAGE_CONSUMER) },
        { 0xe06d, MAKELONG(HID_USAGE_CONSUMER_AL_CONFIGURATION, HID_USAGE_PAGE_CONSUMER) },
    };

    static uint8_t ScanCodeToHIDUsageKeyboard(uint16_t scanCode)
    {
        uint8_t index = (scanCode & 0xff) < 0x80 ? (scanCode & 0xff) : 0x00;
        if ((scanCode & 0xff00) != 0)
        {
            if ((scanCode & 0xff00) == 0xe000)
                index |= 0x80;
            else
                index = 0;
        }

        return scan2usage[index];
    }

    static uint16_t HIDUsageToScanCodeKeyboard(uint8_t usage)
    {
        uint8_t index = usage;

        return ((usage2scan[index] & 0x80) ? 0xe000 : 0) | (usage2scan[index] & 0x7f);
    }

    static uint32_t ScanCodeToHIDUsage(uint16_t scanCode)
    {
        uint32_t usage = ScanCodeToHIDUsageKeyboard(scanCode);
        if (usage != 0)
            return MAKELONG(usage, HID_USAGE_PAGE_KEYBOARD);

        auto it = std::find_if(std::begin(additionalMappings), std::end(additionalMappings), [scanCode](const ScanCode2HID& mapping) { return mapping.scanCode == scanCode; });
        if (it != std::end(additionalMappings))
            return it->hidUsage;

        return 0;
    }

    static uint16_t HIDUsageToScanCode(uint32_t usage)
    {
        if (HIWORD(usage) == HID_USAGE_PAGE_KEYBOARD)
            return HIDUsageToScanCodeKeyboard(LOBYTE(usage));

        auto it = std::find_if(std::begin(additionalMappings), std::end(additionalMappings), [usage](const ScanCode2HID& mapping) { return mapping.hidUsage == usage; });
        if (it != std::end(additionalMappings))
            return it->scanCode;

        return 0;
    }
}

RawInputDeviceKeyboard::RawInputDeviceKeyboard(HANDLE handle)
    : RawInputDevice(handle)
    , m_CurrentHKL(GetKeyboardLayout(0))
{
    if (handle == NULL)
    {
        m_InterfacePath = "Default Mouse";
        m_Identity.product = "Default Keyboard";
        m_IsValid = true; // Virtual keyboard device without a real handle.
        return;
    }

    m_IsValid = QueryDeviceInfo();

    //DBGPRINT("New Keyboard device: '%s', Interface: `%s`", GetProductString().c_str(), GetInterfacePath().c_str());

}

RawInputDeviceKeyboard::~RawInputDeviceKeyboard()
{
    //DBGPRINT("Removed Keyboard device: '%s', Interface: `%s`", GetProductString().c_str(), GetInterfacePath().c_str());
}

void RawInputDeviceKeyboard::OnInput(const RAWINPUT* input)
{
    if (input == nullptr || input->header.dwType != RIM_TYPEKEYBOARD)
    {
        DBGPRINT("Wrong keyboard input.");
        return;
    }

    const RAWKEYBOARD& keyboard = input->data.keyboard;

    // Ignore key overrun state
    if (keyboard.MakeCode == KEYBOARD_OVERRUN_MAKE_CODE)
        return;

    // Ignore keys not mapped to any VK code
    // This effectively filters out scan code pre/postfix for some keys like PrintScreen.
    if (keyboard.VKey >= 0xff/*VK__none_*/)
        return;

    WORD scanCode = keyboard.MakeCode;
    BOOL keyUp = keyboard.Flags & RI_KEY_BREAK;
    if (scanCode != 0)
    {
        // Windows `On-Screen Keyboard` tool can send wrong
        // scan codes with high-order bit set (key break code).
        // Strip it and add extended scan code value.
        scanCode = MAKEWORD(scanCode & 0x7f, ((keyboard.Flags & RI_KEY_E0) ? 0xe0 : ((keyboard.Flags & RI_KEY_E1) ? 0xe1 : 0x00)));
    }
    else
    {
        // Windows may not report scan codes for some buttons (like multimedia buttons).
        // Map them manually from VK code.
        scanCode = LOWORD(MapVirtualKeyW(keyboard.VKey, MAPVK_VK_TO_VSC_EX));
    }

    CHECK_NE(scanCode, 0);

    // These keys are special for historical reasons
    switch (scanCode)
    {
    case 0xe046:            // Break (Ctrl + Pause)
    case 0xe11d:            // Pause (Ctrl + NumLock)
        scanCode = 0x0045;  // -> Pause
        break;
    case 0x0045:            // Pause
        scanCode = 0xe045;  // -> NumLock
        break;
    case 0x0054:            // SysReq (Alt + PrntScrn)
        scanCode = 0xe037;  // -> PrntScrn
        break;
    }

    uint32_t usbKeyCode = HID::ScanCodeToHIDUsage(scanCode);
    std::string scanCodeName = GetScanCodeName(scanCode);
    BYTE dikCode = DirectInput::ScanCodeToDIKCode(scanCode);
    std::string dikCodeName = DirectInput::DIKCodeToString(dikCode);

    CHECK_EQ(scanCode, HID::HIDUsageToScanCode(usbKeyCode));
    CHECK_EQ(scanCode, DirectInput::DIKCodeToScanCode(dikCode));

    // VK code example:
    uint16_t vkCode = keyboard.VKey;
    switch (vkCode)
    {
    case VK_SHIFT:   // -> VK_LSHIFT or VK_RSHIFT
    case VK_CONTROL: // -> VK_LCONTROL or VK_RCONTROL
    case VK_MENU:    // -> VK_LMENU or VK_RMENU
        vkCode = LOWORD(MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX));
        break;
    }
    std::string vkCodeName = VkToString(vkCode);

    DBGPRINT("Keyboard '%s': %s Usage(%04x: %04x), ScanCode(0x%04x), VirtualKeyCode(%s), ScanCodeName(`%s`), DIKCode(0x%02x), DIKCodeName(`%s`)\n",
        GetInterfacePath().c_str(),
        keyUp ? "release" : "press",
        HIWORD(usbKeyCode),
        LOWORD(usbKeyCode),
        scanCode,
        vkCodeName.c_str(),
        scanCodeName.c_str(),
        dikCode,
        dikCodeName.c_str());

    OnInputPrivate(keyboard);
}

void RawInputDeviceKeyboard::OnInputPrivate(const RAWKEYBOARD& keyboard)
{
    const bool isKeyDown = !(keyboard.Flags & RI_KEY_BREAK);
    const bool isE0 = (keyboard.Flags & RI_KEY_E0) != 0;

    // Update key state before ToUnicodeEx so modifier state is current
    if (isKeyDown) m_KeyState[keyboard.VKey] |= 0x80;
    else           m_KeyState[keyboard.VKey] &= ~0x80;

    // Update extended key (e.g. VK_LSHIFT/VK_RSHIFT from VK_SHIFT)
    if (UINT vkEx = ::MapVirtualKeyW(MAKEWORD(keyboard.MakeCode, isE0 ? 0xe0 : 0x00), MAPVK_VSC_TO_VK_EX))
    {
        if (isKeyDown) m_KeyState[vkEx] |= 0x80;
        else           m_KeyState[vkEx] &= ~0x80;
    }

    // Update toggle state for lock keys
    switch (keyboard.VKey)
    {
    case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL:
        if (isKeyDown) m_KeyState[keyboard.VKey] ^= 0x01;
        break;
    }

    // Only translate key-down events to characters — key-up produces no characters
    // and would corrupt dead key state inside ToUnicodeEx.
    // Alt+Numpad sequences are not supported (require UI thread keyboard state).
    if (!isKeyDown)
        return;

    const UINT scanCode = keyboard.MakeCode | (isE0 ? 0x100 : 0);

    wchar_t buf[16] = {};
    const int result = ::ToUnicodeEx(
        keyboard.VKey, scanCode, m_KeyState.data(),
        buf, static_cast<int>(std::size(buf)),
        0x4,  // don't modify dead key state — dead key composition not supported
        m_CurrentHKL);

    // result > 0: regular character(s)
    // result < 0: dead key
    const int len = std::abs(result);
    for (int i = 0; i < len; )
    {
        wchar_t wc = buf[i++];
        if (IS_HIGH_SURROGATE(wc)) { m_PendingSurrogate = wc; continue; }

        char32_t cp;
        if (IS_LOW_SURROGATE(wc) && m_PendingSurrogate)
        {
            cp = 0x10000u
                + ((static_cast<char32_t>(m_PendingSurrogate) - 0xD800u) << 10)
                + (static_cast<char32_t>(wc) - 0xDC00u);
        }
        else
        {
            cp = static_cast<char32_t>(wc);
        }
        m_PendingSurrogate = 0;

        if (result > 0)
        {
            std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> utf32conv;
            std::string utf8ch = utf32conv.to_bytes(cp);
            DBGPRINT("OnCharacter: %s\n", GetUnicodeCharacterNames(utf8ch).c_str());
        }
    }
}

void RawInputDeviceKeyboard::OnInputLanguageChanged(HKL hkl)
{
    // Clear pending surrogate on language change to avoid confusion
	m_PendingSurrogate = 0;
    m_CurrentHKL = hkl;
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
    if (IsHidDevice())
    {
        ScopedHandle hidHandle = OpenDeviceInterface(m_InterfacePath, m_IsInterfaceReadOnly);
        if (!hidHandle || !m_ExtendedKeyboardInfo.QueryInfo(hidHandle))
        {
            DBGPRINT("Cannot get Extended Keyboard info from: %s", m_InterfacePath.c_str());
            return false;
        }

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

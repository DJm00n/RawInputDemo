#include "pch.h"
#include "RawInputDeviceKeyboardDefault.h"

#include <codecvt>


RawInputDeviceKeyboardDefault::RawInputDeviceKeyboardDefault()
: RawInputDeviceKeyboard(NULL)
, m_CurrentHKL(GetKeyboardLayout(0))
{
}

bool RawInputDeviceKeyboardDefault::Initialize()
{
    m_InterfacePath = "Default Keyboard";
    m_Identity.product = "Default Keyboard";

    return true;
}

void RawInputDeviceKeyboardDefault::OnInput(const RAWINPUT* input)
{
	RawInputDeviceKeyboard::OnInput(input);

    const RAWKEYBOARD& keyboard = input->data.keyboard;

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
            DBGPRINT("Keyboard '%s': OnCharacter: %s\n",
                GetInterfacePath().c_str(),
                GetUnicodeCharacterNames(utf8ch).c_str());
        }
    }
}

void RawInputDeviceKeyboardDefault::OnInputLanguageChanged(HKL hkl)
{
	// Clear pending surrogate on language change to avoid confusion
	m_PendingSurrogate = 0;
	m_CurrentHKL = hkl;
}


#include "pch.h"

#include "framework.h"

#include "utils.h"

#include <cwctype>
#include <codecvt>

using namespace std;

// code from https://www.codeproject.com/Articles/5252037/Doing-UTF-8-in-Windows
namespace utf8
{

    /*!
      Conversion from wide character to UTF-8
      \param  s   input string
      \param  nch number of character to convert or 0 if string is null-terminated
      \return UTF-8 character string
    */
    std::string narrow(const wchar_t* s, size_t nch)
    {
        if (!s || *s == L'\0')
            return string();

        int nsz = WideCharToMultiByte(CP_UTF8, 0, s, (nch ? (int)nch : -1), 0, 0, 0, 0);
        if (!nsz)
            return string();

        string out(nsz, 0);
        WideCharToMultiByte(CP_UTF8, 0, s, (nch ? (int)nch : -1), &out[0], nsz, 0, 0);
        if (!nch)
            out.resize((size_t)nsz - 1); //output is null-terminated
        return out;
    }

    /*!
      Conversion from wide character to UTF-8
      \param  s input string
      \return UTF-8 character string
    */
    std::string narrow(const std::wstring& s)
    {
        int nsz = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, 0, 0, 0, 0);
        if (!nsz)
            return string();

        string out(nsz, 0);
        WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, &out[0], nsz, 0, 0);
        out.resize((size_t)nsz - 1); //output is null-terminated
        return out;
    }

    /*!
      Conversion from UTF-8 to wide character
      \param  s input string
      \param nch number of characters to convert or 0 if string is null-terminated
      \return wide character string
    */
    std::wstring widen(const char* s, size_t nch)
    {
        if (!s || *s == '\0')
            return wstring();

        int wsz = MultiByteToWideChar(CP_UTF8, 0, s, (nch ? (int)nch : -1), 0, 0);
        if (!wsz)
            return wstring();

        wstring out(wsz, 0);
        MultiByteToWideChar(CP_UTF8, 0, s, (nch ? (int)nch : -1), &out[0], wsz);
        if (!nch)
            out.resize((size_t)wsz - 1); //output is null-terminated
        return out;
    }

    /*!
      Conversion from UTF-8 to wide character
      \param  s input string
      \return wide character string
    */
    std::wstring widen(const std::string& s)
    {
        int wsz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, 0, 0);
        if (!wsz)
            return wstring();

        wstring out(wsz, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], wsz);
        out.resize((size_t)wsz - 1); //output is null-terminated
        return out;
    }
}

namespace stringutils
{
    std::vector<std::string> split(const std::string& s, char separator)
    {
        std::vector<std::string> output;

        std::string::size_type prev_pos = 0, pos = 0;

        while ((pos = s.find(separator, pos)) != std::string::npos)
        {
            std::string substring(s.substr(prev_pos, pos - prev_pos));

            output.push_back(substring);

            prev_pos = ++pos;
        }

        output.push_back(s.substr(prev_pos, pos - prev_pos)); // Last word

        return output;
    }
}

// Undocumented API that is used in Windows "Character Map" tool
std::string GetUNameWrapper(char32_t codePoint)
{
    if (codePoint > 0xFFFF)
    {
        return "Supplementary Multilingual Plane";
    }

    // https://github.com/reactos/reactos/tree/master/dll/win32/getuname
    typedef int(WINAPI* GetUNameFunc)(WORD wCharCode, LPWSTR lpBuf);
    static GetUNameFunc pfnGetUName = reinterpret_cast<GetUNameFunc>(::GetProcAddress(::LoadLibraryA("getuname.dll"), "GetUName"));

    if (!pfnGetUName)
        return {};

    const WORD character = static_cast<WORD>(codePoint);
    std::array<WCHAR, 256> buffer;
    int length = pfnGetUName(character, buffer.data());

    return utf8::narrow(buffer.data(), length);
}

// u_charName() ICU API that comes with Windows since Fall Creators Update (Version 1709 Build 16299)
// https://docs.microsoft.com/windows/win32/intl/international-components-for-unicode--icu-
std::string GetUCharNameWrapper(char32_t codePoint)
{
    // https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/uchar_8h.html#a2d90141097af5ad4b6c37189e7984932
    typedef int32_t(*u_charNameFunc)(char32_t code, int nameChoice, char* buffer, int32_t bufferLength, int* pErrorCode);
    static u_charNameFunc pfnU_charName = reinterpret_cast<u_charNameFunc>(::GetProcAddress(::LoadLibraryA("icuuc.dll"), "u_charName"));

    if (!pfnU_charName)
        return {};

    int errorCode = 0;
    std::array<char, 512> buffer;
    int32_t length = pfnU_charName(codePoint, 2/*U_EXTENDED_CHAR_NAME*/ , buffer.data(), static_cast<int32_t>(buffer.size() - 1), &errorCode);

    if (errorCode != 0)
        return {};

    return std::string(buffer.data(), length);
}

// Replace invisible code point with code point that is visible
char32_t ReplaceInvisible(char32_t codePoint)
{
    if (codePoint < 0xFFFF && !std::iswgraph(static_cast<wchar_t>(codePoint)))
    {
        if (codePoint <= 0x21)
            codePoint += 0x2400; // U+2400 Control Pictures https://www.unicode.org/charts/PDF/U2400.pdf
        else
            codePoint = 0xFFFD; // U+308 Replacement Character
    }

    return codePoint;
}

std::string GetUnicodeCharacterNames(std::string string)
{
    // UTF-8 <=> UTF-32 converter
    std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> utf32conv;

    // UTF-8 => UTF-32
    std::u32string utf32string = utf32conv.from_bytes(string);

    std::string characterNames;
    characterNames.reserve(35 * utf32string.size());

    for (const char32_t& codePoint : utf32string)
    {
        if (!characterNames.empty())
            characterNames.append(", ");

        char32_t visibleCodePoint = ReplaceInvisible(codePoint);
        std::string charName = GetUCharNameWrapper(codePoint);

        // UTF-32 => UTF-8
        std::string utf8codePoint = utf32conv.to_bytes(&visibleCodePoint, &visibleCodePoint + 1);
        characterNames.append(fmt::format("{} <U+{:X} {}>", utf8codePoint, static_cast<uint32_t>(codePoint), charName));
    }

    return characterNames;
}

std::wstring GetLayoutProfileId(HKL hkl)
{
    LANGID lang = LOWORD(hkl);
    WCHAR szKLID[KL_NAMELENGTH];
    CHECK(::GetKLIDFromHKL(hkl, szKLID));

    WCHAR layoutProfile[MAX_PATH];
    std::swprintf(layoutProfile, MAX_PATH, L"%04x:%s", lang, szKLID);

    return layoutProfile;
}

std::vector<LAYOUTORTIPPROFILE> EnumLayoutProfiles()
{
    // http://archives.miloush.net/michkap/archive/2008/09/29/8968315.html
    // https://docs.microsoft.com/en-us/windows/win32/tsf/enumenabledlayoutortip
    typedef UINT(WINAPI* EnumEnabledLayoutOrTipFunc)(LPCWSTR pszUserReg, LPCWSTR pszSystemReg, LPCWSTR pszSoftwareReg, LAYOUTORTIPPROFILE* pLayoutOrTipProfile, UINT uBufLength);
    static EnumEnabledLayoutOrTipFunc EnumEnabledLayoutOrTip = reinterpret_cast<EnumEnabledLayoutOrTipFunc>(::GetProcAddress(::LoadLibraryA("input.dll"), "EnumEnabledLayoutOrTip"));

    if (!EnumEnabledLayoutOrTip)
        return {};

    const UINT count = EnumEnabledLayoutOrTip(nullptr, nullptr, nullptr, nullptr, 0);

    std::vector<LAYOUTORTIPPROFILE> layouts;
    layouts.resize(count);

    const UINT written = EnumEnabledLayoutOrTip(nullptr, nullptr, nullptr, layouts.data(), count);

    CHECK_EQ(count, written);

    return layouts;
}

std::wstring GetDefaultLayoutProfileId()
{
    typedef HRESULT(WINAPI* GetDefaultLayoutFunc)(LPCWSTR pszUserReg, LPWSTR pszLayout, LPUINT uBufLength);
    static GetDefaultLayoutFunc GetDefaultLayout = reinterpret_cast<GetDefaultLayoutFunc>(::GetProcAddress(::LoadLibraryA("input.dll"), "GetDefaultLayout"));

    if (!GetDefaultLayout)
        return {};

    UINT length = 0;

    CHECK(SUCCEEDED(GetDefaultLayout(nullptr, nullptr, &length)));

    std::wstring defaultLayoutProfileId;
    defaultLayoutProfileId.resize(length);

    CHECK(SUCCEEDED(GetDefaultLayout(nullptr, defaultLayoutProfileId.data(), &length)));

    return defaultLayoutProfileId;
}

bool GetLayoutProfile(const std::wstring& layoutProfileId, LAYOUTORTIPPROFILE* outProfile)
{
    std::vector<LAYOUTORTIPPROFILE> layouts = EnumLayoutProfiles();
    for (const auto& layout : layouts)
    {
        if (layoutProfileId != layout.szId)
            continue;

        CHECK(layout.dwProfileType & LOTP_KEYBOARDLAYOUT);

        std::memcpy(outProfile, &layout, sizeof(layout));

        return true;
    }

    return false;
}

std::string GetLayoutProfileDescription(const std::wstring& layoutProfileId)
{
    // http://archives.miloush.net/michkap/archive/2008/09/29/8968315.html
    typedef HRESULT(WINAPI* GetLayoutDescriptionFunc)(LPCWSTR szId, LPWSTR pszName, LPUINT uBufLength, DWORD dwFlags);
    static GetLayoutDescriptionFunc GetLayoutDescription = reinterpret_cast<GetLayoutDescriptionFunc>(::GetProcAddress(::LoadLibraryA("input.dll"), "GetLayoutDescription"));

    if (!GetLayoutDescription)
        return {};

    UINT length = 0;

    CHECK(SUCCEEDED(GetLayoutDescription(layoutProfileId.c_str(), nullptr, &length, 0)));

    std::wstring description;
    description.resize(length);

    CHECK(SUCCEEDED(GetLayoutDescription(layoutProfileId.c_str(), description.data(), &length, 0)));

    return utf8::narrow(description);
}

std::string GetLocaleInformation(const std::wstring locale, LCTYPE LCType)
{
    int len = ::GetLocaleInfoEx(locale.c_str(), LCType, nullptr, 0);
    CHECK_GE(len, 1);

    std::unique_ptr<wchar_t[]> buffer(new wchar_t[len]);
    ::GetLocaleInfoEx(locale.c_str(), LCType, buffer.get(), len);

    return utf8::narrow(buffer.get());
}

BOOL GetKLIDFromHKL(HKL hkl, LPWSTR pwszKLID)
{
    /*  HKL is a 32 bit value. HIWORD is a Device Handle. LOWORD is Language ID.
        +-------------- +-------------+
        | Device Handle | Language ID |
        +---------------+-------------+
        31            16 15           0 bit
        http://archives.miloush.net/michkap/archive/2005/04/17/409032.html
    */

    bool succeded = false;

    if ((HIWORD(hkl) & 0xf000) == 0xf000) // `Device Handle` contains `Layout ID`
    {
        WORD layoutId = HIWORD(hkl) & 0x0fff;

        HKEY key;
        CHECK_EQ(::RegOpenKeyW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts", &key), ERROR_SUCCESS);

        DWORD index = 0;
        while (::RegEnumKeyW(key, index, pwszKLID, KL_NAMELENGTH) == ERROR_SUCCESS)
        {
            WCHAR layoutIdBuffer[MAX_PATH] = {};
            DWORD layoutIdBufferSize = sizeof(layoutIdBuffer);
            if (::RegGetValueW(key, pwszKLID, L"Layout Id", RRF_RT_REG_SZ, nullptr, layoutIdBuffer, &layoutIdBufferSize) == ERROR_SUCCESS)
            {
                if (layoutId == std::stoul(layoutIdBuffer, nullptr, 16))
                {
                    succeded = true;
                    //DBGPRINT("Found KLID 0x%ls by layoutId=0x%04x", pwszKLID, layoutId);
                    break;
                }
            }
            ++index;
        }
        CHECK_EQ(::RegCloseKey(key), ERROR_SUCCESS);
    }
    else
    {
        WORD langId = LOWORD(hkl);

        // `Device Handle` contains `Language ID` of keyboard layout if set
        if (HIWORD(hkl) != 0)
            langId = HIWORD(hkl);

        std::swprintf(pwszKLID, KL_NAMELENGTH, L"%08X", langId);
        succeded = true;

        //DBGPRINT("Found KLID 0x%ls by langId=0x%04x", pwszKLID, langId);
    }

    return succeded;
}

std::string GetKeyboardLayoutDisplayName(LPCWSTR pwszKLID)
{
    typedef HRESULT(WINAPI* SHLoadIndirectStringFunc)(PCWSTR pszSource, PWSTR pszOutBuf, UINT cchOutBuf, void** ppvReserved);
    static SHLoadIndirectStringFunc SHLoadIndirectString = reinterpret_cast<SHLoadIndirectStringFunc>(::GetProcAddress(::LoadLibraryA("shlwapi.dll"), "SHLoadIndirectString"));

    HKEY key;
    CHECK_EQ(::RegOpenKeyW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts", &key), ERROR_SUCCESS);

    WCHAR layoutName[MAX_PATH] = {};
    DWORD layoutNameSize = sizeof(layoutName);
    // http://archives.miloush.net/michkap/archive/2006/05/06/591174.html
    if (::RegGetValueW(key, pwszKLID, L"Layout Display Name", RRF_RT_REG_SZ, nullptr, layoutName, &layoutNameSize) == ERROR_SUCCESS &&
        SHLoadIndirectString)
    {
        // Convert string like "@%SystemRoot%\system32\input.dll,-5000" to localized string
        CHECK_EQ(SHLoadIndirectString(layoutName, layoutName, MAX_PATH, nullptr), S_OK);
    }

    if (wcslen(layoutName) == 0)
    {
        // Fallback to unlocalized layout name
        ::RegGetValueW(key, pwszKLID, L"Layout Text", RRF_RT_REG_SZ, nullptr, layoutName, &layoutNameSize);
    }

    if (wcslen(layoutName) == 0)
    {
        wcscpy(layoutName, pwszKLID);
    }

    CHECK_EQ(::RegCloseKey(key), ERROR_SUCCESS);

    return utf8::narrow(layoutName);
}

// Clears keyboard buffer
// Needed to avoid side effects on other calls to ToUnicode API
// http://archives.miloush.net/michkap/archive/2007/10/27/5717859.html
inline void ClearKeyboardBuffer(uint16_t vkCode)
{
    std::array<uint8_t, 256> keyboardState{};
    std::array<wchar_t, 10> chars{};
    const uint16_t scanCode = LOWORD(::MapVirtualKeyW(vkCode, MAPVK_VK_TO_VSC_EX));
    int count = 0;
    do
    {
        count = ::ToUnicode(vkCode, scanCode, keyboardState.data(), chars.data(), static_cast<int>(chars.size()), 0);
    } while (count < 0);
}

// Returns UTF-8 string
std::string GetStrFromKeyPress(uint16_t scanCode, bool isShift)
{
    std::array<uint8_t, 256> keyboardState{};
    std::array<wchar_t, 10> chars{};
    const uint16_t vkCode = LOWORD(::MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX));

    if (isShift)
        keyboardState[VK_SHIFT] = 0x80;

    ClearKeyboardBuffer(VK_DECIMAL);

    // For some keyboard layouts ToUnicode() API call can produce multiple chars: UTF-16 surrogate pairs or ligatures.
    // Such layouts are listed here: https://kbdlayout.info/features/ligatures
    int count = ::ToUnicode(vkCode, scanCode, keyboardState.data(), chars.data(), static_cast<int>(chars.size()), 0);

    // Negative value means that we have a `dead key`
    if (count < 0) 
    {
        if (chars[0] == L'\0' || std::iswcntrl(chars[0])) {
            return {};
        }

        count = -count;
    }

    ClearKeyboardBuffer(VK_DECIMAL);

    // Do not return control characters
    if (count <= 0 || (count == 1 && std::iswcntrl(chars[0]))) {
        return {};
    }

    return utf8::narrow(chars.data(), count);
}

// Get keyboard layout specific localized key name
std::string GetScanCodeName(uint16_t scanCode)
{
    switch (scanCode)
    {
        case 0xe010: // VK_MEDIA_PREV_TRACK
            return "Previous Track";
        case 0xe019: // VK_MEDIA_NEXT_TRACK
            return "Next Track";
        case 0xe020: // VK_VOLUME_MUTE
            return "Volume Mute";
        case 0xe021: // VK_LAUNCH_APP2
            return "Launch App 2";
        case 0xe022: // VK_MEDIA_PLAY_PAUSE
            return "Media Play/Pause";
        case 0xe024: // VK_MEDIA_STOP
            return "Media Stop";
        case 0xe02e: // VK_VOLUME_DOWN
            return "Volume Down";
        case 0xe030: // VK_VOLUME_UP
            return "Volume Up";
        case 0xe032: // VK_BROWSER_HOME
            return "Browser Home";
        case 0xe05e: // System Power (no VK code)
            return "System Power";
        case 0xe05f: // VK_SLEEP
            return "System Sleep";
        case 0xe063: // System Wake (no VK code)
            return "System Wake";
        case 0xe065: // VK_BROWSER_SEARCH
            return "Browser Search";
        case 0xe066: // VK_BROWSER_FAVORITES
            return "Browser Favorites";
        case 0xe067: // VK_BROWSER_REFRESH
            return "Browser Refresh";
        case 0xe068: // VK_BROWSER_STOP
            return "Browser Stop";
        case 0xe069: // VK_BROWSER_FORWARD
            return "Browser Forward";
        case 0xe06a: // VK_BROWSER_BACK
            return "Browser Back";
        case 0xe06b: // VK_LAUNCH_APP1
            return "Launch App 1";
        case 0xe06c: // VK_LAUNCH_MAIL
            return "Launch Mail";
        case 0xe06d: // VK_LAUNCH_MEDIA_SELECT
            return "Launch Media Selector";
    }

    std::wstring keyText = utf8::widen(GetStrFromKeyPress(scanCode));
    if (!keyText.empty() && !std::iswblank(keyText[0]))
    {
        constexpr LPCWSTR currentLocale = LOCALE_NAME_USER_DEFAULT;
        constexpr DWORD toUpperFlags = LCMAP_UPPERCASE | LCMAP_LINGUISTIC_CASING;
        int len = ::LCMapStringEx(currentLocale,
            toUpperFlags,
            keyText.c_str(), static_cast<int>(keyText.size()),
            nullptr, 0,
            nullptr, nullptr, 0);
        CHECK_GE(len, 1);

        std::unique_ptr<wchar_t[]> buffer(new wchar_t[len]);
        ::LCMapStringEx(currentLocale,
            toUpperFlags,
            keyText.c_str(), static_cast<int>(keyText.size()),
            buffer.get(), len,
            nullptr, nullptr, 0);

        return utf8::narrow(buffer.get(), len);
    }

    const LPARAM lParam = MAKELPARAM(0, ((scanCode & 0xff00) ? KF_EXTENDED : 0) | (scanCode & 0xff));
    wchar_t name[128] = {};
    size_t nameSize = ::GetKeyNameTextW(static_cast<LONG>(lParam), name, static_cast<int>(std::size(name)));

    return utf8::narrow(name, nameSize);
}

std::string VkToString(uint16_t vk)
{
#define printvk(vk) case vk: return #vk
    switch (vk)
    {
        printvk(VK_LBUTTON);
        printvk(VK_RBUTTON);
        printvk(VK_CANCEL);
        printvk(VK_MBUTTON);
        printvk(VK_XBUTTON1);
        printvk(VK_XBUTTON2);
        printvk(VK_BACK);
        printvk(VK_TAB);
        printvk(VK_CLEAR);
        printvk(VK_RETURN);
        printvk(VK_SHIFT);
        printvk(VK_CONTROL);
        printvk(VK_MENU);
        printvk(VK_PAUSE);
        printvk(VK_CAPITAL);
        printvk(VK_KANA);
        printvk(VK_IME_ON);
        printvk(VK_JUNJA);
        printvk(VK_FINAL);
        printvk(VK_HANJA);
        printvk(VK_IME_OFF);
        printvk(VK_ESCAPE);
        printvk(VK_CONVERT);
        printvk(VK_NONCONVERT);
        printvk(VK_ACCEPT);
        printvk(VK_MODECHANGE);
        printvk(VK_SPACE);
        printvk(VK_PRIOR);
        printvk(VK_NEXT);
        printvk(VK_END);
        printvk(VK_HOME);
        printvk(VK_LEFT);
        printvk(VK_UP);
        printvk(VK_RIGHT);
        printvk(VK_DOWN);
        printvk(VK_SELECT);
        printvk(VK_PRINT);
        printvk(VK_EXECUTE);
        printvk(VK_SNAPSHOT);
        printvk(VK_INSERT);
        printvk(VK_DELETE);
        printvk(VK_HELP);
        printvk(VK_LWIN);
        printvk(VK_RWIN);
        printvk(VK_APPS);
        printvk(VK_SLEEP);
        printvk(VK_NUMPAD0);
        printvk(VK_NUMPAD1);
        printvk(VK_NUMPAD2);
        printvk(VK_NUMPAD3);
        printvk(VK_NUMPAD4);
        printvk(VK_NUMPAD5);
        printvk(VK_NUMPAD6);
        printvk(VK_NUMPAD7);
        printvk(VK_NUMPAD8);
        printvk(VK_NUMPAD9);
        printvk(VK_MULTIPLY);
        printvk(VK_ADD);
        printvk(VK_SEPARATOR);
        printvk(VK_SUBTRACT);
        printvk(VK_DECIMAL);
        printvk(VK_DIVIDE);
        printvk(VK_F1);
        printvk(VK_F2);
        printvk(VK_F3);
        printvk(VK_F4);
        printvk(VK_F5);
        printvk(VK_F6);
        printvk(VK_F7);
        printvk(VK_F8);
        printvk(VK_F9);
        printvk(VK_F10);
        printvk(VK_F11);
        printvk(VK_F12);
        printvk(VK_F13);
        printvk(VK_F14);
        printvk(VK_F15);
        printvk(VK_F16);
        printvk(VK_F17);
        printvk(VK_F18);
        printvk(VK_F19);
        printvk(VK_F20);
        printvk(VK_F21);
        printvk(VK_F22);
        printvk(VK_F23);
        printvk(VK_F24);
        printvk(VK_NAVIGATION_VIEW);
        printvk(VK_NAVIGATION_MENU);
        printvk(VK_NAVIGATION_UP);
        printvk(VK_NAVIGATION_DOWN);
        printvk(VK_NAVIGATION_LEFT);
        printvk(VK_NAVIGATION_RIGHT);
        printvk(VK_NAVIGATION_ACCEPT);
        printvk(VK_NAVIGATION_CANCEL);
        printvk(VK_NUMLOCK);
        printvk(VK_SCROLL);
        printvk(VK_OEM_NEC_EQUAL);
        printvk(VK_OEM_FJ_MASSHOU);
        printvk(VK_OEM_FJ_TOUROKU);
        printvk(VK_OEM_FJ_LOYA);
        printvk(VK_OEM_FJ_ROYA);
        printvk(VK_LSHIFT);
        printvk(VK_RSHIFT);
        printvk(VK_LCONTROL);
        printvk(VK_RCONTROL);
        printvk(VK_LMENU);
        printvk(VK_RMENU);
        printvk(VK_BROWSER_BACK);
        printvk(VK_BROWSER_FORWARD);
        printvk(VK_BROWSER_REFRESH);
        printvk(VK_BROWSER_STOP);
        printvk(VK_BROWSER_SEARCH);
        printvk(VK_BROWSER_FAVORITES);
        printvk(VK_BROWSER_HOME);
        printvk(VK_VOLUME_MUTE);
        printvk(VK_VOLUME_DOWN);
        printvk(VK_VOLUME_UP);
        printvk(VK_MEDIA_NEXT_TRACK);
        printvk(VK_MEDIA_PREV_TRACK);
        printvk(VK_MEDIA_STOP);
        printvk(VK_MEDIA_PLAY_PAUSE);
        printvk(VK_LAUNCH_MAIL);
        printvk(VK_LAUNCH_MEDIA_SELECT);
        printvk(VK_LAUNCH_APP1);
        printvk(VK_LAUNCH_APP2);
        printvk(VK_OEM_1);
        printvk(VK_OEM_PLUS);
        printvk(VK_OEM_COMMA);
        printvk(VK_OEM_MINUS);
        printvk(VK_OEM_PERIOD);
        printvk(VK_OEM_2);
        printvk(VK_OEM_3);
        printvk(VK_GAMEPAD_A);
        printvk(VK_GAMEPAD_B);
        printvk(VK_GAMEPAD_X);
        printvk(VK_GAMEPAD_Y);
        printvk(VK_GAMEPAD_RIGHT_SHOULDER);
        printvk(VK_GAMEPAD_LEFT_SHOULDER);
        printvk(VK_GAMEPAD_LEFT_TRIGGER);
        printvk(VK_GAMEPAD_RIGHT_TRIGGER);
        printvk(VK_GAMEPAD_DPAD_UP);
        printvk(VK_GAMEPAD_DPAD_DOWN);
        printvk(VK_GAMEPAD_DPAD_LEFT);
        printvk(VK_GAMEPAD_DPAD_RIGHT);
        printvk(VK_GAMEPAD_MENU);
        printvk(VK_GAMEPAD_VIEW);
        printvk(VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON);
        printvk(VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON);
        printvk(VK_GAMEPAD_LEFT_THUMBSTICK_UP);
        printvk(VK_GAMEPAD_LEFT_THUMBSTICK_DOWN);
        printvk(VK_GAMEPAD_LEFT_THUMBSTICK_RIGHT);
        printvk(VK_GAMEPAD_LEFT_THUMBSTICK_LEFT);
        printvk(VK_GAMEPAD_RIGHT_THUMBSTICK_UP);
        printvk(VK_GAMEPAD_RIGHT_THUMBSTICK_DOWN);
        printvk(VK_GAMEPAD_RIGHT_THUMBSTICK_RIGHT);
        printvk(VK_GAMEPAD_RIGHT_THUMBSTICK_LEFT);
        printvk(VK_OEM_4);
        printvk(VK_OEM_5);
        printvk(VK_OEM_6);
        printvk(VK_OEM_7);
        printvk(VK_OEM_8);
        printvk(VK_OEM_AX);
        printvk(VK_OEM_102);
        printvk(VK_ICO_HELP);
        printvk(VK_ICO_00);
        printvk(VK_PROCESSKEY);
        printvk(VK_ICO_CLEAR);
        printvk(VK_PACKET);
        printvk(VK_OEM_RESET);
        printvk(VK_OEM_JUMP);
        printvk(VK_OEM_PA1);
        printvk(VK_OEM_PA2);
        printvk(VK_OEM_PA3);
        printvk(VK_OEM_WSCTRL);
        printvk(VK_OEM_CUSEL);
        printvk(VK_OEM_ATTN);
        printvk(VK_OEM_FINISH);
        printvk(VK_OEM_COPY);
        printvk(VK_OEM_AUTO);
        printvk(VK_OEM_ENLW);
        printvk(VK_OEM_BACKTAB);
        printvk(VK_ATTN);
        printvk(VK_CRSEL);
        printvk(VK_EXSEL);
        printvk(VK_EREOF);
        printvk(VK_PLAY);
        printvk(VK_ZOOM);
        printvk(VK_NONAME);
        printvk(VK_PA1);
        printvk(VK_OEM_CLEAR);
    }
#undef printvk

    std::array<char, 10> str;
    if ((vk >= (uint16_t)'A') && (vk <= (uint16_t)'Z') ||
        (vk >= (uint16_t)'0') && (vk <= (uint16_t)'9'))
    {
        std::snprintf(str.data(), str.size(), "VK_%c", vk);
    }
    else
    {
        std::snprintf(str.data(), str.size(), "0x%x", vk);
    }

    return std::string(str.data(), str.size());
}

//#ifdef _DEBUG
VOID DebugPrint(const char* /*function_name*/, unsigned int /*line_number*/, const char* format, ...)
{
    std::array<char, 1024> formatted;

    va_list args;
    va_start(args, format);

    vsnprintf(formatted.data(), formatted.size(), format, args);

    va_end(args);

    ::OutputDebugStringW(utf8::widen(fmt::format("{}\n", formatted.data())).c_str());
    std::printf("%s\n", formatted.data());
}
//#endif


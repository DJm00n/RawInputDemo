#include "pch.h"

#include "framework.h"

#include "utils.h"

#include <cwctype>
#include <codecvt>
#include <kbd.h>
#include <shlwapi.h>

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
    std::array<wchar_t, 256> buffer;
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
    int32_t length = pfnU_charName(codePoint, 2/*U_EXTENDED_CHAR_NAME*/, buffer.data(), static_cast<int32_t>(buffer.size() - 1), &errorCode);

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
    wchar_t layoutProfile[MAX_PATH];
    std::swprintf(layoutProfile, MAX_PATH, L"%04x:%hs", LOWORD(hkl), GetKlidFromHkl(hkl).c_str());

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
        if (stringutils::ci_wstring(layout.szId).compare(layoutProfileId.c_str()) != 0)
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

std::string GetLocaleInformation(const std::string& locale, LCTYPE LCType)
{
    std::wstring tmp = utf8::widen(locale);

    int len = ::GetLocaleInfoEx(tmp.c_str(), LCType, nullptr, 0);
    CHECK_GE(len, 1);

    std::unique_ptr<wchar_t[]> buffer(new wchar_t[len]);
    ::GetLocaleInfoEx(tmp.c_str(), LCType, buffer.get(), len);

    return utf8::narrow(buffer.get());
}

std::string GetBcp47FromHkl(HKL hkl)
{
    // According to the GetKeyboardLayout API function docs low word of HKL contains input language
    // identifier.
    LANGID langId = LOWORD(hkl);
    wchar_t language[LOCALE_NAME_MAX_LENGTH] = { 0 };

    // We need to convert the language identifier to a language tag as soon as
    // possible, because they are obsolete and may have a transient value - 0x2000,
    // 0x2400, 0x2800, 0x2C00.
    // https://learn.microsoft.com/globalization/locale/locale-names#the-deprecation-of-lcids
    //
    // It turns out that the LCIDToLocaleName API may return incorrect language tags
    // for transient language identifiers. For example, it returns "nqo-GN" and
    // "jv-Java-ID" instead of the "nqo" and "jv-Java" (as seen in the
    // Get-WinUserLanguageList PowerShell cmdlet). Try to extract proper language tag
    // from registry.
    if (langId == LOCALE_TRANSIENT_KEYBOARD1 ||
        langId == LOCALE_TRANSIENT_KEYBOARD2 ||
        langId == LOCALE_TRANSIENT_KEYBOARD3 ||
        langId == LOCALE_TRANSIENT_KEYBOARD4)
    {
        HKEY key;
        CHECK_EQ(::RegOpenKeyW(HKEY_CURRENT_USER, L"Control Panel\\International\\User Profile", &key), ERROR_SUCCESS);

        DWORD bytes = 0;
        CHECK_EQ(::RegGetValueW(key, nullptr, L"Languages", RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &bytes), ERROR_SUCCESS);

        std::unique_ptr<wchar_t[]> installedLanguages(new wchar_t[bytes / sizeof(wchar_t)]);
        CHECK_EQ(::RegGetValueW(key, nullptr, L"Languages", RRF_RT_REG_MULTI_SZ, nullptr, installedLanguages.get(), &bytes), ERROR_SUCCESS);

        for (wchar_t* installedLanguage = installedLanguages.get(); *installedLanguage; installedLanguage += wcslen(installedLanguage) + 1)
        {
            DWORD transientLangId = 0;
            bytes = sizeof(transientLangId);
            if (::RegGetValueW(key, installedLanguage, L"TransientLangId", RRF_RT_REG_DWORD, nullptr, &transientLangId, &bytes) == ERROR_SUCCESS)
            {
                if (langId == transientLangId)
                {
                    wcscpy(language, installedLanguage);
                    break;
                }
            }
        }

        CHECK_EQ(::RegCloseKey(key), ERROR_SUCCESS);
    }

    if (wcslen(language) == 0)
    {
        CHECK(::LCIDToLocaleName(langId, language, (int)std::size(language), 0));
    }

    return utf8::narrow(language);
}

std::string GetKlidFromHkl(HKL hkl)
{
    wchar_t klid[KL_NAMELENGTH] = { 0 };

    WORD device = HIWORD(hkl);

    if ((device & 0xf000) == 0xf000) // `Device Handle` contains `Layout ID`
    {
        WORD layoutId = device & 0x0fff;

        HKEY key;
        CHECK_EQ(::RegOpenKeyW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts", &key), ERROR_SUCCESS);

        DWORD index = 0;
        wchar_t buffer[KL_NAMELENGTH];
        DWORD len = (DWORD)std::size(buffer);
        while (::RegEnumKeyExW(key, index, buffer, &len, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
        {
            wchar_t layoutIdBuffer[MAX_PATH] = {};
            DWORD layoutIdBufferSize = sizeof(layoutIdBuffer);
            if (::RegGetValueW(key, buffer, L"Layout Id", RRF_RT_REG_SZ, nullptr, layoutIdBuffer, &layoutIdBufferSize) == ERROR_SUCCESS)
            {
                if (layoutId == std::stoul(layoutIdBuffer, nullptr, 16))
                {
                    _wcsupr(buffer);
                    wcscpy(klid, buffer);
                    //DBGPRINT("Found KLID %ls by layoutId=0x%04x", klid, layoutId);
                    break;
                }
            }
            len = (DWORD)std::size(buffer);
            ++index;
        }

        CHECK_EQ(::RegCloseKey(key), ERROR_SUCCESS);
    }
    else
    {
        // Use input language only if keyboard layout language is not available. This
        // is crucial in cases when keyboard is installed more than once or under
        // different languages. For example when French keyboard is installed under US
        // input language we need to return French keyboard identifier.
        if (device == 0)
        {
            device = LOWORD(hkl);
        }

        std::swprintf(klid, std::size(klid), L"%08X", device);
        //DBGPRINT("Found KLID %ls by langId=0x%04x", klid, device);
    }

    return utf8::narrow(klid);
}

std::string GetKeyboardLayoutDisplayName(const std::string& klid)
{
    // http://archives.miloush.net/michkap/archive/2006/05/06/591174.html
    // https://github.com/dotnet/winforms/issues/4345

    wchar_t regPath[MAX_PATH] = { 0 };
    std::swprintf(regPath, std::size(regPath), L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts\\%hs", klid.c_str());

    HKEY key;
    CHECK_EQ(::RegOpenKeyW(HKEY_LOCAL_MACHINE, regPath, &key), ERROR_SUCCESS);

    // Convert string like "@%SystemRoot%\system32\input.dll,-5000" to localized "US" string
    wchar_t layoutName[MAX_PATH] = {};
    if (RegLoadMUIStringW(key, L"Layout Display Name", layoutName, (DWORD)std::size(layoutName), nullptr, REG_MUI_STRING_TRUNCATE, nullptr) != ERROR_SUCCESS)
    {
        // Fallback to unlocalized layout name
        DWORD layoutNameSize = sizeof(layoutName);
        CHECK_EQ(::RegGetValueW(key, nullptr, L"Layout Text", RRF_RT_REG_SZ, nullptr, layoutName, &layoutNameSize), ERROR_SUCCESS);
    }

    if (wcslen(layoutName) == 0)
    {
        wcscpy(layoutName, L"Unknown layout");
    }

    CHECK_EQ(::RegCloseKey(key), ERROR_SUCCESS);

    return utf8::narrow(layoutName);
}

std::string GetLayoutDescription(HKL hkl)
{
    std::string locale = GetBcp47FromHkl(hkl);
    std::string layoutId = GetKlidFromHkl(hkl);

    std::string layoutLang = GetLocaleInformation(locale, LOCALE_SENGLISHDISPLAYNAME);
    std::string layoutDisplayName = GetKeyboardLayoutDisplayName(layoutId);

    return layoutLang + " - " + layoutDisplayName;
}

std::vector<std::string> EnumInstalledKeyboardLayouts()
{
    std::vector<std::string> layouts;

    HKEY key;
    CHECK_EQ(::RegOpenKeyW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts", &key), ERROR_SUCCESS);

    DWORD index = 0;
    wchar_t layoutName[MAX_PATH] = {};
    DWORD layoutNameSize = (DWORD)std::size(layoutName);

    while (::RegEnumKeyExW(key, index, layoutName, &layoutNameSize, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
    {
        layouts.emplace_back(utf8::narrow(layoutName));
        layoutNameSize = (DWORD)std::size(layoutName);
        ++index;
    }

    return layouts;
}

// Clears keyboard buffer
// Needed to avoid side effects on other calls to ToUnicode API
// http://archives.miloush.net/michkap/archive/2007/10/27/5717859.html
inline void ClearKeyboardBuffer(uint16_t vkCode)
{
    std::array<wchar_t, 10> chars{};
    const uint16_t scanCode = LOWORD(::MapVirtualKeyW(vkCode, MAPVK_VK_TO_VSC_EX));
    int count = 0;
    do
    {
        count = ::ToUnicode(vkCode, scanCode, nullptr, chars.data(), static_cast<int>(chars.size()), 0);
    } while (count < 0);
}

std::string GetStringFromKeyPress(uint16_t scanCode)
{
    std::array<wchar_t, 10> chars{};
    const uint16_t vkCode = LOWORD(::MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX));
    std::array<uint8_t, 256> keyboardState{};

    // Turn on CapsLock to return capital letters
    keyboardState[VK_CAPITAL] = 0b00000001;

    ClearKeyboardBuffer(VK_DECIMAL);

    // For some keyboard layouts ToUnicode() API call can produce multiple chars: UTF-16 surrogate pairs or ligatures.
    // Such layouts are listed here: https://kbdlayout.info/features/ligatures
    int count = ::ToUnicode(vkCode, scanCode, keyboardState.data(), chars.data(), static_cast<int>(chars.size()), 0);

    ClearKeyboardBuffer(VK_DECIMAL);

    return utf8::narrow(chars.data(), std::abs(count));
}

std::string GetScanCodeName(uint16_t scanCode)
{
    static struct
    {
        uint16_t scanCode;
        const char* keyText;
    } mediaKeys[] =
    {
        { 0xe010, "Previous Track"}, // VK_MEDIA_PREV_TRACK
        { 0xe019, "Next Track"}, // VK_MEDIA_NEXT_TRACK
        { 0xe020, "Volume Mute"}, // VK_VOLUME_MUTE
        { 0xe021, "Launch App 2"}, // VK_LAUNCH_APP2
        { 0xe022, "Media Play/Pause"}, // VK_MEDIA_PLAY_PAUSE
        { 0xe024, "Media Stop"},// VK_MEDIA_STOP
        { 0xe02e, "Volume Down"}, // VK_VOLUME_DOWN
        { 0xe030, "Volume Up"}, // VK_VOLUME_UP
        { 0xe032, "Browser Home"}, // VK_BROWSER_HOME
        { 0xe05e, "System Power"}, // System Power (no VK code)
        { 0xe05f, "System Sleep"}, // VK_SLEEP
        { 0xe063, "System Wake"}, // System Wake (no VK code)
        { 0xe065, "Browser Search"}, // VK_BROWSER_SEARCH
        { 0xe066, "Browser Favorites"}, // VK_BROWSER_FAVORITES
        { 0xe067, "Browser Refresh"}, // VK_BROWSER_REFRESH
        { 0xe068, "Browser Stop"}, // VK_BROWSER_STOP
        { 0xe069, "Browser Forward"}, // VK_BROWSER_FORWARD
        { 0xe06a, "Browser Back"}, // VK_BROWSER_BACK
        { 0xe06b, "Launch App 1"}, // VK_LAUNCH_APP1
        { 0xe06c, "Launch Mail"}, // VK_LAUNCH_MAIL
        { 0xe06d, "Launch Media Selector"} // VK_LAUNCH_MEDIA_SELECT
    };

    auto it = std::find_if(std::begin(mediaKeys), std::end(mediaKeys),
        [scanCode](auto& key) { return key.scanCode == scanCode; });
    if (it != std::end(mediaKeys))
        return it->keyText;

    std::string keyText = GetStringFromKeyPress(scanCode);
    std::wstring keyTextWide = utf8::widen(keyText);
    if (!keyTextWide.empty() && !std::iswblank(keyTextWide[0]) && !std::iswcntrl(keyTextWide[0]))
    {
        return keyText;
    }

    std::array<wchar_t, 128> buffer{};
    const LPARAM lParam = MAKELPARAM(0, ((scanCode & 0xff00) ? KF_EXTENDED : 0) | (scanCode & 0xff));
    int count = ::GetKeyNameTextW(static_cast<LONG>(lParam), buffer.data(), static_cast<int>(buffer.size()));

    return utf8::narrow(buffer.data(), count);
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


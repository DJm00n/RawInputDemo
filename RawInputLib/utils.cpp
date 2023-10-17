#include "pch.h"

#include "framework.h"

#include "utils.h"

#include <cwctype>
#include <codecvt>
#include <set>

#include <hidsdi.h>
#include <hidpi.h>

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
    static GetUNameFunc pfnGetUName = reinterpret_cast<GetUNameFunc>(::GetProcAddress(::LoadLibraryW(L"getuname.dll"), "GetUName"));

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
    static u_charNameFunc pfnU_charName = reinterpret_cast<u_charNameFunc>(::GetProcAddress(::LoadLibraryW(L"icuuc.dll"), "u_charName"));

    if (!pfnU_charName)
        return GetUNameWrapper(codePoint);

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
    static EnumEnabledLayoutOrTipFunc EnumEnabledLayoutOrTip = reinterpret_cast<EnumEnabledLayoutOrTipFunc>(::GetProcAddress(::LoadLibraryW(L"input.dll"), "EnumEnabledLayoutOrTip"));

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
    static GetDefaultLayoutFunc GetDefaultLayout = reinterpret_cast<GetDefaultLayoutFunc>(::GetProcAddress(::LoadLibraryW(L"input.dll"), "GetDefaultLayout"));

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
    static GetLayoutDescriptionFunc GetLayoutDescription = reinterpret_cast<GetLayoutDescriptionFunc>(::GetProcAddress(::LoadLibraryW(L"input.dll"), "GetLayoutDescription"));

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

BOOL Bcp47FromHkl(HKL hkl, LPWSTR pszDest, DWORD cchDest)
{
    constexpr wchar_t userProfileRegPath[] = L"Control Panel\\International\\User Profile";
    constexpr wchar_t regLanguages[] = L"Languages";
    constexpr wchar_t regTransientLangId[] = L"TransientLangId";

    LANGID langId = LOWORD(HandleToUlong(hkl));

    memset(pszDest, 0, sizeof(WCHAR) * cchDest);

    if (langId == LOCALE_TRANSIENT_KEYBOARD1 ||
        langId == LOCALE_TRANSIENT_KEYBOARD2 ||
        langId == LOCALE_TRANSIENT_KEYBOARD3 ||
        langId == LOCALE_TRANSIENT_KEYBOARD4)
    {
        HKEY key;
        LPWSTR buffer;
        DWORD size = 0;

        if (RegOpenKeyExW(HKEY_CURRENT_USER, userProfileRegPath, 0, KEY_READ, &key) != ERROR_SUCCESS)
            return FALSE;

        if (RegGetValueW(key, NULL, regLanguages, RRF_RT_REG_MULTI_SZ, NULL, NULL, &size) != ERROR_SUCCESS)
            return FALSE;

        buffer = (LPWSTR)malloc(size);

        if (RegGetValueW(key, NULL, regLanguages, RRF_RT_REG_MULTI_SZ, NULL, buffer, &size) != ERROR_SUCCESS)
        {
            free(buffer);
            return FALSE;
        }

        for (LPWSTR lang = buffer; *lang; lang += wcsnlen_s(lang, (size / sizeof(WCHAR)) - (lang - buffer)) + 1)
        {
            DWORD regLangId = 0;
            DWORD regLangIdSize = sizeof(regLangId);
            if (RegGetValueW(key, lang, regTransientLangId, RRF_RT_REG_DWORD, NULL, &regLangId, &regLangIdSize) != ERROR_SUCCESS)
                continue;

            if (langId != regLangId)
                continue;

            wcscpy_s(pszDest, cchDest, lang);
            break;
        }

        free(buffer);

        if (RegCloseKey(key) != ERROR_SUCCESS)
            return FALSE;
    }

    if (wcsnlen_s(pszDest, cchDest) == 0)
    {
        LCIDToLocaleName(MAKELCID(langId, SORT_DEFAULT), pszDest, cchDest, 0);
    }

    return wcsnlen_s(pszDest, cchDest) != 0;
}

std::string GetBcp47FromHkl(HKL hkl)
{
    wchar_t language[LOCALE_NAME_MAX_LENGTH] = { 0 };
    Bcp47FromHkl(hkl, language, (DWORD)std::size(language));

    return utf8::narrow(language);
}

std::string GetKlidFromHkl(HKL hkl)
{
    constexpr wchar_t keyboardLayoutsRegPath[] = L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts";
    constexpr wchar_t regLayoutId[] = L"Layout Id";

    wchar_t klid[KL_NAMELENGTH] = { 0 };

    WORD device = HIWORD(hkl);

    if ((device & 0xf000) == 0xf000) // `Device Handle` contains `Layout ID`
    {
        WORD layoutId = device & 0x0fff;

        HKEY key;
        CHECK_EQ(::RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyboardLayoutsRegPath, 0, KEY_READ, &key), ERROR_SUCCESS);

        DWORD index = 0;
        wchar_t buffer[KL_NAMELENGTH];
        DWORD len = (DWORD)std::size(buffer);
        while (::RegEnumKeyExW(key, index, buffer, &len, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
        {
            wchar_t layoutIdBuffer[MAX_PATH] = {};
            DWORD layoutIdBufferSize = sizeof(layoutIdBuffer);
            if (::RegGetValueW(key, buffer, regLayoutId, RRF_RT_REG_SZ, nullptr, layoutIdBuffer, &layoutIdBufferSize) == ERROR_SUCCESS)
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
    constexpr wchar_t keyboardLayoutsRegPath[] = L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts";
    constexpr wchar_t keyboardLayoutDisplayName[] = L"Layout Display Name";
    constexpr wchar_t keyboardLayoutText[] = L"Layout Text";

    // http://archives.miloush.net/michkap/archive/2006/05/06/591174.html
    // https://learn.microsoft.com/windows/win32/intl/using-registry-string-redirection#create-resources-for-keyboard-layout-strings
    HKEY key;
    const std::wstring fullRegPath = std::wstring(keyboardLayoutsRegPath) + L"\\" + utf8::widen(klid);
    CHECK_EQ(::RegOpenKeyExW(HKEY_LOCAL_MACHINE, fullRegPath.c_str(), 0, KEY_READ, &key), ERROR_SUCCESS);

    // https://learn.microsoft.com/windows/win32/intl/locating-redirected-strings#load-a-language-neutral-registry-value
    std::unique_ptr<wchar_t[]> buffer;
    DWORD bytes = 0;
    if (::RegLoadMUIStringW(key, keyboardLayoutDisplayName, nullptr, 0, &bytes, 0, nullptr) == ERROR_MORE_DATA)
    {
        buffer.reset(new wchar_t[bytes / sizeof(wchar_t)]);
        CHECK_EQ(::RegLoadMUIStringW(key, keyboardLayoutDisplayName, buffer.get(), bytes, &bytes, 0, nullptr), ERROR_SUCCESS);
    }

    // Fallback to unlocalized layout name
    if (buffer.get() == nullptr)
    {
        CHECK_EQ(::RegGetValueW(key, nullptr, keyboardLayoutText, RRF_RT_REG_SZ, nullptr, nullptr, &bytes), ERROR_SUCCESS);
        buffer.reset(new wchar_t[bytes / sizeof(wchar_t)]);
        CHECK_EQ(::RegGetValueW(key, nullptr, keyboardLayoutText, RRF_RT_REG_SZ, nullptr, buffer.get(), &bytes), ERROR_SUCCESS);
    }

    if (buffer.get() == nullptr)
    {
        buffer.reset(new wchar_t[](L"Unknown layout"));
    }

    CHECK_EQ(::RegCloseKey(key), ERROR_SUCCESS);

    return utf8::narrow(buffer.get());
}

std::string GetLayoutDescription(HKL hkl)
{
    std::string locale = GetBcp47FromHkl(hkl);
    std::string layoutLang = GetLocaleInformation(locale, LOCALE_SLOCALIZEDDISPLAYNAME);

    std::string layoutId = GetKlidFromHkl(hkl);
    std::string layoutDisplayName = GetKeyboardLayoutDisplayName(layoutId);

    return layoutLang + " - " + layoutDisplayName;
}

std::string GetIcuLocaleFromBcp47(const std::string& langTag)
{
    typedef int32_t(*uloc_forLanguageTagFunc)(const char* langtag, char* localeID, int32_t localeIDCapacity, int32_t* parsedLength, int* err);
    static uloc_forLanguageTagFunc pfnUloc_forLanguageTag = reinterpret_cast<uloc_forLanguageTagFunc>(::GetProcAddress(::LoadLibraryW(L"icuuc.dll"), "uloc_forLanguageTag"));

    if (!pfnUloc_forLanguageTag)
        return {};

    int32_t parsedLength = 0;
    int errorCode = 0;
    std::array<char, 512> buffer;
    int32_t length = pfnUloc_forLanguageTag(langTag.c_str(), buffer.data(), static_cast<int32_t>(buffer.size() - 1), &parsedLength, &errorCode);

    if (errorCode != 0)
        return {};

    return std::string(buffer.data(), length);
}

std::string GetIcuMinLocaleIDFromLocaleID(const std::string& localeID)
{
    typedef int32_t(*uloc_minimizeSubtagsFunc)(const char* localeID, char* minimizedLocaleID, int32_t minimizedLocaleIDCapacity, int* err);
    static uloc_minimizeSubtagsFunc pfUloc_minimizeSubtags = reinterpret_cast<uloc_minimizeSubtagsFunc>(::GetProcAddress(::LoadLibraryW(L"icuuc.dll"), "uloc_minimizeSubtags"));

    if (!pfUloc_minimizeSubtags)
        return {};

    int errorCode = 0;
    std::array<char, 512> buffer;
    int32_t length = pfUloc_minimizeSubtags(localeID.c_str(), buffer.data(), static_cast<int32_t>(buffer.size() - 1), &errorCode);

    if (errorCode != 0)
        return {};

    return std::string(buffer.data(), length);
}

std::string GetIcuLocaleDisplayName(const std::string& icuLocale)
{
    typedef int32_t(*uloc_getDisplayNameFunc)(const char* localeID, const char* inLocaleID, wchar_t* result, int32_t maxResultSize, int* err);
    static uloc_getDisplayNameFunc pfnUloc_getDisplayName = reinterpret_cast<uloc_getDisplayNameFunc>(::GetProcAddress(::LoadLibraryW(L"icuuc.dll"), "uloc_getDisplayName"));

    if (!pfnUloc_getDisplayName)
        return {};

    int errorCode = 0;
    std::array<wchar_t, 512> buffer;
    int32_t length = pfnUloc_getDisplayName(icuLocale.c_str(), "en", buffer.data(), static_cast<int32_t>(buffer.size() - 1), &errorCode);

    if (errorCode != 0)
        return {};

    return utf8::narrow(buffer.data(), length);
}

std::string GetLayoutDescriptionIcu(HKL hkl)
{
    std::string locale = GetBcp47FromHkl(hkl);
    std::string icuLocale = GetIcuLocaleFromBcp47(locale);
    std::string icuLocaleMin = GetIcuMinLocaleIDFromLocaleID(icuLocale);
    std::string layoutLang = GetIcuLocaleDisplayName(icuLocaleMin);

    std::string layoutId = GetKlidFromHkl(hkl);
    std::string layoutDisplayName = GetKeyboardLayoutDisplayName(layoutId);

    return layoutLang + " - " + layoutDisplayName;
}

std::vector<std::string> EnumInstalledKeyboardLayouts()
{
    constexpr wchar_t keyboardLayoutsRegPath[] = L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts";

    std::vector<std::string> layouts;

    HKEY key;
    CHECK_EQ(::RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyboardLayoutsRegPath, 0, KEY_READ, &key), ERROR_SUCCESS);

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

std::map<uint32_t, uint32_t> GetUsagesToScanCodes()
{
    // Translate declared usages from HID_USAGE_PAGE_KEYBOARD page
    // See 10. Keyboard/Keypad Page (0x07) in HID Usage Tables 1.4
    std::map<uint32_t, uint32_t> usagesToScanCodes;
    constexpr USAGE minKeyboardUsage = 0x01; 
    constexpr USAGE maxKeboardUsage = 0xe7;
    for (USAGE usage = minKeyboardUsage; usage <= maxKeboardUsage; ++usage)
    {
        HIDP_KEYBOARD_MODIFIER_STATE modifiers{};
        uint32_t scanCode = 0;

        NTSTATUS status = HidP_TranslateUsagesToI8042ScanCodes(
            &usage,
            1,
            HidP_Keyboard_Make,
            &modifiers,
            [](PVOID context, PCHAR newScanCodes, ULONG length) -> BOOLEAN
            {
                uint32_t& scanCode = *reinterpret_cast<uint32_t*>(context);

                CHECK_LE(length, 3);

                switch (length)
                {
                case 1:
                    scanCode = (uint8_t)newScanCodes[0];
                    break;
                case 2:
                    scanCode = ((uint8_t)newScanCodes[0] << 8) | (uint8_t)newScanCodes[1];
                    break;
                case 3:
                    scanCode = ((uint8_t)newScanCodes[0] << 16) | ((uint8_t)newScanCodes[1] << 8) | (uint8_t)newScanCodes[2];
                    break;
                }

                return TRUE;
            },
            reinterpret_cast<PVOID>(&scanCode));
        CHECK(status == HIDP_STATUS_SUCCESS || status == HIDP_STATUS_I8042_TRANS_UNKNOWN);

        if (scanCode != 0)
        {
            usagesToScanCodes[(HID_USAGE_PAGE_KEYBOARD << 16) | usage] = scanCode;
        }
    }

    // Additional mapped scan codes.
    // Looks like HidP_TranslateUsageAndPagesToI8042ScanCodes cannot be called from user-mode
    // So just add known buttons to the list:
    static struct
    {
        uint32_t usageAndPage;
        uint32_t scanCode;
    } additionalMappings[] =
    {
        { HID_USAGE_PAGE_GENERIC << 16 | 0x0081, 0xe05e }, // System Power Down
        { HID_USAGE_PAGE_GENERIC << 16 | 0x0082, 0xe05f }, // System Sleep
        { HID_USAGE_PAGE_GENERIC << 16 | 0x0083, 0xe063 }, // System Wake Up
        { HID_USAGE_PAGE_KEYBOARD << 16 | 0x0001, 0x00ff }, // ErrorRollOver
        { HID_USAGE_PAGE_CONSUMER << 16 | 0x00b5, 0xe019 }, // Scan Next Track
        { HID_USAGE_PAGE_CONSUMER << 16 | 0x00b6, 0xe010 }, // Scan Previous Track
        { HID_USAGE_PAGE_CONSUMER << 16 | 0x00b7, 0xe024 }, // Stop
        { HID_USAGE_PAGE_CONSUMER << 16 | 0x00cd, 0xe022 }, // Play/Pause
        { HID_USAGE_PAGE_CONSUMER << 16 | 0x00e2, 0xe020 }, // Mute
        { HID_USAGE_PAGE_CONSUMER << 16 | 0x00e9, 0xe030 }, // Volume Increment
        { HID_USAGE_PAGE_CONSUMER << 16 | 0x00ea, 0xe02e }, // Volume Decrement
        { HID_USAGE_PAGE_CONSUMER << 16 | 0x0183, 0xe06d }, // AL Consumer Control Configuration
        { HID_USAGE_PAGE_CONSUMER << 16 | 0x018a, 0xe06c }, // AL Email Reader
        { HID_USAGE_PAGE_CONSUMER << 16 | 0x0192, 0xe021 }, // AL Calculator
        { HID_USAGE_PAGE_CONSUMER << 16 | 0x0194, 0xe06b }, // AL Local Machine Browser
        { HID_USAGE_PAGE_CONSUMER << 16 | 0x0221, 0xe065 }, // AC Search
        { HID_USAGE_PAGE_CONSUMER << 16 | 0x0223, 0xe032 }, // AC Home
        { HID_USAGE_PAGE_CONSUMER << 16 | 0x0224, 0xe06a }, // AC Back
        { HID_USAGE_PAGE_CONSUMER << 16 | 0x0225, 0xe069 }, // AC Forward
        { HID_USAGE_PAGE_CONSUMER << 16 | 0x0226, 0xe068 }, // AC Stop
        { HID_USAGE_PAGE_CONSUMER << 16 | 0x0227, 0xe067 }, // AC Refresh
        { HID_USAGE_PAGE_CONSUMER << 16 | 0x022a, 0xe066 }, // AC Previous Link
    };

    for (const auto& mapping : additionalMappings)
    {
        usagesToScanCodes[mapping.usageAndPage] = mapping.scanCode;
    }

    return usagesToScanCodes;
}

std::string GetScanCodeName(uint16_t scanCode)
{
    /*static struct
    {
        uint16_t scanCode;
        const char* keyText;
    } missingKeys[] =
    {
        { 0x0059, "Keypad Equals"}, // VK_CLEAR
        { 0x005c, "International 6"}, // VK_OEM_JUMP
        { 0x0064, "F13"}, // VK_F13
        { 0x0065, "F14"}, // VK_F14
        { 0x0066, "F15"}, // VK_F15
        { 0x0067, "F16"}, // VK_F16
        { 0x0068, "F17"}, // VK_F17
        { 0x0069, "F18"}, // VK_F18
        { 0x006a, "F19"}, // VK_F19
        { 0x006b, "F20"}, // VK_F20
        { 0x006c, "F21"}, // VK_F21
        { 0x006d, "F22"}, // VK_F22
        { 0x006e, "F23"}, // VK_F23
        { 0x0070, "International 2"}, // VK_DBE_HIRAGANA in Japan layout
        { 0x0071, "LANG2"}, // VK_OEM_RESET or VK_HANGEUL in Korean layout
        { 0x0072, "LANG1"}, // VK_HANJA in Korean layout
        { 0x0073, "International 1"}, // VK_ABNT_C1
        { 0x0076, "F24"}, // VK_F24
        { 0x0079, "International 4"}, // VK_CONVERT in Japan layout
        { 0x007b, "International 5"}, // VK_OEM_PA1 or VK_NONCONVERT in Japan layout
        { 0x007e, "Keypad Comma"}, // VK_ABNT_C2 in Japan layout
        { 0x007d, "International 3"}, // VK_OEM_5 in Japan layout
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

    auto it = std::find_if(std::begin(missingKeys), std::end(missingKeys),
        [scanCode](auto& key) { return key.scanCode == scanCode; });
    if (it != std::end(missingKeys))
        return it->keyText;*/

    std::string keyText = GetStringFromKeyPress(scanCode);
    std::wstring keyTextWide = utf8::widen(keyText);
    if (!keyTextWide.empty() && !std::iswblank(keyTextWide[0]) && !std::iswcntrl(keyTextWide[0]))
    {
        return keyText;
    }

    // Keyboard Scan Code Specification:
    //
    // Avoid Set 1 scan codes above 0x79, since the release (key up) code would be
    // 0xFA or greater, and the keyboard driver is known to interpret such values as
    // responses from the 8042 chip, not as keystrokes (scan codes).
    //
    // Avoid 0x60 and 0x61, since the release (up key) code would be 0xE0 and 0xE1,
    // which are reserved prefix bytes.
    //
    // But GetKeyNameTextW may return some WRONG values. Skip.
    if ((scanCode & 0xff) > 0x79 || (scanCode & 0xff) == 0x60 || (scanCode & 0xff) == 0x61)
    {
        return "";
    }

    std::array<wchar_t, 128> buffer{};
    const LPARAM lParam = MAKELPARAM(0, ((scanCode & 0xff00) ? KF_EXTENDED : 0) | (scanCode & 0xff));
    int count = ::GetKeyNameTextW(static_cast<LONG>(lParam), buffer.data(), static_cast<int>(buffer.size()));

    return utf8::narrow(buffer.data(), count);
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


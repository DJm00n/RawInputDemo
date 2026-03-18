#include "pch.h"

#include "framework.h"

#include "utils.h"
#include "utils_winrt.h"

#include <cwctype>
#include <codecvt>
#include <set>

#include <hidsdi.h>
#include <hidpi.h>
#include <span>

#include <combaseapi.h>

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

static std::string GetUnicodeName(char32_t cp)
{
    using ICUFn = int32_t(*)(char32_t, int, char*, int32_t, int*);
    using LegacyFn = int(WINAPI*)(WORD, LPWSTR);

    // u_charName() ICU API that comes with Windows since Fall Creators Update (Version 1709 Build 16299)
    // https://docs.microsoft.com/windows/win32/intl/international-components-for-unicode--icu-
    // https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/uchar_8h.html#a2d90141097af5ad4b6c37189e7984932
    static ICUFn icu = [] {
        if (auto h = LoadLibraryW(L"icuuc.dll"))
            return reinterpret_cast<ICUFn>(GetProcAddress(h, "u_charName"));
        return (ICUFn)nullptr;
        }();

    // Undocumented API that is used in Windows "Character Map" tool
    // https://github.com/reactos/reactos/tree/master/dll/win32/getuname
    static LegacyFn legacy = [] {
        if (auto h = LoadLibraryW(L"getuname.dll"))
            return reinterpret_cast<LegacyFn>(GetProcAddress(h, "GetUName"));
        return (LegacyFn)nullptr;
        }();

    if (icu)
    {
        std::array<char, 512> buf{};
        int err = 0;

        int32_t len = icu(cp, 2 /*U_EXTENDED_CHAR_NAME*/, buf.data(),
            static_cast<int32_t>(buf.size() - 1), &err);

        if (!err)
            return std::string(buf.data(), len);
    }

    if (legacy && cp <= 0xFFFF)
    {
        std::array<wchar_t, 256> buf{};
        int len = legacy(static_cast<WORD>(cp), buf.data());

        return utf8::narrow(buf.data(), len);
    }

    if (cp > 0xFFFF)
        return "Supplementary Multilingual Plane";

    return {};
}


bool IsGraphical(char32_t cp)
{
    wchar_t buf[2];
    int len = 0;

    if (cp <= 0xFFFF)
    {
        buf[0] = static_cast<wchar_t>(cp);
        len = 1;
    }
    else
    {
        cp -= 0x10000;
        buf[0] = static_cast<wchar_t>(0xD800 + (cp >> 10));
        buf[1] = static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
        len = 2;
    }

    WORD type = 0;

    if (!GetStringTypeW(CT_CTYPE1, buf, len, &type))
        return false;

    return !(type & C1_CNTRL);
}

char32_t MakeVisibleCodepoint(char32_t cp)
{
    // U+2400 Control Pictures https://www.unicode.org/charts/PDF/U2400.pdf
    // U+308 Replacement Character
    if (cp < 0xFFFF && !IsGraphical(cp))
        return cp <= 0x21 ? cp + 0x2400 : 0xFFFD;

    return cp;
}

static char32_t DecodeUtf16(const wchar_t* s, size_t& i, size_t len)
{
    wchar_t high = s[i];

    if (IS_HIGH_SURROGATE(high) &&
        i + 1 < len &&
        IS_LOW_SURROGATE(s[i + 1]))
    {
        wchar_t low = s[++i];

        return ((char32_t(high) - 0xD800) << 10) +
            ((char32_t(low) - 0xDC00)) +
            0x10000;
    }

    return high;
}

std::string Utf32ToUtf8(char32_t cp)
{
	char buf[4];
    int len = 0;

    if (cp <= 0x7F)
	{
		buf[len++] = static_cast<char>(cp);
	}
	else if (cp <= 0x7FF)
	{
		buf[len++] = static_cast<char>(0xC0 | (cp >> 6));
        buf[len++] = static_cast<char>(0x80 | (cp & 0x3F));
	}
	else if (cp <= 0xFFFF)
    {
        buf[len++] = static_cast<char>(0xE0 | (cp >> 12));
        buf[len++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        buf[len++] = static_cast<char>(0x80 | (cp & 0x3F));
    }
	else
    {
        buf[len++] = static_cast<char>(0xF0 | (cp >> 18));
        buf[len++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        buf[len++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        buf[len++] = static_cast<char>(0x80 | (cp & 0x3F));
    }

    return std::string(buf, len);
}

std::string GetUnicodeCharacterNames(const std::string& utf8)
{
    std::wstring wstr = utf8::widen(utf8);

    std::string result;
    result.reserve(35 * wstr.size());

    for (size_t i = 0; i < wstr.size(); ++i)
    {
        char32_t cp = DecodeUtf16(wstr.data(), i, wstr.size());
        std::string glyph = Utf32ToUtf8(MakeVisibleCodepoint(cp));
        std::string name = GetUnicodeName(cp);

        if (!result.empty())
            result += ", ";

        result += std::format("{} <U+{:X} {}>", glyph, (uint32_t)cp, name);
    }

    return result;
}

std::string GetLayoutProfileId(HKL hkl)
{
    const LANGID langId = LOWORD(hkl);

    for (const auto& layout : EnumLayoutProfiles())
    {
        if ((layout.dwProfileType & LOTP_INPUTPROCESSOR) != LOTP_INPUTPROCESSOR)
            continue;

        if ((layout.dwFlags & LOT_DISABLED) == LOT_DISABLED)
            continue;

        if (layout.langid == langId)
            return utf8::narrow(layout.szId);
    }

    return std::format("{:04x}:{}", langId, GetKlidFromHkl(hkl));
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

bool GetLayoutProfile(const std::string& layoutProfileId, LAYOUTORTIPPROFILE* outProfile)
{
    std::wstring tmp = utf8::widen(layoutProfileId);

    std::vector<LAYOUTORTIPPROFILE> layouts = EnumLayoutProfiles();
    for (const auto& layout : layouts)
    {
        if (stringutils::ci_wstring(layout.szId).compare(tmp.c_str()) != 0)
            continue;

        std::memcpy(outProfile, &layout, sizeof(layout));

        return true;
    }

    return false;
}

std::string GetLayoutProfileDescription(const std::string& layoutProfileId)
{
    std::wstring tmp = utf8::widen(layoutProfileId);

    // http://archives.miloush.net/michkap/archive/2008/09/29/8968315.html
    typedef HRESULT(WINAPI* GetLayoutDescriptionFunc)(LPCWSTR szId, LPWSTR pszName, LPUINT uBufLength, DWORD dwFlags);
    static GetLayoutDescriptionFunc GetLayoutDescription = reinterpret_cast<GetLayoutDescriptionFunc>(::GetProcAddress(::LoadLibraryW(L"input.dll"), "GetLayoutDescription"));

    if (!GetLayoutDescription)
        return {};

    UINT length = 0;

    CHECK(SUCCEEDED(GetLayoutDescription(tmp.c_str(), nullptr, &length, 0)));

    std::wstring description;
    description.resize(length);

    CHECK(SUCCEEDED(GetLayoutDescription(tmp.c_str(), description.data(), &length, 0)));

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
    constexpr wchar_t regPath[] = L"Control Panel\\International\\User Profile";
    constexpr wchar_t regLanguages[] = L"Languages";
    constexpr wchar_t regTransientLangId[] = L"TransientLangId";

    LANGID langId = LOWORD(HandleToULong(hkl));

    // Try transient keyboard mapping
    if (langId >= LOCALE_TRANSIENT_KEYBOARD1 && langId <= LOCALE_TRANSIENT_KEYBOARD4)
    {
        HKEY key{};
        if (RegOpenKeyExW(HKEY_CURRENT_USER, regPath, 0, KEY_READ, &key) == ERROR_SUCCESS)
        {
            DWORD size = 0;
            if (RegGetValueW(key, nullptr, regLanguages, RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &size) == ERROR_SUCCESS)
            {
                std::vector<wchar_t> buffer(size / sizeof(wchar_t));

                if (RegGetValueW(key, nullptr, regLanguages, RRF_RT_REG_MULTI_SZ, nullptr, buffer.data(), &size) == ERROR_SUCCESS)
                {
                    for (wchar_t* lang = buffer.data(); *lang; lang += wcslen(lang) + 1)
                    {
                        DWORD regLangId{};
                        DWORD regSize = sizeof(regLangId);

                        if (RegGetValueW(key, lang, regTransientLangId, RRF_RT_REG_DWORD, nullptr, &regLangId, &regSize) == ERROR_SUCCESS &&
                            regLangId == langId)
                        {
                            RegCloseKey(key);
                            return utf8::narrow(lang);
                        }
                    }
                }
            }

            RegCloseKey(key);
        }
    }

    // Fallback to LCID → locale name
    wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};

    if (LCIDToLocaleName(MAKELCID(langId, SORT_DEFAULT), locale, LOCALE_NAME_MAX_LENGTH, 0) > 0)
        return utf8::narrow(locale);

    return {};
}

static DWORD FindKeyboardLayout(DWORD specialId)
{
    constexpr wchar_t keyboardLayoutsRegPath[] = L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts";
    constexpr wchar_t regLayoutId[] = L"Layout Id";

    HKEY key{};
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyboardLayoutsRegPath, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return specialId;

    DWORD index = 0;
    wchar_t keyName[KL_NAMELENGTH];
    DWORD keyNameLen = std::size(keyName);

    while (::RegEnumKeyExW(key, index, keyName, &keyNameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
    {
        wchar_t layoutIdBuffer[32]{};
        DWORD layoutIdBufferSize = sizeof(layoutIdBuffer);

        if (::RegGetValueW(key, keyName, regLayoutId, RRF_RT_REG_SZ, nullptr,
            layoutIdBuffer, &layoutIdBufferSize) == ERROR_SUCCESS)
        {
            DWORD layoutId = std::wcstoul(layoutIdBuffer, nullptr, 16);
            if (layoutId == specialId)
            {
                ::RegCloseKey(key);
                return std::wcstoul(keyName, nullptr, 16);
            }
        }

        keyNameLen = std::size(keyName);
        ++index;
    }

    ::RegCloseKey(key);
    return specialId;
}

std::string GetKlidFromHkl(HKL hkl)
{
    DWORD langId = LOWORD(HandleToULong(hkl));
    DWORD deviceId = HIWORD(HandleToULong(hkl));

    DWORD layoutId =
        (deviceId & 0xF000) == 0xF000
        ? FindKeyboardLayout(deviceId & 0x0FFF)
        : deviceId;

    layoutId = layoutId ? layoutId : langId;

    std::wstring klid = std::format(L"{:08X}", layoutId);

    return utf8::narrow(klid);
}

// Attempts to extract the localized keyboard layout name
// as it appears in the Windows Regional Settings on the computer.
// https://docs.microsoft.com/en-us/windows-hardware/manufacture/desktop/windows-language-pack-default-values
// It mimics GetLayoutDescription() from input.dll but lacks IME layout support
std::string GetKeyboardLayoutDisplayName(const std::string& klid)
{
    constexpr wchar_t regBase[] = L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts";
    constexpr wchar_t valueDisplayName[] = L"Layout Display Name";
    constexpr wchar_t valueText[] = L"Layout Text";

    // http://archives.miloush.net/michkap/archive/2006/05/06/591174.html
    // https://learn.microsoft.com/windows/win32/intl/using-registry-string-redirection#create-resources-for-keyboard-layout-strings
    std::wstring path = std::format(L"{}\\{}", regBase, utf8::widen(klid));

    HKEY key{};
    CHECK_EQ(RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ, &key), ERROR_SUCCESS);

    std::wstring result;

    DWORD bytes = 0;
    if (RegLoadMUIStringW(key, valueDisplayName, nullptr, 0, &bytes, 0, nullptr) == ERROR_MORE_DATA)
    {
        result.resize(bytes / sizeof(wchar_t));
        CHECK_EQ(RegLoadMUIStringW(key, valueDisplayName, result.data(), bytes, &bytes, 0, nullptr), ERROR_SUCCESS);
    }
    else
    {
        // Fallback to unlocalized layout name
        CHECK_EQ(RegGetValueW(key, nullptr, valueText, RRF_RT_REG_SZ, nullptr, nullptr, &bytes), ERROR_SUCCESS);
        result.resize(bytes / sizeof(wchar_t));
        CHECK_EQ(RegGetValueW(key, nullptr, valueText, RRF_RT_REG_SZ, nullptr, result.data(), &bytes), ERROR_SUCCESS);
    }

    RegCloseKey(key);

    return utf8::narrow(result.c_str());
}

// Attempts to extract the display name of a TSF (Text Services Framework) input profile
// as it appears in the Windows language settings.
// It mimics ITfInputProcessorProfiles::GetLanguageProfileDescription but reads directly from registry.
// https://learn.microsoft.com/en-us/windows/win32/tsf/text-services-framework
std::string GetTSFProfileDisplayName(const LCID langId, const CLSID& clsId, const GUID& profileGuid)
{
    // HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\CTF\TIP\{CLSID}\LanguageProfile\[langid]\{guidProfile}
    constexpr wchar_t regBase[] = L"SOFTWARE\\Microsoft\\CTF\\TIP";
    constexpr wchar_t valueDisplayDescription[] = L"Display Description";
    constexpr wchar_t valueDescription[] = L"Description";

    wchar_t clsIdStr[64] = {};
    CHECK(StringFromGUID2(clsId, clsIdStr, static_cast<int>(std::size(clsIdStr))) > 0);

    wchar_t profileGuidStr[64] = {};
    CHECK(StringFromGUID2(profileGuid, profileGuidStr, static_cast<int>(std::size(profileGuidStr))) > 0);

    std::wstring path = std::format(L"{}\\{}\\LanguageProfile\\0x{:08x}\\{}", regBase, clsIdStr, langId, profileGuidStr);

    HKEY key{};
    CHECK_EQ(RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ, &key), ERROR_SUCCESS);

    std::wstring result;

    DWORD bytes = 0;
    if (RegLoadMUIStringW(key, valueDisplayDescription, nullptr, 0, &bytes, 0, nullptr) == ERROR_MORE_DATA)
    {
        result.resize(bytes / sizeof(wchar_t));
        CHECK_EQ(RegLoadMUIStringW(key, valueDisplayDescription, result.data(), bytes, &bytes, 0, nullptr), ERROR_SUCCESS);
    }
    else
    {
        // Fallback to unlocalized profile name
        CHECK_EQ(RegGetValueW(key, nullptr, valueDescription, RRF_RT_REG_SZ, nullptr, nullptr, &bytes), ERROR_SUCCESS);
        result.resize(bytes / sizeof(wchar_t));
        CHECK_EQ(RegGetValueW(key, nullptr, valueDescription, RRF_RT_REG_SZ, nullptr, result.data(), &bytes), ERROR_SUCCESS);
    }

    RegCloseKey(key);

    return utf8::narrow(result.c_str());
}

std::string GetInputProfileDisplayName(const std::string& inputProfile)
{
	auto tip = stringutils::split(inputProfile, ':');
    if (tip.size() != 2)
        return {};

    if (tip[1].size() == 8)
		return GetKeyboardLayoutDisplayName(tip[1]);
    else
    {
		std::wstring tsfInputProfile = utf8::widen(tip[1]);
		constexpr size_t guidLen = 38; // including { and }

        char* langIdStrTmp = nullptr;
        LANGID langId = static_cast<LANGID>(std::strtoul(tip[0].c_str(), &langIdStrTmp, 16));

        CLSID clsId;
        CHECK_EQ(::CLSIDFromString(tsfInputProfile.substr(0, guidLen).c_str(), &clsId), NOERROR);

        GUID guid;
        CHECK_EQ(::IIDFromString(tsfInputProfile.substr(guidLen).c_str(), &guid), S_OK);


        return GetTSFProfileDisplayName(langId, clsId, guid);
    }
}

std::string GetLayoutDescription(HKL hkl)
{
    std::string languageTag = GetBcp47FromHkl(hkl);
    std::string layoutLang = GetLocaleInformation(languageTag, LOCALE_SLOCALIZEDDISPLAYNAME);

    std::string layoutProfileId = GetLayoutProfileId(hkl);
    std::string layoutProfileDisplayName = GetInputProfileDisplayName(layoutProfileId);

    return std::format("{} - {}", layoutLang, layoutProfileDisplayName);
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
    std::string languageTag = GetBcp47FromHkl(hkl);
    std::string icuLocale = GetIcuLocaleFromBcp47(languageTag);
    std::string icuLocaleMin = GetIcuMinLocaleIDFromLocaleID(icuLocale);
    std::string layoutLang = GetIcuLocaleDisplayName(icuLocaleMin);

    std::string layoutProfileId = GetLayoutProfileId(hkl);
    std::string layoutProfileDisplayName = GetInputProfileDisplayName(layoutProfileId);

    return std::format("{} - {}", layoutLang, layoutProfileDisplayName);
}

std::string GetLayoutDescriptionWinRT(HKL hkl)
{
    std::string languageTag = GetBcp47FromHklWinRT(hkl);
    std::string layoutLang = GetLanguageNameWinRT(languageTag);

    std::string layoutProfileId = GetLayoutProfileId(hkl);
	std::string layoutProfileDisplayName = GetInputProfileDisplayName(layoutProfileId);

    std::string abbreviatedName = GetAbbreviatedName2WinRT(languageTag);
    std::string currentInputLang = GetCurrentInputLanguage();

    return std::format("{} - {}", layoutLang, layoutProfileDisplayName);
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

std::unordered_map<uint32_t, uint32_t> GetUsagesToScanCodes()
{
    // Translate declared usages from HID_USAGE_PAGE_KEYBOARD page
    // See 10. Keyboard/Keypad Page (0x07) in HID Usage Tables 1.4
    std::unordered_map<uint32_t, uint32_t> table;
    constexpr USAGE minKeyboardUsage = 0x01; 
    constexpr USAGE maxKeyboardUsage = 0xe7;
    for (USAGE usage = minKeyboardUsage; usage <= maxKeyboardUsage; ++usage)
    {
        HIDP_KEYBOARD_MODIFIER_STATE modifiers{};
        uint32_t scanCode = 0;

        NTSTATUS status = HidP_TranslateUsagesToI8042ScanCodes(
            &usage,
            1,
            HidP_Keyboard_Make,
            &modifiers,
            [](PVOID context, PCHAR data, ULONG length) -> BOOLEAN
            {
                auto* scan = static_cast<uint32_t*>(context);

                std::span bytes{ reinterpret_cast<uint8_t*>(data), length };

                uint32_t value = 0;
                for (uint8_t b : bytes)
                    value = (value << 8) | b;

                *scan = value;

                return TRUE;
            },
            reinterpret_cast<PVOID>(&scanCode));

        if (status == HIDP_STATUS_SUCCESS && scanCode != 0)
        {
            uint32_t usageAndPage = (HID_USAGE_PAGE_KEYBOARD << 16) | usage;
            table.emplace(usageAndPage, scanCode);
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

    for (auto& m : additionalMappings)
        table.emplace(m.usageAndPage, m.scanCode);

    return table;
}

std::string GetScanCodeName(uint16_t scanCode)
{
    static struct
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
        { 0x0077, "LANG4"}, //
        { 0x0078, "LANG3"}, //
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
        return it->keyText;

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

    ::OutputDebugStringW(utf8::widen(std::format("{}\n", formatted.data())).c_str());
    std::printf("%s\n", formatted.data());
}
//#endif


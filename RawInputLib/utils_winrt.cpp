#include "pch.h"

#include "framework.h"

#include "utils_winrt.h"

#include "utils.h"

#include <windows.globalization.h>
#include <objidlbase.h>
#include <roapi.h>

// Undocumented - returns localized language name for a BCP-47 tag
// Used internally by Get-WinUserLanguageList (winlangdb.dll!GetLanguageNames)
std::string GetLanguageNameWinRT(const std::string& languageTag)
{
    static HMODULE winlangdb = ::LoadLibraryW(L"winlangdb.dll");
    static auto GetLanguageNames = reinterpret_cast<HRESULT(*)(LPCWSTR languageTag, LPWSTR autonym, LPWSTR englishName, LPWSTR localName, LPWSTR scriptName)>(::GetProcAddress(winlangdb, "GetLanguageNames"));

    if (!GetLanguageNames)
        return {};

    wchar_t autonym[MAX_PATH] = {};
    wchar_t englishName[MAX_PATH] = {};
    wchar_t localName[MAX_PATH] = {};
    wchar_t scriptName[MAX_PATH] = {};
    if (FAILED(GetLanguageNames(utf8::widen(languageTag).c_str(), autonym, englishName, localName, scriptName)))
        return {};

    return utf8::narrow(localName);
}

std::string GetLanguageName2WinRT(const std::string& languageTag)
{
    static HMODULE combase = ::LoadLibraryW(L"combase.dll");
    static auto RoGetActivationFactory = reinterpret_cast<decltype(&::RoGetActivationFactory)>(::GetProcAddress(combase, "RoGetActivationFactory"));
    static auto RoInitialize = reinterpret_cast<decltype(&::RoInitialize)>(::GetProcAddress(combase, "RoInitialize"));
    static auto WindowsCreateStringReference = reinterpret_cast<decltype(&::WindowsCreateStringReference)>(::GetProcAddress(combase, "WindowsCreateStringReference"));
    static auto WindowsDeleteString = reinterpret_cast<decltype(&::WindowsDeleteString)>(::GetProcAddress(combase, "WindowsDeleteString"));
    static auto WindowsGetStringRawBuffer = reinterpret_cast<decltype(&::WindowsGetStringRawBuffer)>(::GetProcAddress(combase, "WindowsGetStringRawBuffer"));

    if (!(RoGetActivationFactory && RoInitialize && WindowsCreateStringReference && WindowsDeleteString && WindowsGetStringRawBuffer))
        return {};

    if (FAILED(RoInitialize(RO_INIT_MULTITHREADED)))
        return {};

    // RAII wrapper for HSTRING allocated by WinRT (WindowsDeleteString)
    auto HStringDeleter = [&](HSTRING s) { WindowsDeleteString(s); };
    using HStringOwned = std::unique_ptr<std::remove_pointer_t<HSTRING>, decltype(HStringDeleter)>;

    // RAII wrapper for WinRT ref string (no ownership, no delete needed)
    HSTRING_HEADER hStringHeader{};
    auto MakeStringRef = [&](const std::wstring& str) -> HSTRING {
        HSTRING hString = nullptr;
        WindowsCreateStringReference(str.c_str(), static_cast<UINT32>(str.size()), &hStringHeader, &hString);
        return hString;
        };

    // RAII wrapper for COM pointers
    auto ComDeleter = [](IUnknown* p) { if (p) p->Release(); };
    using ComOwned = std::unique_ptr<IUnknown, decltype(ComDeleter)>;

    const std::wstring languageClassName = RuntimeClass_Windows_Globalization_Language;
    HSTRING hClassNameRef = MakeStringRef(languageClassName);
    if (!hClassNameRef)
        return {};

    ABI::Windows::Globalization::ILanguageFactory* rawFactory = nullptr;
    if (FAILED(RoGetActivationFactory(hClassNameRef, ABI::Windows::Globalization::IID_ILanguageFactory, reinterpret_cast<void**>(&rawFactory))))
        return {};
    ComOwned languageFactory(rawFactory, ComDeleter);

    const std::wstring languageTagWide = utf8::widen(languageTag);
    HSTRING hTagRef = MakeStringRef(languageTagWide);
    if (!hTagRef)
        return {};

    ABI::Windows::Globalization::ILanguage* rawLanguage = nullptr;
    if (FAILED(rawFactory->CreateLanguage(hTagRef, &rawLanguage)))
        return {};
    ComOwned language(rawLanguage, ComDeleter);

    HSTRING rawDisplayName = nullptr;
    if (FAILED(rawLanguage->get_DisplayName(&rawDisplayName)))
        return {};
    HStringOwned displayName(rawDisplayName, HStringDeleter);

    return utf8::narrow(WindowsGetStringRawBuffer(rawDisplayName, nullptr));
}

std::string GetAbbreviatedName2WinRT(const std::string& languageTag)
{
    static HMODULE combase = ::LoadLibraryW(L"combase.dll");
    static auto RoGetActivationFactory = reinterpret_cast<decltype(&::RoGetActivationFactory)>(::GetProcAddress(combase, "RoGetActivationFactory"));
    static auto RoInitialize = reinterpret_cast<decltype(&::RoInitialize)>(::GetProcAddress(combase, "RoInitialize"));
    static auto WindowsCreateStringReference = reinterpret_cast<decltype(&::WindowsCreateStringReference)>(::GetProcAddress(combase, "WindowsCreateStringReference"));
    static auto WindowsDeleteString = reinterpret_cast<decltype(&::WindowsDeleteString)>(::GetProcAddress(combase, "WindowsDeleteString"));
    static auto WindowsGetStringRawBuffer = reinterpret_cast<decltype(&::WindowsGetStringRawBuffer)>(::GetProcAddress(combase, "WindowsGetStringRawBuffer"));

    if (!(RoGetActivationFactory && RoInitialize && WindowsCreateStringReference && WindowsDeleteString && WindowsGetStringRawBuffer))
        return {};

    if (FAILED(RoInitialize(RO_INIT_MULTITHREADED)))
        return {};

    // RAII wrapper for HSTRING allocated by WinRT (WindowsDeleteString)
    auto HStringDeleter = [&](HSTRING s) { WindowsDeleteString(s); };
    using HStringOwned = std::unique_ptr<std::remove_pointer_t<HSTRING>, decltype(HStringDeleter)>;

    // RAII wrapper for WinRT ref string (no ownership, no delete needed)
    HSTRING_HEADER hStringHeader{};
    auto MakeStringRef = [&](const std::wstring& str) -> HSTRING {
        HSTRING hString = nullptr;
        WindowsCreateStringReference(str.c_str(), static_cast<UINT32>(str.size()), &hStringHeader, &hString);
        return hString;
        };

    // RAII wrapper for COM pointers
    auto ComDeleter = [](IUnknown* p) { if (p) p->Release(); };
    using ComOwned = std::unique_ptr<IUnknown, decltype(ComDeleter)>;

    const std::wstring languageClassName = RuntimeClass_Windows_Globalization_Language;
    HSTRING hClassNameRef = MakeStringRef(languageClassName);
    if (!hClassNameRef)
        return {};

    ABI::Windows::Globalization::ILanguageFactory* rawFactory = nullptr;
    if (FAILED(RoGetActivationFactory(hClassNameRef, ABI::Windows::Globalization::IID_ILanguageFactory, reinterpret_cast<void**>(&rawFactory))))
        return {};
    ComOwned languageFactory(rawFactory, ComDeleter);

    const std::wstring languageTagWide = utf8::widen(languageTag);
    HSTRING hTagRef = MakeStringRef(languageTagWide);
    if (!hTagRef)
        return {};

    ABI::Windows::Globalization::ILanguage* rawLanguage = nullptr;
    if (FAILED(rawFactory->CreateLanguage(hTagRef, &rawLanguage)))
        return {};
    ComOwned language(rawLanguage, ComDeleter);

    ABI::Windows::Globalization::ILanguage3* rawLanguage3 = nullptr;
    if (FAILED(rawLanguage->QueryInterface(ABI::Windows::Globalization::IID_ILanguage3, reinterpret_cast<void**>(&rawLanguage3))))
        return {};
    ComOwned language3(rawLanguage3, ComDeleter);
    

    HSTRING rawAbbreviatedName = nullptr;
    if (FAILED(rawLanguage3->get_AbbreviatedName(&rawAbbreviatedName)))
        return {};
    HStringOwned abbreviatedName(rawAbbreviatedName, HStringDeleter);

    return utf8::narrow(WindowsGetStringRawBuffer(rawAbbreviatedName, nullptr));
}

std::string GetBcp47FromHklWinRT(HKL hkl)
{
    static HMODULE combase = ::LoadLibraryW(L"combase.dll");
    static auto WindowsDeleteString = reinterpret_cast<decltype(&::WindowsDeleteString)>(::GetProcAddress(combase, "WindowsDeleteString"));
    static auto WindowsGetStringRawBuffer = reinterpret_cast<decltype(&::WindowsGetStringRawBuffer)>(::GetProcAddress(combase, "WindowsGetStringRawBuffer"));

    // Undocumented method used by Get-WinUserLanguageList
    static HMODULE bcp47langs = ::LoadLibraryW(L"bcp47langs.dll");
    static auto Bcp47FromHkl = reinterpret_cast<HRESULT(*)(HKL, HSTRING*)>(::GetProcAddress(bcp47langs, "Bcp47FromHkl"));

    if (!(WindowsDeleteString && WindowsGetStringRawBuffer && Bcp47FromHkl))
        return {};

    HSTRING hString = nullptr;
    if (FAILED(Bcp47FromHkl(hkl, &hString)))
        return {};

    auto HStringDeleter = [](HSTRING s) { WindowsDeleteString(s); };
    std::unique_ptr<std::remove_pointer_t<HSTRING>, decltype(HStringDeleter)> owned(hString, HStringDeleter);

    return utf8::narrow(WindowsGetStringRawBuffer(hString, nullptr));
}

std::string GetCurrentInputLanguage()
{
    static HMODULE bcp47langs = ::LoadLibraryW(L"ext-ms-win-globalization-input-l1-1-2.dll");
    static auto WGIGetCurrentInputLanguage = reinterpret_cast<HRESULT(*)(USHORT*, UINT64, UINT*, GUID*)>(::GetProcAddress(bcp47langs, "WGIGetCurrentInputLanguage"));

    static HMODULE combase = ::LoadLibraryW(L"combase.dll");
    static auto WindowsDeleteString = reinterpret_cast<decltype(&::WindowsDeleteString)>(::GetProcAddress(combase, "WindowsDeleteString"));
    static auto WindowsGetStringRawBuffer = reinterpret_cast<decltype(&::WindowsGetStringRawBuffer)>(::GetProcAddress(combase, "WindowsGetStringRawBuffer"));

    if (!(WGIGetCurrentInputLanguage && WindowsDeleteString && WindowsGetStringRawBuffer))
        return {};

    USHORT  outParam[4] = {};   // param_1: 4 x ushort, zeroed at start of function
    UINT    inputScope = 0;
    GUID    profileGuid{};

    if (FAILED(WGIGetCurrentInputLanguage(outParam, 0, &inputScope, &profileGuid)))
        return {};

    // param_1 might be an HSTRING written by value (8 bytes = pointer on x64)
    HSTRING hResult = *reinterpret_cast<HSTRING*>(outParam);
    if (!hResult)
        return {};

    auto HStringDeleter = [](HSTRING s) { WindowsDeleteString(s); };
    std::unique_ptr<std::remove_pointer_t<HSTRING>, decltype(HStringDeleter)> owned(hResult, HStringDeleter);

    return utf8::narrow(WindowsGetStringRawBuffer(hResult, nullptr));
}

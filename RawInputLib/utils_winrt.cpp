#include "pch.h"

#include "framework.h"

#include "utils_winrt.h"

#include "utils.h"

#include <windows.globalization.h>
#include <objidlbase.h>
#include <roapi.h>

std::string GetLanguageNameWinRT(const std::string& languageTag)
{
    HSTRING_HEADER hStringHeader;
    HSTRING hString = nullptr;
    HRESULT hr = S_OK;

    static HMODULE combase = ::LoadLibraryW(L"combase.dll");

    typedef HRESULT(*RoGetActivationFactoryFunc)(_In_ HSTRING activatableClassId, _In_ REFIID iid, _COM_Outptr_ void** factory);
    typedef HRESULT(*RoInitializeFunc)(_In_ RO_INIT_TYPE initType);
    typedef HRESULT(*WindowsCreateStringReferenceFunc)(_In_reads_opt_(length + 1) PCWSTR sourceString, UINT32 length, _Out_ HSTRING_HEADER* hstringHeader, _Outptr_result_maybenull_ _Result_nullonfailure_ HSTRING* string);
    typedef HRESULT(*WindowsDeleteStringFunc)(_In_opt_ HSTRING string);
    typedef PCWSTR(*WindowsGetStringRawBufferFunc)(_In_opt_ HSTRING string, _Out_opt_ UINT32* length);

    static RoGetActivationFactoryFunc RoGetActivationFactory = reinterpret_cast<RoGetActivationFactoryFunc>(::GetProcAddress(combase, "RoGetActivationFactory"));
    static RoInitializeFunc RoInitialize = reinterpret_cast<RoInitializeFunc>(::GetProcAddress(combase, "RoInitialize"));
    static WindowsCreateStringReferenceFunc WindowsCreateStringReference = reinterpret_cast<WindowsCreateStringReferenceFunc>(::GetProcAddress(combase, "WindowsCreateStringReference"));
    static WindowsDeleteStringFunc WindowsDeleteString = reinterpret_cast<WindowsDeleteStringFunc>(::GetProcAddress(combase, "WindowsDeleteString"));
    static WindowsGetStringRawBufferFunc WindowsGetStringRawBuffer = reinterpret_cast<WindowsGetStringRawBufferFunc>(::GetProcAddress(combase, "WindowsGetStringRawBuffer"));

    if (!(RoGetActivationFactory && RoInitialize && WindowsCreateStringReference && WindowsDeleteString && WindowsGetStringRawBuffer))
        return {};

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(hr))
        return {};

    const std::wstring languageClassName = RuntimeClass_Windows_Globalization_Language;
    hr = WindowsCreateStringReference(languageClassName.c_str(), (UINT32)languageClassName.length(), &hStringHeader, &hString);

    if (FAILED(hr) || !hString)
        return {};

    ABI::Windows::Globalization::ILanguageFactory* languageFactory = nullptr; // __x_ABI_CWindows_CGlobalization_CILanguageFactory
    const IID& languageFactoryIID = ABI::Windows::Globalization::IID_ILanguageFactory; // IID___x_ABI_CWindows_CGlobalization_CILanguageFactory

    hr = RoGetActivationFactory(hString, languageFactoryIID, (void**)&languageFactory);

    if (FAILED(hr))
        return {};

    const std::wstring languageTagWide(utf8::widen(languageTag));
    hr = WindowsCreateStringReference(languageTagWide.c_str(), (UINT32)languageTagWide.size(), &hStringHeader, &hString);

    if (FAILED(hr))
    {
        languageFactory->Release();
        return {};
    }

    ABI::Windows::Globalization::ILanguage* language = nullptr; // __x_ABI_CWindows_CGlobalization_CILanguage
    hr = languageFactory->CreateLanguage(hString, &language);

    if (FAILED(hr))
    {
        languageFactory->Release();
        return {};
    }

    HSTRING languageDisplayNameString = nullptr;
    hr = language->get_DisplayName(&languageDisplayNameString);

    if (FAILED(hr))
    {
        language->Release();
        languageFactory->Release();
        return {};
    }

    std::wstring returnString = WindowsGetStringRawBuffer(languageDisplayNameString, NULL);

    hr = WindowsDeleteString(languageDisplayNameString);
    language->Release();
    languageFactory->Release();

    return utf8::narrow(returnString);
}

std::string GetBcp47FromHklWinRT(HKL hkl)
{
    HRESULT hr = S_OK;
    HSTRING hString = nullptr;

    static HMODULE combase = ::LoadLibraryW(L"combase.dll");

    typedef HRESULT(*WindowsDeleteStringFunc)(_In_opt_ HSTRING string);
    typedef PCWSTR(*WindowsGetStringRawBufferFunc)(_In_opt_ HSTRING string, _Out_opt_ UINT32* length);

    static WindowsDeleteStringFunc WindowsDeleteString = reinterpret_cast<WindowsDeleteStringFunc>(::GetProcAddress(combase, "WindowsDeleteString"));
    static WindowsGetStringRawBufferFunc WindowsGetStringRawBuffer = reinterpret_cast<WindowsGetStringRawBufferFunc>(::GetProcAddress(combase, "WindowsGetStringRawBuffer"));

    if (!(WindowsDeleteString && WindowsGetStringRawBuffer))
        return {};

    static HMODULE bcp47langs = ::LoadLibraryW(L"bcp47langs.dll");

    // Undocumented method
    typedef HRESULT(*Bcp47FromHklFunc)(HKL hkl, HSTRING* hString);
    static Bcp47FromHklFunc Bcp47FromHkl = reinterpret_cast<Bcp47FromHklFunc>(::GetProcAddress(bcp47langs, "Bcp47FromHkl"));

    if (!Bcp47FromHkl)
        return {};

    hr = Bcp47FromHkl(hkl, &hString);
    if (FAILED(hr))
    {
        return {};
    }

    std::wstring returnString = WindowsGetStringRawBuffer(hString, NULL);
    hr = WindowsDeleteString(hString);

    return utf8::narrow(returnString);
}

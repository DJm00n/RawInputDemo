#include "framework.h"

#include "utils.h"

std::string toUtf8(std::wstring wstring)
{
    if (wstring.size() == 0)
    {
        return std::string();
    }

    int requiredLength = ::WideCharToMultiByte(
        CP_UTF8,
        0,
        reinterpret_cast<const wchar_t*>(wstring.data()),
        static_cast<int>(wstring.size()),
        nullptr,
        0,
        nullptr,
        nullptr);

    if (requiredLength <= 0)
    {
        return std::string();
    }

    std::vector<char> utf8Chars(requiredLength);

    if (::WideCharToMultiByte(
        CP_UTF8,
        0,
        reinterpret_cast<const wchar_t*>(wstring.data()),
        static_cast<int>(wstring.size()),
        utf8Chars.data(),
        static_cast<int>(utf8Chars.size()),
        nullptr,
        nullptr) == 0)
    {
        return std::string();
    }

    return std::string(begin(utf8Chars), end(utf8Chars));
}

std::wstring fromUtf8(std::string string)
{
    if (string.size() == 0)
    {
        return std::wstring();
    }

    int requiredLength = MultiByteToWideChar(CP_UTF8, 0, string.data(), static_cast<int>(string.size()), nullptr, 0);

    if (requiredLength <= 0)
    {
        return std::wstring();
    }

    std::vector<wchar_t> utf16Chars(requiredLength);

    if (MultiByteToWideChar(
        CP_UTF8,
        0,
        string.data(),
        static_cast<int>(string.size()),
        utf16Chars.data(),
        static_cast<int>(utf16Chars.size())) == 0)
    {
        return std::wstring();
    }

    return std::wstring(reinterpret_cast<wchar_t*>(utf16Chars.data()), utf16Chars.size());
}


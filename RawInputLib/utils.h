#pragma once

#pragma warning(push, 0)
#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <cwctype>
#pragma warning(pop)

// UTF8<=>UTF16 conversion functions
// recommended at http://utf8everywhere.org/#how.cvt
namespace utf8
{
    std::string narrow(const wchar_t* s, size_t nch = 0);
    std::string narrow(const std::wstring& s);

    std::wstring widen(const char* s, size_t nch = 0);
    std::wstring widen(const std::string& s);
}

namespace stringutils
{
    std::vector<std::string> split(const std::string& s, char separator);

    // case-insensitive string
    // http://www.gotw.ca/gotw/029.htm
    struct ci_char_traits : public std::char_traits<char> {
        static char to_upper(char ch) {
            return (char)std::toupper((unsigned char)ch);
        }
        static bool eq(char c1, char c2) {
            return to_upper(c1) == to_upper(c2);
        }
        static bool lt(char c1, char c2) {
            return to_upper(c1) < to_upper(c2);
        }
        static int compare(const char* s1, const char* s2, std::size_t n) {
            while (n-- != 0) {
                if (to_upper(*s1) < to_upper(*s2)) return -1;
                if (to_upper(*s1) > to_upper(*s2)) return 1;
                ++s1; ++s2;
            }
            return 0;
        }
        static const char* find(const char* s, std::size_t n, char a) {
            auto const ua(to_upper(a));
            while (n-- != 0)
            {
                if (to_upper(*s) == ua)
                    return s;
                s++;
            }
            return nullptr;
        }
    };

    typedef std::basic_string<char, ci_char_traits> ci_string;

    struct ci_wchar_traits : public std::char_traits<wchar_t> {
        static char to_upper(wchar_t ch) {
            return (char)std::towupper(ch);
        }
        static bool eq(wchar_t c1, wchar_t c2) {
            return to_upper(c1) == to_upper(c2);
        }
        static bool lt(wchar_t c1, wchar_t c2) {
            return to_upper(c1) < to_upper(c2);
        }
        static int compare(const wchar_t* s1, const wchar_t* s2, std::size_t n) {
            while (n-- != 0) {
                if (to_upper(*s1) < to_upper(*s2)) return -1;
                if (to_upper(*s1) > to_upper(*s2)) return 1;
                ++s1; ++s2;
            }
            return 0;
        }
        static const wchar_t* find(const wchar_t* s, std::size_t n, wchar_t a) {
            auto const ua(to_upper(a));
            while (n-- != 0)
            {
                if (to_upper(*s) == ua)
                    return s;
                s++;
            }
            return nullptr;
        }
    };

    typedef std::basic_string<wchar_t, ci_wchar_traits> ci_wstring;
}

inline bool IsValidHandle(void* handle)
{
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
}

struct ScopedHandleDeleter
{
    void operator()(void* handle)
    {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }
};

typedef std::unique_ptr<void, ScopedHandleDeleter> ScopedHandle;

inline ScopedHandle OpenDeviceInterface(const std::string& deviceInterface, bool readOnly = false)
{
    DWORD desired_access = readOnly ? 0 : (GENERIC_WRITE | GENERIC_READ);
    DWORD share_mode = FILE_SHARE_READ | FILE_SHARE_WRITE;

    HANDLE handle = ::CreateFileW(utf8::widen(deviceInterface).c_str(), desired_access, share_mode, 0, OPEN_EXISTING, 0, 0);

    return ScopedHandle(handle);
}

std::string GetUnicodeCharacterNames(std::string string);

typedef struct tagLAYOUTORTIPPROFILE {
    DWORD dwProfileType;
    LANGID langid;
    CLSID clsid;
    GUID guidProfile;
    GUID catid;
    DWORD dwSubstituteLayout;
    DWORD dwFlags;
    WCHAR szId[MAX_PATH];
} LAYOUTORTIPPROFILE;

// Flags used in LAYOUTORTIPPROFILE::dwProfileType
#define LOTP_INPUTPROCESSOR 1
#define LOTP_KEYBOARDLAYOUT 2

// Flags used in LAYOUTORTIPPROFILE::dwFlags.
#define LOT_DEFAULT 0x0001
#define LOT_DISABLED 0x0002

std::wstring GetLayoutProfileId(HKL hkl);

// Enumerates all enabled keyboard layouts or text services of the specified user setting
std::vector<LAYOUTORTIPPROFILE> EnumLayoutProfiles();

// Returns default layout profile set in user settings
std::wstring GetDefaultLayoutProfileId();

bool GetLayoutProfile(const std::wstring& layoutProfileId, LAYOUTORTIPPROFILE* outProfile);
std::string GetLayoutProfileDescription(const std::wstring& layoutProfileId);

std::string GetLocaleInformation(const std::string& locale, LCTYPE LCType);

// Returns IETF BCP 47 language tag from the HKL keyboard layout handle
std::string GetBcp47FromHkl(HKL hkl);

// Returns KLID string of size KL_NAMELENGTH
// Same as GetKeyboardLayoutName but for any HKL
// https://docs.microsoft.com/en-us/windows-hardware/manufacture/desktop/windows-language-pack-default-values
// https://github.com/dotnet/winforms/issues/4345#issuecomment-759161693
// 
std::string GetKlidFromHkl(HKL hkl);

// Attempts to extract the localized keyboard layout name
// as it appears in the Windows Regional Settings on the computer.
// https://docs.microsoft.com/en-us/windows-hardware/manufacture/desktop/windows-language-pack-default-values
// It mimics GetLayoutDescription() from input.dll but lacks IME layout support
std::string GetKeyboardLayoutDisplayName(const std::string& klid);

// Returns "Language - Layout" string
// Mimics GetLayoutProfileDescription API
std::string GetLayoutDescription(HKL hkl);

// Returns installed keyboard layout names
// https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/windows-language-pack-default-values
std::vector<std::string> EnumInstalledKeyboardLayouts();

// Returns UTF-8 string that will be printed on key press
std::string GetStringFromKeyPress(uint16_t scanCode);

// Get keyboard layout specific localized key name
std::string GetScanCodeName(uint16_t scanCode);

// Get the list of scan codes that are mapped to HID usages
std::vector<uint16_t> GetMappedScanCodes();

std::string VkToString(uint16_t vk);

//#ifdef _DEBUG
#define DBGPRINT(format, ...) DebugPrint(__FUNCTION__, (unsigned int)__LINE__, format, __VA_ARGS__)
VOID DebugPrint(const char* function_name, unsigned int line_number, const char* format, ...);
//#else
//#define DBGPRINT(format, ...) (void)0
//#endif

#define CHECK(x) \
  if (!(x)) LogMessageFatal(__FILE__, __LINE__).stream() << "Check failed: " #x
#define CHECK_EQ(x, y) CHECK((x) == (y))
#define CHECK_NE(x, y) CHECK((x) != (y))
#define CHECK_LE(x, y) CHECK((x) <= (y))
#define CHECK_LT(x, y) CHECK((x) < (y))
#define CHECK_GE(x, y) CHECK((x) >= (y))
#define CHECK_GT(x, y) CHECK((x) > (y))
#ifndef NDEBUG
#define DCHECK(x) CHECK(x)
#define DCHECK_EQ(x, y) CHECK_EQ(x, y)
#define DCHECK_NE(x, y) CHECK_NE(x, y)
#define DCHECK_LE(x, y) CHECK_LE(x, y)
#define DCHECK_LT(x, y) CHECK_LT(x, y)
#define DCHECK_GE(x, y) CHECK_GE(x, y)
#define DCHECK_GT(x, y) CHECK_GT(x, y)
#else  // NDEBUG
#define DCHECK(condition) \
  while (false) \
    CHECK(condition)
#define DCHECK_EQ(val1, val2) \
  while (false) \
    CHECK_EQ(val1, val2)
#define DCHECK_NE(val1, val2) \
  while (false) \
    CHECK_NE(val1, val2)
#define DCHECK_LE(val1, val2) \
  while (false) \
    CHECK_LE(val1, val2)
#define DCHECK_LT(val1, val2) \
  while (false) \
    CHECK_LT(val1, val2)
#define DCHECK_GE(val1, val2) \
  while (false) \
    CHECK_GE(val1, val2)
#define DCHECK_GT(val1, val2) \
  while (false) \
    CHECK_GT(val1, val2)
#define DCHECK_STREQ(str1, str2) \
  while (false) \
    CHECK_STREQ(str1, str2)
#endif
#define LOG_INFO LogMessage(__FILE__, __LINE__)
#define LOG(severity) LOG_ ## severity.stream()
#define LG LOG(INFO)
class LogMessage
{
public:
    LogMessage(const char* /*file*/, int /*line*/) {}
    ~LogMessage() { std::cerr << "\n"; }
    std::ostream& stream() { return std::cerr; }
private:
    LogMessage(LogMessage&) = delete;
    void operator=(LogMessage) = delete;
};
class LogMessageFatal : public LogMessage
{
public:
    LogMessageFatal(const char* file, int line)
        : LogMessage(file, line)
    {}
    ~LogMessageFatal()
    {
        std::cerr << "\n";
        std::abort();
    }
private:
    LogMessageFatal(LogMessageFatal&) = delete;
    void operator=(LogMessageFatal) = delete;
};

#pragma once

#pragma warning(push, 0)
#include <string>
#include <vector>
#include <memory>
#include <iostream>
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

    inline std::string BCDVersionToString(uint16_t bcd)
    {
        uint8_t major1 = ((bcd & (0xf << 16)) >> 16);
        uint8_t major2 = ((bcd & (0xf << 8)) >> 8);
        uint8_t minor1 = ((bcd & (0xf << 4)) >> 4);
        uint8_t minor2 = ((bcd & (0xf << 0)) >> 0);

        std::string ver;
        if (major1)
            ver += std::to_string(major1);

        ver += std::to_string(major2);
        ver.append(1, '.');
        ver += std::to_string(minor1);
        ver += std::to_string(minor2);

        return ver;
    }
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

#ifdef _DEBUG
#define DBGPRINT(format, ...) DebugPrint(__FUNCTION__, (unsigned int)__LINE__, format, __VA_ARGS__)
VOID DebugPrint(const char* function_name, unsigned int line_number, const char* format, ...);
#else
#define DBGPRINT(format, ...) (void)0
#endif

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

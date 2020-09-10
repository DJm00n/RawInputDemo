#pragma once

// UTF8<=>UTF16 conversion functions
// recommended at http://utf8everywhere.org/#how.cvt
namespace utf8
{
    std::string narrow(const wchar_t* s, size_t nch = 0);
    std::string narrow(const std::wstring& s);

    std::wstring widen(const char* s, size_t nch = 0);
    std::wstring widen(const std::string& s);
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

#ifdef _DEBUG
#define DBGPRINT(format, ...) DebugPrint(__FUNCTION__, __LINE__, format, __VA_ARGS__)
VOID DebugPrint(const char* function_name, unsigned int line_number, const char* format, ...);
#else
#define DBGPRINT(format, ...);
#endif

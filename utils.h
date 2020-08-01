#pragma once

std::string toUtf8(std::wstring wstring);
std::wstring fromUtf8(std::string string);

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



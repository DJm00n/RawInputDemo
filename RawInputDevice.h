#pragma once

class RawInputDevice
{
public:
    RawInputDevice(HANDLE handle);
    virtual ~RawInputDevice() = 0;

    static std::unique_ptr<RawInputDevice> CreateRawInputDevice(UINT type, HANDLE handle);

    virtual void OnInput(const RAWINPUT* input) = 0;

    bool IsValid() { return m_IsValid; }

protected:
    HANDLE m_Handle;
    bool m_IsValid;
};

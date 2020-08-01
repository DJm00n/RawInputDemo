#pragma once

class RawInputDevice
{
public:
    static constexpr size_t kIdLengthCap = 128;

    RawInputDevice(HANDLE handle);
    virtual ~RawInputDevice() = 0;

    static std::unique_ptr<RawInputDevice> CreateRawInputDevice(HANDLE handle);

    virtual void OnInput(const RAWINPUT* input) = 0;

    bool IsValid() { return m_IsValid; }

    std::string GetDeviceName() const { return m_Name; }

protected:
    // Fetch the device name (RIDI_DEVICENAME). Returns false on failure.
    bool QueryRawDeviceName();

    // (RIDI_DEVICEINFO). nullptr on failure.
    static bool QueryRawDeviceInfo(HANDLE handle, RID_DEVICE_INFO* deviceInfo);

    HANDLE m_Handle;
    std::string m_Name;
    bool m_IsValid;
};

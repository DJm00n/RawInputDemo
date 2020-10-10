#pragma once

#include <memory>
#include <string>

class RawInputDevice
{
public:
    static constexpr size_t kIdLengthCap = 128;

    RawInputDevice(HANDLE handle);
    virtual ~RawInputDevice() = 0;

    RawInputDevice(RawInputDevice&) = delete;
    void operator=(RawInputDevice) = delete;

    bool IsValid() { return m_IsValid; }

    std::string GetDeviceName() const { return m_Name; }

protected:
    friend class RawInputDeviceManager;

    virtual void OnInput(const RAWINPUT* input) = 0;

    // Fetch the device name (RIDI_DEVICENAME). Returns false on failure.
    bool QueryRawDeviceName();

    bool QueryDeviceName();

    // (RIDI_DEVICEINFO). nullptr on failure.
    static bool QueryRawDeviceInfo(HANDLE handle, RID_DEVICE_INFO* deviceInfo);

    HANDLE m_Handle;
    std::string m_Name;
    std::string m_Manufacturer;
    std::string m_DeviceDesc;
    bool m_IsValid;
};

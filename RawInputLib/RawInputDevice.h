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

    std::string GetDeviceInterfaceName() const { return m_DeviceInterfaceName; }

protected:
    friend class RawInputDeviceManager;

    virtual void OnInput(const RAWINPUT* input) = 0;

    virtual bool QueryDeviceInfo();

    // Fetch the device name (RIDI_DEVICENAME). Returns false on failure.
    bool QueryRawDeviceName();

    bool QueryDevNodeInfo();

    // (RIDI_DEVICEINFO). nullptr on failure.
    static bool QueryRawDeviceInfo(HANDLE handle, RID_DEVICE_INFO* deviceInfo);

    HANDLE m_Handle;
    std::string m_DeviceInterfaceName;
    std::string m_DeviceInstanceId;
    std::string m_Manufacturer;
    std::string m_FriendlyName;
    bool m_IsValid;
};

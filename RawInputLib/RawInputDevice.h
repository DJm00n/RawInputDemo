#pragma once

#include "utils.h"

#include <memory>
#include <string>

class RawInputDevice
{
    friend class RawInputDeviceManager;

public:
    virtual ~RawInputDevice() = 0;

    RawInputDevice(RawInputDevice&) = delete;
    void operator=(RawInputDevice) = delete;

    bool IsValid() { return m_IsValid; }

    // Device Interface Path. Example: `\\?\HID#VID_203A&PID_FFFC&MI_01#7&2de99099&0&0000#{378de44c-56ef-11d1-bc8c-00a0c91405dd}`
    std::string GetInterfacePath() const { return m_InterfacePath; }

    std::string GetManufacturerString() const { return m_ManufacturerString; }
    std::string GetProductString() const { return m_ProductString; }
    bool IsHidDevice() const { return !m_HidInterfacePath.empty(); }
    uint16_t GetVendorId() const { return m_VendorId; }
    uint16_t GetProductId() const { return m_ProductId; }
    uint16_t GetVersionNumber() const { return m_VersionNumber; }

protected:
    RawInputDevice(HANDLE handle);

    virtual void OnInput(const RAWINPUT* input) = 0;

    virtual bool QueryDeviceInfo();

    bool QueryRawInputDeviceInfo();
    bool QueryDeviceNodeInfo();
    bool QueryHidDeviceInfo();

    // (RIDI_DEVICEINFO). nullptr on failure.
    static bool QueryRawDeviceInfo(HANDLE handle, RID_DEVICE_INFO* deviceInfo);

    // Raw input device handle
    HANDLE m_Handle = INVALID_HANDLE_VALUE;

    std::string m_InterfacePath;
    ScopedHandle m_InterfaceHandle;

    bool m_IsReadOnlyInterface = false;
    std::string m_HidInterfacePath;

    // HID device info
    std::string m_ManufacturerString;
    std::string m_ProductString;
    std::string m_SerialNumberString;
    uint16_t m_VendorId = 0;
    uint16_t m_ProductId = 0;
    uint16_t m_VersionNumber = 0;

    // Device node info
    std::string m_DeviceInstanceId;
    std::string m_DeviceService;
    std::string m_DeviceClass;
    std::vector<std::string> m_DeviceHardwareIds;

    bool m_IsValid = false;
};

template<typename T> class RawInputDeviceFactory
{
    friend class RawInputDeviceManager;

    RawInputDeviceFactory() { }

    // TODO make it variadic template
    std::unique_ptr<RawInputDevice> Create(HANDLE handle)
    {
        return std::unique_ptr<T>(new T { handle });
    }
};

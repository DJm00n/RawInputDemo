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

    std::string GetInterfacePath() const { return m_RawInputInfo.m_InterfaceName; }

    std::string GetManufacturerString() const { return !m_HidDInfo.m_ManufacturerString.empty() ? m_HidDInfo.m_ManufacturerString : m_DeviceNodeInfo.m_Manufacturer; }
    std::string GetProductString() const { return !m_HidDInfo.m_ProductString.empty() ? m_HidDInfo.m_ProductString : m_DeviceNodeInfo.m_FriendlyName; }
    bool IsHidDevice() const { return m_DeviceNodeInfo.m_IsHidDevice; }
    uint16_t GetVendorId() const { return m_HidDInfo.m_VendorId; }
    uint16_t GetProductId() const { return m_HidDInfo.m_ProductId; }
    uint16_t GetVersionNumber() const { return m_HidDInfo.m_VersionNumber; }

protected:
    RawInputDevice(HANDLE handle);

    virtual void OnInput(const RAWINPUT* input) = 0;

    virtual bool QueryDeviceInfo();

    // (RIDI_DEVICEINFO). nullptr on failure.
    static bool QueryRawDeviceInfo(HANDLE handle, RID_DEVICE_INFO* deviceInfo);

    HANDLE m_Handle = INVALID_HANDLE_VALUE;

    struct RawInputInfo
    {
        bool QueryInfo(HANDLE handle);

        std::string m_InterfaceName;
        ScopedHandle m_InterfaceHandle;
    } m_RawInputInfo;

    struct HidDInfo
    {
        bool QueryInfo(const ScopedHandle& interfaceHandle);

        std::string m_ManufacturerString;
        std::string m_ProductString;
        std::string m_SerialNumberString;
        uint16_t m_VendorId = 0;
        uint16_t m_ProductId = 0;
        uint16_t m_VersionNumber = 0;
    } m_HidDInfo;

    struct DeviceNodeInfo
    {
        bool QueryInfo(const std::string& interfaceName);

        std::string m_DeviceInstanceId;
        std::string m_Manufacturer;
        std::string m_FriendlyName;
        std::string m_DeviceService;
        std::string m_DeviceClass;
        std::vector<std::string> m_DeviceHardwareIds;
        bool m_IsHidDevice = false;
    } m_DeviceNodeInfo;

    bool m_IsValid;
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

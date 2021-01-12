#pragma once

#include "utils.h"

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

    std::string GetInterfacePath() const { return m_RawInput.m_InterfaceName; }

    std::string GetManufacturerString() const { return !m_HidInfo.m_ManufacturerString.empty() ? m_HidInfo.m_ManufacturerString : m_DeviceNodeInfo.m_Manufacturer; }
    std::string GetProductString() const { return !m_HidInfo.m_ProductString.empty() ? m_HidInfo.m_ProductString : m_DeviceNodeInfo.m_FriendlyName; }
    uint16_t GetVendorId() const { return m_HidInfo.m_VendorId; }
    uint16_t GetProductId() const { return m_HidInfo.m_ProductId; }
    uint16_t GetVersionNumber() const { return m_HidInfo.m_VersionNumber; }

protected:
    friend class RawInputDeviceManager;

    virtual void OnInput(const RAWINPUT* input) = 0;

    virtual bool QueryDeviceInfo();

    // (RIDI_DEVICEINFO). nullptr on failure.
    static bool QueryRawDeviceInfo(HANDLE rawInputDeviceHandle, RID_DEVICE_INFO* deviceInfo);

    HANDLE m_Handle = INVALID_HANDLE_VALUE;

    struct RawInputInfo
    {
        bool QueryInfo(HANDLE rawInputDeviceHandle);

        std::string m_InterfaceName;
        ScopedHandle m_InterfaceHandle;
    } m_RawInput;

    struct HIDInfo
    {
        bool QueryInfo(const ScopedHandle& interfaceHandle);

        std::string m_ManufacturerString;
        std::string m_ProductString;
        std::string m_SerialNumberString;
        uint16_t m_VendorId = 0;
        uint16_t m_ProductId = 0;
        uint16_t m_VersionNumber = 0;
    } m_HidInfo;

    struct DeviceNodeInfo
    {
        bool QueryInfo(const std::string& interfaceName);

        std::string m_InstanceId;
        std::string m_Manufacturer;
        std::string m_FriendlyName;
        std::string m_DeviceService;
        std::string m_DeviceClass;
    } m_DeviceNodeInfo;

    bool m_IsValid;
};

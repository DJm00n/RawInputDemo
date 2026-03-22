#pragma once

#include "utils.h"

#include "RawInputDeviceFactory.h"

#include "UsbDevice.h"

class RawInputDevice
{
    friend class RawInputDeviceManager;

public:
    virtual ~RawInputDevice() = 0;

    RawInputDevice(RawInputDevice&) = delete;
    void operator=(RawInputDevice) = delete;

    bool IsValid() { return m_IsValid; }

    virtual uint32_t GetType() const = 0;
    virtual void OnInputLanguageChanged(HKL /*hkl*/) {}

    // Device Interface Path. Example: `\\?\HID#VID_203A&PID_FFFC&MI_01#7&2de99099&0&0000#{378de44c-56ef-11d1-bc8c-00a0c91405dd}`
    std::string GetInterfacePath() const { return m_InterfacePath; }

    const std::string& GetManufacturerString() const { return m_Identity.manufacturer; }
    const std::string& GetProductString()      const { return m_Identity.product; }
    const std::string& GetSerialNumberString() const { return m_Identity.serial; }
    uint16_t GetVendorId()      const { return m_Identity.vendorId; }
    uint16_t GetProductId()     const { return m_Identity.productId; }
    uint16_t GetVersionNumber() const { return m_Identity.versionNumber; }

    bool IsXInputDevice()     const { return m_XboxInfo && !m_XboxInfo->xInputInterfacePath.empty(); }
    uint8_t GetXInputUserIndex() const { return m_XboxInfo ? m_XboxInfo->xInputUserIndex : 0xff; }
    bool IsXboxGipDevice()    const { return m_XboxInfo && !m_XboxInfo->gipInterfacePath.empty(); }
    bool IsBluetoothLEDevice() const { return m_BleInfo.has_value(); }

    bool IsUsbDevice() const { return m_UsbInfo && !m_UsbInfo->m_DeviceInterfacePath.empty(); }

    std::string GetUsbInterfacePath() const { return m_UsbInfo->m_DeviceInterfacePath; }

    bool IsHidDevice() const { return m_HidInfo && !m_HidInfo->hidInterfacePath.empty(); }
    std::string GetHidInterfacePath() const { return m_HidInfo->hidInterfacePath; }

    const std::vector<uint8_t>& GetUsbConfigurationDescriptor() const { return m_UsbInfo->m_ConfigurationDescriptor; }
    const std::vector<uint8_t>& GetUsbHidReportDescriptor() const { return m_UsbInfo->m_HidReportDescriptor; }

protected:
    RawInputDevice(HANDLE handle);

    virtual void OnInput(const RAWINPUT* input) = 0;

    virtual bool QueryDeviceInfo();

    bool QueryRawInputDeviceInfo();
    
    void TryQueryDeviceNodeInfo();
    void TryQueryUsbInfo();
    void TryQueryXboxInfo();
    void TryQueryBluetoothLEInfo();

    void ResolveIdentity();

    // (RIDI_DEVICEINFO). nullptr on failure.
    static bool QueryRawDeviceInfo(HANDLE handle, RID_DEVICE_INFO* deviceInfo);
	static std::string QueryRawDeviceInterfacePath(HANDLE handle);

    // Raw input device handle
    HANDLE m_Handle = INVALID_HANDLE_VALUE;

    std::string m_InterfacePath;
    bool m_IsInterfaceReadOnly = false;

    struct DeviceIdentity
    {
        std::string manufacturer;
        std::string product;
        std::string serial;
        uint16_t    vendorId = 0;
        uint16_t    productId = 0;
        uint16_t    versionNumber = 0;
    };

    struct DeviceNodeInfo
    {
        std::string              instanceId;
        std::string              manufacturer;
        std::string              displayName;
        std::string              service;
        std::string              deviceClass;
        std::vector<std::string> stack;
        std::vector<std::string> hardwareIds;
        GUID                     busTypeGuid{};
    };

    struct HidDeviceInfo
    {
        std::string hidInterfacePath;
    };

    struct XboxInfo
    {
        std::string xInputInterfacePath;
        uint8_t     xInputUserIndex = 0xff;
        std::string gipInterfacePath;
        std::string gipSerial;
    };

    // Bluetooth LE
    struct BluetoothLEInfo
    {
        std::string interfacePath;
        std::string manufacturer;
        std::string product;
        std::string address;
        uint16_t    vendorId = 0;
        uint16_t    productId = 0;
        uint16_t    versionNumber = 0;

    };

    DeviceIdentity                 m_Identity;
    std::optional<DeviceNodeInfo>  m_DevNode;
    std::optional<UsbDeviceInfo>   m_UsbInfo;
	std::optional<HidDeviceInfo>   m_HidInfo;
    std::optional<XboxInfo>        m_XboxInfo;
    std::optional<BluetoothLEInfo> m_BleInfo;

    bool m_IsValid = false;
};

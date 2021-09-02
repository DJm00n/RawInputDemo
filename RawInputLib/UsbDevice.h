#pragma once

#include <string>

class UsbDeviceInfo
{
public:
    UsbDeviceInfo(const std::string& deviceInstanceId);

    UsbDeviceInfo(UsbDeviceInfo&) = delete;
    void operator=(UsbDeviceInfo) = delete;

public:
    std::string m_DeviceInstanceId;

    std::string m_UsbDeviceInstanceId;
    std::string m_UsbDeviceInterfacePath;

    uint16_t m_VendorId = 0;
    uint16_t m_ProductId = 0;
    uint16_t m_VersionNumber = 0;

    std::string m_Manufacturer;
    std::string m_Product;
    std::string m_SerialNumber;

    std::vector<uint8_t> m_ConfigurationDescriptor;

    std::string m_UsbCompositeDeviceInstanceId;
    UCHAR m_UsbInterfaceNumber = 0;

    std::vector<uint8_t> m_HidReportDescriptor;

    std::string m_UsbHubInterfacePath;
    ScopedHandle m_UsbHubHandle;
    ULONG m_ConnectionIndex;

};
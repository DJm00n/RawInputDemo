#include "pch.h"

#include "UsbDevice.h"

#include "CfgMgr32Wrapper.h"

#pragma warning(push, 0)
#include <initguid.h>
#pragma warning(pop)

#include <winioctl.h>
#include <usbioctl.h>

namespace
{
    #pragma pack(1)
    typedef struct _HID_DESCRIPTOR
    {
        UCHAR   bLength;
        UCHAR   bDescriptorType;
        USHORT  bcdHID;
        UCHAR   bCountry;
        UCHAR   bNumDescriptors;

        struct _HID_DESCRIPTOR_DESC_LIST
        {
            UCHAR   bReportType;
            USHORT  wReportLength;
        } DescriptorList[1];

    } HID_DESCRIPTOR, * PHID_DESCRIPTOR;

    // Codes for HID-specific descriptor types
    constexpr UCHAR HID_HID_DESCRIPTOR_TYPE = 0x21;
    constexpr UCHAR HID_REPORT_DESCRIPTOR_TYPE = 0x22;

    std::unique_ptr<uint8_t[]> GetDescriptor(const ScopedHandle& usbHubHandle, ULONG usbPortIndex, UCHAR descriptorType, USHORT descriptorSize, UCHAR descriptorIndex, USHORT descriptorParam)
    {
        if (!IsValidHandle(usbHubHandle.get()))
            return nullptr;

        std::vector<uint8_t> buffer(sizeof(USB_DESCRIPTOR_REQUEST) + descriptorSize, 0);

        PUSB_DESCRIPTOR_REQUEST request = reinterpret_cast<PUSB_DESCRIPTOR_REQUEST>(buffer.data());

        // Indicate the port from which the descriptor will be requested
        request->ConnectionIndex = usbPortIndex;

        request->SetupPacket.bmRequest = 0x80; // Endpoint_In
        request->SetupPacket.bRequest = 0x06; // Get_Descriptor
        request->SetupPacket.wValue = (descriptorType << 8) | descriptorIndex;
        request->SetupPacket.wIndex = descriptorParam;
        request->SetupPacket.wLength = descriptorSize;

        // According to HID spec we need to do Report Descriptor request with
        // bmRequest set to 0x81 (Interface_In) but
        // seems IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION overrides this :(
        if (descriptorType == HID_REPORT_DESCRIPTOR_TYPE)
            request->SetupPacket.bmRequest = 0x81 /*Interface_In*/;

        ULONG writtenSize = 0;

        if (!::DeviceIoControl(usbHubHandle.get(),
            IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            &writtenSize,
            nullptr))
        {
            DBGPRINT("DeviceIoControl(IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION) failed. GetLastError() = 0x%04x", ::GetLastError());
            return nullptr;
        }

        if (descriptorSize < (writtenSize - sizeof(USB_DESCRIPTOR_REQUEST)))
            return nullptr;

        writtenSize -= sizeof(USB_DESCRIPTOR_REQUEST);

        std::unique_ptr<uint8_t[]> retBuffer(std::make_unique<uint8_t[]>(writtenSize));
        std::memcpy(retBuffer.get(), request->Data, writtenSize);

        return retBuffer;
    }

    bool GetDeviceDescriptor(const ScopedHandle& usbHubHandle, ULONG connectionIndex, USB_DEVICE_DESCRIPTOR& outDeviceDescriptor)
    {
        std::unique_ptr<uint8_t[]> buffer = GetDescriptor(usbHubHandle, connectionIndex, USB_DEVICE_DESCRIPTOR_TYPE, sizeof(USB_DEVICE_DESCRIPTOR), 0, 0);
        if (!buffer)
            return false;

        const PUSB_COMMON_DESCRIPTOR commonDescriptor = reinterpret_cast<PUSB_COMMON_DESCRIPTOR>(buffer.get());

        if (commonDescriptor->bDescriptorType != USB_DEVICE_DESCRIPTOR_TYPE)
            return false;

        std::memcpy(&outDeviceDescriptor, commonDescriptor, commonDescriptor->bLength);

        return true;
    }

    bool GetConfigurationDescriptor(const ScopedHandle& usbHubHandle, ULONG connectionIndex, UCHAR configurationIndex, USB_CONFIGURATION_DESCRIPTOR& outConfigurationDescriptor)
    {
        std::unique_ptr<uint8_t[]> buffer = GetDescriptor(usbHubHandle, connectionIndex, USB_CONFIGURATION_DESCRIPTOR_TYPE, sizeof(USB_CONFIGURATION_DESCRIPTOR), configurationIndex, 0);
        if (!buffer)
            return false;

        const PUSB_COMMON_DESCRIPTOR commonDescriptor = reinterpret_cast<PUSB_COMMON_DESCRIPTOR>(buffer.get());

        if (commonDescriptor->bDescriptorType != USB_CONFIGURATION_DESCRIPTOR_TYPE)
            return false;

        std::memcpy(&outConfigurationDescriptor, commonDescriptor, commonDescriptor->bLength);

        return true;
    }

    bool GetFullConfigurationDescriptor(const ScopedHandle& usbHubHandle, ULONG connectionIndex, UCHAR configurationIndex, std::vector<uint8_t>& outConfigurationDescriptor)
    {
        USB_CONFIGURATION_DESCRIPTOR configurationDescriptor;
        if (!GetConfigurationDescriptor(usbHubHandle, connectionIndex, configurationIndex, configurationDescriptor))
            return false;

        const USHORT size = configurationDescriptor.wTotalLength;
        std::unique_ptr<uint8_t[]> buffer = GetDescriptor(usbHubHandle, connectionIndex, USB_CONFIGURATION_DESCRIPTOR_TYPE, size, configurationIndex, 0);

        const PUSB_COMMON_DESCRIPTOR commonDescriptor = reinterpret_cast<PUSB_COMMON_DESCRIPTOR>(buffer.get());

        if (commonDescriptor->bDescriptorType != USB_CONFIGURATION_DESCRIPTOR_TYPE)
            return false;

        outConfigurationDescriptor.assign(buffer.get(), buffer.get() + size);

        return true;
    }

    bool GetDeviceString(const ScopedHandle& usbHubHandle, ULONG connectionIndex, UCHAR stringIndex, USHORT languageID, std::wstring& outString)
    {
        if (!stringIndex && languageID)
            return false;

        std::unique_ptr<uint8_t[]> buffer = GetDescriptor(usbHubHandle, connectionIndex, USB_STRING_DESCRIPTOR_TYPE, MAXIMUM_USB_STRING_LENGTH, stringIndex, languageID);
        if (!buffer)
            return false;

        const PUSB_STRING_DESCRIPTOR stringDescriptor = reinterpret_cast<PUSB_STRING_DESCRIPTOR>(buffer.get());
        const size_t count = (stringDescriptor->bLength - sizeof(USB_COMMON_DESCRIPTOR)) / sizeof(WCHAR);

        if (!count)
            return false;

        outString.assign(stringDescriptor->bString, count);

        return true;
    }

    bool SearchInterfaceDescriptor(std::vector<uint8_t>& descriptorData, UCHAR interfaceNumber, PUSB_INTERFACE_DESCRIPTOR& outInterfaceDescriptor)
    {
        for (size_t offset = 0; offset < descriptorData.size(); offset += reinterpret_cast<PUSB_COMMON_DESCRIPTOR>(descriptorData.data() + offset)->bLength)
        {
            PUSB_COMMON_DESCRIPTOR commonDesc = reinterpret_cast<PUSB_COMMON_DESCRIPTOR>(descriptorData.data() + offset);
            if (commonDesc->bDescriptorType != USB_INTERFACE_DESCRIPTOR_TYPE)
                continue;

            PUSB_INTERFACE_DESCRIPTOR interfaceDescriptor = reinterpret_cast<PUSB_INTERFACE_DESCRIPTOR>(commonDesc);
            if (interfaceDescriptor->bInterfaceNumber != interfaceNumber)
                continue;

            outInterfaceDescriptor = interfaceDescriptor;

            return true;
        }

        return false;
    }

    bool GetHidDescriptor(PUSB_INTERFACE_DESCRIPTOR interfaceDescriptor, PHID_DESCRIPTOR& outHidDescriptor)
    {
        // https://www.usb.org/document-library/device-class-definition-hid-111
        // 7.1  Standard Requests
        // When a Get_Descriptor(Configuration) request is issued, it
        // returns the Configuration descriptor, all Interface descriptors, all Endpoint
        // descriptors, and the HID descriptor for each interface. It shall not return the
        // String descriptor, HID Report descriptor or any of the optional HID class
        // descriptors. The HID descriptor shall be interleaved between the Interface and
        // Endpoint descriptors for HID Interfaces. That is, the order shall be:
        //
        // Configuration descriptor
        // Interface descriptor (specifying HID Class)
        //     HID descriptor (associated with above Interface)
        //         Endpoint descriptor (for HID Interrupt In Endpoint)
        //         Optional Endpoint descriptor (for HID Interrupt Out Endpoint)

        if (interfaceDescriptor->bInterfaceClass != USB_DEVICE_CLASS_HUMAN_INTERFACE)
            return false;

        // 4.2  Subclass
        // 0       No Subclass
        // 1       Boot Interface Subclass
        // 2 - 255 Reserved
        //if (interfaceDescriptor->bInterfaceSubClass != 0)
        //    return false;

        // 4.3  Protocols
        // 0       None
        // 1       Keyboard
        // 2       Mouse
        // 3 - 255 Reserved
        //if (interfaceDescriptor->bInterfaceProtocol != 0)
        //    return false;

        PHID_DESCRIPTOR hidDescriptor = reinterpret_cast<PHID_DESCRIPTOR>(reinterpret_cast<uint8_t*>(interfaceDescriptor) + interfaceDescriptor->bLength);

        CHECK_EQ(hidDescriptor->bLength, sizeof(HID_DESCRIPTOR));
        CHECK_EQ(hidDescriptor->bDescriptorType, HID_HID_DESCRIPTOR_TYPE);

        CHECK_GE(hidDescriptor->bNumDescriptors, 1);

        CHECK_EQ(hidDescriptor->DescriptorList[0].bReportType, HID_REPORT_DESCRIPTOR_TYPE);
        CHECK_NE(hidDescriptor->DescriptorList[0].wReportLength, 0);

        outHidDescriptor = hidDescriptor;

        return true;
    }

    bool GetHidReportDescriptor(const ScopedHandle& usbHubHandle, ULONG connectionIndex, USHORT reportLength, UCHAR interfaceNumber, std::vector<uint8_t>& outReportDescriptor)
    {
        std::unique_ptr<uint8_t[]> buffer = GetDescriptor(usbHubHandle, connectionIndex, HID_REPORT_DESCRIPTOR_TYPE, reportLength, 0, interfaceNumber);
        if (!buffer)
            return false;

        outReportDescriptor.assign(buffer.get(), buffer.get() + reportLength);

        return true;
    }

    // According to MSDN the instance IDs for the device nodes created by the
    // composite driver is in the form "USB\VID_vvvv&PID_dddd&MI_zz" where "zz"
    // is the interface number extracted from bInterfaceNumber field of the interface descriptor.
    // https://docs.microsoft.com/windows-hardware/drivers/install/standard-usb-identifiers#multiple-interface-usb-devices
    bool GetInterfaceNumber(const std::string& deviceInstanceId, UCHAR& outInterfaceNumber)
    {
        static const std::string interfaceToken("MI_");
        stringutils::ci_string tmp(deviceInstanceId.data(), deviceInstanceId.size());
        size_t pos = tmp.find(interfaceToken.c_str());
        if (pos == stringutils::ci_string::npos)
            return false;

        std::string interfaceNumberStr = deviceInstanceId.substr(pos + interfaceToken.size(), 2);
        long interfaceNumber = std::stol(interfaceNumberStr, &pos, 16);
        if (pos != 2)
            return false;

        outInterfaceNumber = static_cast<UCHAR>(interfaceNumber);

        return true;
    }
}

UsbDeviceInfo::UsbDeviceInfo(const std::string& hidDeviceInstanceId)
{
    m_DeviceInterfacePath = SearchParentDeviceInterface(hidDeviceInstanceId, &GUID_DEVINTERFACE_USB_DEVICE);
    m_DeviceInstanceId = GetDeviceFromInterface(m_DeviceInterfacePath);

    DEVINST devNodeHandle = OpenDevNode(m_DeviceInstanceId);

    // open handle to wake up device from S3 suspend state
    ScopedHandle usbInterfaceHandle = OpenDeviceInterface(m_DeviceInterfacePath);

    // device index in parent USB hub
    // https://docs.microsoft.com/windows-hardware/drivers/ddi/wdm/ns-wdm-_device_capabilities#usb
    m_UsbPortIndex = PropertyDataCast<ULONG>(GetDevNodeProperty(devNodeHandle, &DEVPKEY_Device_Address, DEVPROP_TYPE_UINT32));

    std::vector<std::string> usbDeviceCompatibleIds = PropertyDataCast<std::vector<std::string>>(GetDevNodeProperty(devNodeHandle, &DEVPKEY_Device_CompatibleIds, DEVPROP_TYPE_STRING_LIST));
    for (const std::string& usbCompatibleId : usbDeviceCompatibleIds)
    {
        stringutils::ci_string tmp(usbCompatibleId.data(), usbCompatibleId.size());
        // https://docs.microsoft.com/windows-hardware/drivers/usbcon/enumeration-of-the-composite-parent-device
        if (tmp.find("USB\\COMPOSITE") == stringutils::ci_string::npos)
            continue;

        // Its Composite USB device
        // Need to acquire interface number in parent USB device
        // https://docs.microsoft.com/windows-hardware/drivers/usbcon/usb-common-class-generic-parent-driver
        m_UsbCompositeDeviceInstanceId = GetParentDevice(hidDeviceInstanceId);
        if (!GetInterfaceNumber(m_UsbCompositeDeviceInstanceId, m_UsbInterfaceNumber))
        {
            // Try again
            m_UsbCompositeDeviceInstanceId = GetParentDevice(m_UsbCompositeDeviceInstanceId);
            if (!GetInterfaceNumber(m_UsbCompositeDeviceInstanceId, m_UsbInterfaceNumber))
            {
                DBGPRINT("UsbDevice: cannot get interface number");
                return;
            }
        }
        break;
    }

    std::string usbHubInterface = SearchParentDeviceInterface(m_DeviceInstanceId, &GUID_DEVINTERFACE_USB_HUB);

    if (usbHubInterface.empty())
        return;

    ScopedHandle hubInterfaceHandle = OpenDeviceInterface(usbHubInterface, true);

    USB_DEVICE_DESCRIPTOR deviceDescriptor;
    if (!GetDeviceDescriptor(hubInterfaceHandle, m_UsbPortIndex, deviceDescriptor))
        return;

    m_VendorId = deviceDescriptor.idVendor;
    m_ProductId = deviceDescriptor.idProduct;
    m_VersionNumber = deviceDescriptor.bcdDevice;

    // Assume that we are always using first configuration
    const UCHAR configurationIndex = 0;
    if (!GetFullConfigurationDescriptor(hubInterfaceHandle, m_UsbPortIndex, configurationIndex, m_ConfigurationDescriptor))
        return;

    // Search for interface descriptor
    PUSB_INTERFACE_DESCRIPTOR interfaceDescriptor = nullptr;
    if (!SearchInterfaceDescriptor(m_ConfigurationDescriptor, m_UsbInterfaceNumber, interfaceDescriptor))
        return;

    std::wstring stringBuffer;
    // Get the array of supported Language IDs, which is returned in String Descriptor 0
    if (!GetDeviceString(hubInterfaceHandle, m_UsbPortIndex, 0, 0, stringBuffer))
        return;

    // Use first supported language
    USHORT languageID = stringBuffer[0];
    if (GetDeviceString(hubInterfaceHandle, m_UsbPortIndex, deviceDescriptor.iManufacturer, languageID, stringBuffer))
        m_Manufacturer = utf8::narrow(stringBuffer);

    // Get interface name instead of whole product name, if present
    UCHAR productStringIndex = interfaceDescriptor->iInterface ? interfaceDescriptor->iInterface : deviceDescriptor.iProduct;
    if (GetDeviceString(hubInterfaceHandle, m_UsbPortIndex, productStringIndex, languageID, stringBuffer))
        m_Product = utf8::narrow(stringBuffer);

    if (GetDeviceString(hubInterfaceHandle, m_UsbPortIndex, deviceDescriptor.iSerialNumber, languageID, stringBuffer))
        m_SerialNumber = utf8::narrow(stringBuffer);

    // Get HID Descriptor
    PHID_DESCRIPTOR hidDescriptor = nullptr;
    if (!GetHidDescriptor(interfaceDescriptor, hidDescriptor))
        return;

    // Get raw HID Report Descriptor
    if (!GetHidReportDescriptor(hubInterfaceHandle, m_UsbPortIndex, hidDescriptor->DescriptorList[0].wReportLength, interfaceDescriptor->bInterfaceNumber, m_HidReportDescriptor))
        DBGPRINT("UsbDevice: cannot get raw HID Report Descriptor");
}
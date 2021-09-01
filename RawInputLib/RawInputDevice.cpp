#include "pch.h"
#include "framework.h"

#include "RawInputDevice.h"

#include "SetupApiWrapper.h"

#pragma warning(push, 0)
#include <hidsdi.h> // HidD_* API

#include <initguid.h>
#pragma warning(pop)

#include <winioctl.h>
#include <usbioctl.h>

RawInputDevice::RawInputDevice(HANDLE handle)
    : m_Handle(handle)
    , m_IsValid(false)
{}

RawInputDevice::~RawInputDevice() = default;

bool RawInputDevice::QueryDeviceInfo()
{
    if (!QueryRawInputDeviceInfo())
        return false;

    if (IsValidHandle(m_InterfaceHandle.get()) && !QueryDeviceNodeInfo())
        return false;

    if (QueryUsbDeviceInterface() && !QueryUsbDeviceInfo())
    {
        DBGPRINT("Cannot get USB device info from '%s' interface.", m_UsbDeviceInterface.c_str());
        return false;
    }

    // optional HID device info
    if (IsHidDevice() && !QueryHidDeviceInfo())
    {
        DBGPRINT("Cannot get HID info from '%s' interface.", m_InterfacePath.c_str());
        return false;
    }

    return true;
}

bool RawInputDevice::QueryRawInputDeviceInfo()
{
    DCHECK(IsValidHandle(m_Handle));

    UINT size = 0;

    UINT result = ::GetRawInputDeviceInfoW(m_Handle, RIDI_DEVICENAME, nullptr, &size);
    if (result == static_cast<UINT>(-1))
    {
        //PLOG(ERROR) << "GetRawInputDeviceInfo() failed";
        return false;
    }
    DCHECK_EQ(0u, result);

    std::wstring buffer(size, 0);
    result = ::GetRawInputDeviceInfoW(m_Handle, RIDI_DEVICENAME, buffer.data(), &size);
    if (result == static_cast<UINT>(-1))
    {
        //PLOG(ERROR) << "GetRawInputDeviceInfo() failed";
        return false;
    }
    DCHECK_EQ(size, result);

    m_InterfacePath = utf8::narrow(buffer);

    m_InterfaceHandle = OpenDeviceInterface(m_InterfacePath);

    if (!IsValidHandle(m_InterfaceHandle.get()))
    {
        /* System devices, such as keyboards and mice, cannot be opened in
        read-write mode, because the system takes exclusive control over
        them.  This is to prevent keyloggers.  However, feature reports
        can still be sent and received.  Retry opening the device, but
        without read/write access. */
        m_InterfaceHandle = OpenDeviceInterface(m_InterfacePath, true);
        if (IsValidHandle(m_InterfaceHandle.get()))
            m_IsReadOnlyInterface = true;
    }

    return !m_InterfacePath.empty();
}

bool RawInputDevice::QueryDeviceNodeInfo()
{
    DCHECK(!m_InterfacePath.empty());

    // TODO implement fHasSpecificHardwareMatch from DirectInput code

    m_DeviceInstanceId = GetDeviceFromInterface(m_InterfacePath);

    DEVINST devNodeHandle = OpenDevNode(m_DeviceInstanceId);

    m_ManufacturerString = PropertyDataCast<std::string>(GetDevNodeProperty(devNodeHandle, &DEVPKEY_Device_Manufacturer, DEVPROP_TYPE_STRING));
    m_ProductString = PropertyDataCast<std::string>(GetDevNodeProperty(devNodeHandle, &DEVPKEY_NAME, DEVPROP_TYPE_STRING));
    m_DeviceService = PropertyDataCast<std::string>(GetDevNodeProperty(devNodeHandle, &DEVPKEY_Device_Service, DEVPROP_TYPE_STRING));
    m_DeviceClass = PropertyDataCast<std::string>(GetDevNodeProperty(devNodeHandle, &DEVPKEY_Device_Class, DEVPROP_TYPE_STRING));

    // TODO extract VID/PID/COL from hardwareId
    m_DeviceHardwareIds = PropertyDataCast<std::vector<std::string>>(GetDevNodeProperty(devNodeHandle, &DEVPKEY_Device_HardwareIds, DEVPROP_TYPE_STRING_LIST));

    GUID hid_guid;
    ::HidD_GetHidGuid(&hid_guid);

    m_HidInterfacePath = SearchParentDeviceInterface(m_DeviceInstanceId, &hid_guid);

    return !m_ProductString.empty() || !m_ManufacturerString.empty();
}

bool RawInputDevice::QueryUsbDeviceInterface()
{
    m_UsbDeviceInterface = SearchParentDeviceInterface(m_DeviceInstanceId, &GUID_DEVINTERFACE_USB_DEVICE);

    return !m_UsbDeviceInterface.empty();
}

namespace
{
    std::unique_ptr<uint8_t[]> GetUsbDescriptor(const ScopedHandle& usbHubHandle, ULONG connectionIndex, UCHAR descriptorType, USHORT descriptorSize, UCHAR descriptorIndex, USHORT descriptorParam)
    {
        if (!IsValidHandle(usbHubHandle.get()))
            return nullptr;

        std::vector<uint8_t> buffer(sizeof(USB_DESCRIPTOR_REQUEST) + descriptorSize, 0);

        PUSB_DESCRIPTOR_REQUEST request = reinterpret_cast<PUSB_DESCRIPTOR_REQUEST>(buffer.data());

        // Indicate the port from which the descriptor will be requested
        request->ConnectionIndex = connectionIndex;

        request->SetupPacket.bmRequest = 0x80; // Endpoint_In
        request->SetupPacket.bRequest = 0x06; // Get_Descriptor
        request->SetupPacket.wValue = (descriptorType << 8) | descriptorIndex;
        request->SetupPacket.wIndex = descriptorParam;
        request->SetupPacket.wLength = descriptorSize;

        if (descriptorType == 0x22 /*HID_REPORT_DESCRIPTOR_TYPE*/)
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

    bool GetUsbDeviceDescriptor(const ScopedHandle& usbHubHandle, ULONG connectionIndex, USB_DEVICE_DESCRIPTOR& outDeviceDescriptor)
    {
        std::unique_ptr<uint8_t[]> buffer = GetUsbDescriptor(usbHubHandle, connectionIndex, USB_DEVICE_DESCRIPTOR_TYPE, sizeof(USB_DEVICE_DESCRIPTOR), 0, 0);
        if (!buffer)
            return false;

        const PUSB_COMMON_DESCRIPTOR commonDescriptor = reinterpret_cast<PUSB_COMMON_DESCRIPTOR>(buffer.get());

        if (commonDescriptor->bDescriptorType != USB_DEVICE_DESCRIPTOR_TYPE)
            return false;

        std::memcpy(&outDeviceDescriptor, commonDescriptor, commonDescriptor->bLength);

        return true;
    }

    bool GetUsbConfigurationDescriptor(const ScopedHandle& usbHubHandle, ULONG connectionIndex, UCHAR configurationIndex, USB_CONFIGURATION_DESCRIPTOR& outConfigurationDescriptor)
    {
        std::unique_ptr<uint8_t[]> buffer = GetUsbDescriptor(usbHubHandle, connectionIndex, USB_CONFIGURATION_DESCRIPTOR_TYPE, sizeof(USB_CONFIGURATION_DESCRIPTOR), configurationIndex, 0);
        if (!buffer)
            return false;

        const PUSB_COMMON_DESCRIPTOR commonDescriptor = reinterpret_cast<PUSB_COMMON_DESCRIPTOR>(buffer.get());

        if (commonDescriptor->bDescriptorType != USB_CONFIGURATION_DESCRIPTOR_TYPE)
            return false;

        std::memcpy(&outConfigurationDescriptor, commonDescriptor, commonDescriptor->bLength);

        return true;
    }

    bool GetFullUsbConfigurationDescriptor(const ScopedHandle& usbHubHandle, ULONG connectionIndex, UCHAR configurationIndex, std::vector<uint8_t>& outConfigurationDescriptor)
    {
        USB_CONFIGURATION_DESCRIPTOR configurationDescriptor;
        if (!GetUsbConfigurationDescriptor(usbHubHandle, connectionIndex, configurationIndex, configurationDescriptor))
            return false;

        const USHORT size = configurationDescriptor.wTotalLength;
        std::unique_ptr<uint8_t[]> buffer = GetUsbDescriptor(usbHubHandle, connectionIndex, USB_CONFIGURATION_DESCRIPTOR_TYPE, size, configurationIndex, 0);

        const PUSB_COMMON_DESCRIPTOR commonDescriptor = reinterpret_cast<PUSB_COMMON_DESCRIPTOR>(buffer.get());

        if (commonDescriptor->bDescriptorType != USB_CONFIGURATION_DESCRIPTOR_TYPE)
            return false;

        outConfigurationDescriptor.assign(buffer.get(), buffer.get() + size);

        return true;
    }

    bool GetUsbDeviceString(const ScopedHandle& usbHubHandle, ULONG connectionIndex, UCHAR stringIndex, USHORT languageID, std::wstring& outString)
    {
        if (!stringIndex && languageID)
            return false;

        std::unique_ptr<uint8_t[]> buffer = GetUsbDescriptor(usbHubHandle, connectionIndex, USB_STRING_DESCRIPTOR_TYPE, MAXIMUM_USB_STRING_LENGTH, stringIndex, languageID);
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

    bool GetUsbHidReportDescriptor(const ScopedHandle& usbHubHandle, ULONG connectionIndex, PUSB_INTERFACE_DESCRIPTOR interfaceDescriptor, std::vector<uint8_t>& outReportDescriptor)
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

#include <pshpack1.h>
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
#include <poppack.h>

        PHID_DESCRIPTOR hidDescriptor = reinterpret_cast<PHID_DESCRIPTOR>(reinterpret_cast<uint8_t*>(interfaceDescriptor) + interfaceDescriptor->bLength);

        // Codes for HID-specific descriptor types
        constexpr UCHAR HID_HID_DESCRIPTOR_TYPE = 0x21;
        constexpr UCHAR HID_REPORT_DESCRIPTOR_TYPE = 0x22;

        CHECK_EQ(hidDescriptor->bLength, sizeof(HID_DESCRIPTOR));
        CHECK_EQ(hidDescriptor->bDescriptorType, HID_HID_DESCRIPTOR_TYPE);

        CHECK_GE(hidDescriptor->bNumDescriptors, 1);

        CHECK_EQ(hidDescriptor->DescriptorList[0].bReportType, HID_REPORT_DESCRIPTOR_TYPE);
        CHECK_NE(hidDescriptor->DescriptorList[0].wReportLength, 0);

        // According to HID spec we need to do Report Descriptor request with
        // bmRequest set to 0x81 (Interface_In) but seems its also working with 0x80 for some reason
        const USHORT hidReportDescriptorSize = hidDescriptor->DescriptorList[0].wReportLength;
        std::unique_ptr<uint8_t[]> hidReportDescriptor = GetUsbDescriptor(usbHubHandle, connectionIndex, HID_REPORT_DESCRIPTOR_TYPE, hidReportDescriptorSize, 0, interfaceDescriptor->bInterfaceNumber);

        if (!hidReportDescriptor)
            return false;

        outReportDescriptor.assign(hidReportDescriptor.get(), hidReportDescriptor.get() + hidReportDescriptorSize);

        return true;
    }
}

static inline void HexDump(const uint8_t* src, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        if (i % 8 == 0)
        {
            //printf("%04x ", uint32_t(i));
        }

        printf("%02x ", src[i]);

        if ((i + 1) % 8 == 0)
        {
            //printf("\n");
        }
    }
    //printf("\n");
    printf("(%d bytes)\n", (int)len);
}

bool RawInputDevice::QueryUsbDeviceInfo()
{
    std::string usbDeviceInstanceId = GetDeviceFromInterface(m_UsbDeviceInterface);
    DEVINST devNodeHandle = OpenDevNode(usbDeviceInstanceId);

    // device index in parent USB hub
    // https://docs.microsoft.com/windows-hardware/drivers/ddi/wdm/ns-wdm-_device_capabilities#usb
    ULONG deviceIndex = PropertyDataCast<ULONG>(GetDevNodeProperty(devNodeHandle, &DEVPKEY_Device_Address, DEVPROP_TYPE_UINT32));

    // USB device interface index
    UCHAR interfaceNumber = 0;

    std::vector<std::string> usbDeviceCompatibleIds = PropertyDataCast<std::vector<std::string>>(GetDevNodeProperty(devNodeHandle, &DEVPKEY_Device_CompatibleIds, DEVPROP_TYPE_STRING_LIST));
    for (const std::string& usbCompatibleId : usbDeviceCompatibleIds)
    {
        stringutils::ci_string tmp(usbCompatibleId.c_str(), usbCompatibleId.size());
        if (tmp.find("USB\\COMPOSITE") == stringutils::ci_string::npos)
            continue;

        // Its Composite USB device
        // Need to aquire interface number in parent USB device
        // https://docs.microsoft.com/windows-hardware/drivers/usbcon/usb-common-class-generic-parent-driver
        std::string usbCompositeDeviceInstanceId = GetParentDevice(m_DeviceInstanceId);
        DEVINST usbCompositeDevNodeHandle = OpenDevNode(usbCompositeDeviceInstanceId);
        interfaceNumber = PropertyDataCast<UCHAR>(GetDevNodeProperty(usbCompositeDevNodeHandle, &DEVPKEY_Device_Address, DEVPROP_TYPE_UINT32));
        --interfaceNumber; // should be zero-based
        break;
    }

    std::string usbHubInterface = SearchParentDeviceInterface(usbDeviceInstanceId, &GUID_DEVINTERFACE_USB_HUB);

    if (usbHubInterface.empty())
        return false;

    ScopedHandle usbHubInterfaceHandle = OpenDeviceInterface(usbHubInterface, true);

    USB_DEVICE_DESCRIPTOR deviceDescriptor;
    if (!GetUsbDeviceDescriptor(usbHubInterfaceHandle, deviceIndex, deviceDescriptor))
        return false;

    m_UsbVendorId = deviceDescriptor.idVendor;
    m_UsbProductId = deviceDescriptor.idProduct;
    m_UsbVersionNumber = deviceDescriptor.bcdDevice;

    // Assume that we are always using first configuration
    const UCHAR configurationIndex = 0;
    std::vector<uint8_t> configurationDescriptorData;
    if (!GetFullUsbConfigurationDescriptor(usbHubInterfaceHandle, deviceIndex, configurationIndex, configurationDescriptorData))
        return false;

    // TMP
    printf("USB Descriptor:\n");
    HexDump(configurationDescriptorData.data(), configurationDescriptorData.size());

    // Search for interface descriptor
    PUSB_INTERFACE_DESCRIPTOR interfaceDescriptor = nullptr;
    if (!SearchInterfaceDescriptor(configurationDescriptorData, interfaceNumber, interfaceDescriptor))
        return false;

    std::wstring stringBuffer;
    // Get the array of supported Language IDs, which is returned in String Descriptor 0
    if (!GetUsbDeviceString(usbHubInterfaceHandle, deviceIndex, 0, 0, stringBuffer))
        return false;

    USHORT languageID = stringBuffer[0];

    if (GetUsbDeviceString(usbHubInterfaceHandle, deviceIndex, deviceDescriptor.iManufacturer, languageID, stringBuffer))
        m_UsbDeviceManufacturer = utf8::narrow(stringBuffer);

    // Get interface name instead of whole product name, if present
    if (GetUsbDeviceString(usbHubInterfaceHandle, deviceIndex, interfaceDescriptor->iInterface ? interfaceDescriptor->iInterface : deviceDescriptor.iProduct, languageID, stringBuffer))
        m_UsbDeviceProduct = utf8::narrow(stringBuffer);

    if (GetUsbDeviceString(usbHubInterfaceHandle, deviceIndex, deviceDescriptor.iSerialNumber, languageID, stringBuffer))
        m_UsbDeviceSerialNumber = utf8::narrow(stringBuffer);

    GetUsbHidReportDescriptor(usbHubInterfaceHandle, deviceIndex, interfaceDescriptor, m_UsbHidReportDescriptor);

    return true;
}

bool RawInputDevice::QueryHidDeviceInfo()
{
    DCHECK(!m_HidInterfacePath.empty());

    ScopedHandle hidHandle = OpenDeviceInterface(m_HidInterfacePath, true);

    if (!IsValidHandle(hidHandle.get()))
        return false;

    std::wstring buffer;
    buffer.resize(128);

    if (::HidD_GetManufacturerString(hidHandle.get(), buffer.data(), static_cast<ULONG>(buffer.size())))
        m_ManufacturerString = utf8::narrow(buffer);

    if (::HidD_GetProductString(hidHandle.get(), buffer.data(), static_cast<ULONG>(buffer.size())))
        m_ProductString = utf8::narrow(buffer);

    if (::HidD_GetSerialNumberString(hidHandle.get(), buffer.data(), static_cast<ULONG>(buffer.size())))
        m_SerialNumberString = utf8::narrow(buffer);

    HIDD_ATTRIBUTES attrib;
    attrib.Size = sizeof(HIDD_ATTRIBUTES);

    if (::HidD_GetAttributes(hidHandle.get(), &attrib))
    {
        m_VendorId = attrib.VendorID;
        m_ProductId = attrib.ProductID;
        m_VersionNumber = attrib.VersionNumber;
    }

    return !m_ManufacturerString.empty() || !m_ProductString.empty() || !m_SerialNumberString.empty() || m_VendorId || m_ProductId || m_VersionNumber;
}

bool RawInputDevice::QueryRawDeviceInfo(HANDLE handle, RID_DEVICE_INFO* deviceInfo)
{
    UINT size = sizeof(RID_DEVICE_INFO);
    UINT result = ::GetRawInputDeviceInfoW(handle, RIDI_DEVICEINFO, deviceInfo, &size);

    if (result == static_cast<UINT>(-1))
    {
        DBGPRINT("GetRawInputDeviceInfo() failed for 0x%x handle", handle);
        return false;
    }
    DCHECK_EQ(size, result);

    return true;
}

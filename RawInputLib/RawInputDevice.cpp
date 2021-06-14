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

bool RawInputDevice::QueryUsbDeviceInfo()
{
    std::string usbDeviceInstanceId = GetDeviceFromInterface(m_UsbDeviceInterface);

    DEVINST devNodeHandle = OpenDevNode(usbDeviceInstanceId);

    // device index in parent USB hub
    uint32_t deviceIndex = PropertyDataCast<uint32_t>(GetDevNodeProperty(devNodeHandle, &DEVPKEY_Device_Address, DEVPROP_TYPE_UINT32));

    std::string usbHubInterface = SearchParentDeviceInterface(m_DeviceInstanceId, &GUID_DEVINTERFACE_USB_HUB);

    if (usbHubInterface.empty())
        return false;

    ScopedHandle usbHubInterfaceHandle = OpenDeviceInterface(usbHubInterface);

    if (!IsValidHandle(usbHubInterfaceHandle.get()))
        return false;

    DWORD len = sizeof(USB_NODE_CONNECTION_INFORMATION);
    std::vector<uint8_t> buffer(len, 0);

    PUSB_NODE_CONNECTION_INFORMATION connectionInfo = reinterpret_cast<PUSB_NODE_CONNECTION_INFORMATION>(buffer.data());
    connectionInfo->ConnectionIndex = deviceIndex;

    if (!::DeviceIoControl(usbHubInterfaceHandle.get(),
        IOCTL_USB_GET_NODE_CONNECTION_INFORMATION,
        connectionInfo,
        len,
        connectionInfo,
        len,
        &len,
        nullptr))
        return false;

    m_UsbVendorId = connectionInfo->DeviceDescriptor.idVendor;
    m_UsbProductId = connectionInfo->DeviceDescriptor.idProduct;
    m_UsbVersionNumber = connectionInfo->DeviceDescriptor.bcdDevice;

    // TODO we can also
    // call IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION
    // to get string descriptors from USB device: serial number etc

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

    if (::HidD_GetSerialNumberString(hidHandle.get(), &buffer.front(), static_cast<ULONG>(buffer.size())))
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

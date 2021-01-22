#include "pch.h"
#include "framework.h"

#include "RawInputDevice.h"

#include <hidsdi.h> // HidD_* API

#include <initguid.h>
#include <Cfgmgr32.h>
#include <Devpkey.h>

#include <Usbiodef.h> // USB GUIDs

RawInputDevice::RawInputDevice(HANDLE handle)
    : m_Handle(handle)
    , m_IsValid(false)
{}

RawInputDevice::~RawInputDevice() = default;

bool RawInputDevice::QueryDeviceInfo()
{
    if (!m_RawInputInfo.QueryInfo(m_Handle))
        return false;

    if (!m_DeviceNodeInfo.QueryInfo(m_RawInputInfo.m_InterfaceName))
        return false;

    // optional HID device info
    if (IsHidDevice() && !m_HidDInfo.QueryInfo(m_RawInputInfo.m_InterfaceHandle))
    {
        DBGPRINT("Cannot get HID info from '%s' interface.", m_RawInputInfo.m_InterfaceName.c_str());
        return false;
    }

    return true;
}

namespace
{
    ScopedHandle OpenInterface(const std::string& deviceInterface)
    {
        DWORD desired_access = GENERIC_WRITE | GENERIC_READ;
        DWORD share_mode = FILE_SHARE_READ | FILE_SHARE_WRITE;

        HANDLE handle = ::CreateFileW(utf8::widen(deviceInterface).c_str(), desired_access, share_mode, 0, OPEN_EXISTING, 0, 0);

        if (!IsValidHandle(handle))
        {
            /* System devices, such as keyboards and mice, cannot be opened in
               read-write mode, because the system takes exclusive control over
               them.  This is to prevent keyloggers.  However, feature reports
               can still be sent and received.  Retry opening the device, but
               without read/write access. */
            desired_access = 0;
            handle = ::CreateFileW(utf8::widen(deviceInterface).c_str(), desired_access, share_mode, 0, OPEN_EXISTING, 0, 0);
        }

        return ScopedHandle(handle);
    }
}

bool RawInputDevice::RawInputInfo::QueryInfo(HANDLE handle)
{
    DCHECK(IsValidHandle(handle));

    UINT size = 0;

    UINT result = ::GetRawInputDeviceInfoW(handle, RIDI_DEVICENAME, nullptr, &size);
    if (result == static_cast<UINT>(-1))
    {
        //PLOG(ERROR) << "GetRawInputDeviceInfo() failed";
        return false;
    }
    DCHECK_EQ(0u, result);

    std::wstring buffer(size, 0);
    result = ::GetRawInputDeviceInfoW(handle, RIDI_DEVICENAME, buffer.data(), &size);
    if (result == static_cast<UINT>(-1))
    {
        //PLOG(ERROR) << "GetRawInputDeviceInfo() failed";
        return false;
    }
    DCHECK_EQ(size, result);

    m_InterfaceName = utf8::narrow(buffer);
    m_InterfaceHandle = OpenInterface(m_InterfaceName);

    return !m_InterfaceName.empty() && IsValidHandle(m_InterfaceHandle.get());
}

bool RawInputDevice::HidDInfo::QueryInfo(const ScopedHandle& interfaceHandle)
{
    DCHECK(IsValidHandle(interfaceHandle.get()));

    std::wstring buffer;
    buffer.resize(128);

    if (::HidD_GetManufacturerString(interfaceHandle.get(), buffer.data(), static_cast<ULONG>(buffer.size())))
        m_ManufacturerString = utf8::narrow(buffer);

    if (::HidD_GetProductString(interfaceHandle.get(), buffer.data(), static_cast<ULONG>(buffer.size())))
        m_ProductString = utf8::narrow(buffer);

    if (::HidD_GetSerialNumberString(interfaceHandle.get(), &buffer.front(), static_cast<ULONG>(buffer.size())))
        m_SerialNumberString = utf8::narrow(buffer);

    HIDD_ATTRIBUTES attrib;
    attrib.Size = sizeof(HIDD_ATTRIBUTES);

    if (::HidD_GetAttributes(interfaceHandle.get(), &attrib))
    {
        m_VendorId = attrib.VendorID;
        m_ProductId = attrib.ProductID;
        m_VersionNumber = attrib.VersionNumber;
    }

    return !m_ManufacturerString.empty() || !m_ProductString.empty() || !m_SerialNumberString.empty() || m_VendorId || m_ProductId || m_VersionNumber;
}

namespace
{
    std::vector<uint8_t> GetDeviceInterfaceProperty(const std::wstring& deviceInterfaceName, const DEVPROPKEY* propertyKey, DEVPROPTYPE expectedPropertyType)
    {
        DEVPROPTYPE propertyType;
        ULONG propertySize = 0;
        CONFIGRET cr = ::CM_Get_Device_Interface_PropertyW(deviceInterfaceName.c_str(), propertyKey, &propertyType, nullptr, &propertySize, 0);

        DCHECK_EQ(cr, CR_BUFFER_SMALL);
        DCHECK_EQ(propertyType, expectedPropertyType);

        std::vector<uint8_t> propertyData(propertySize, 0);
        cr = ::CM_Get_Device_Interface_PropertyW(deviceInterfaceName.c_str(), propertyKey, &propertyType, (PBYTE)propertyData.data(), &propertySize, 0);

        DCHECK_EQ(cr, CR_SUCCESS);
        DCHECK_EQ(propertyType, expectedPropertyType);

        return std::move(propertyData);
    }

    std::vector<uint8_t> GetDevNodeProperty(DEVINST devInst, const DEVPROPKEY* propertyKey, DEVPROPTYPE expectedPropertyType)
    {
        DEVPROPTYPE propertyType;
        ULONG propertySize = 0;
        CONFIGRET cr = ::CM_Get_DevNode_PropertyW(devInst, propertyKey, &propertyType, nullptr, &propertySize, 0);

        if (cr == CR_NO_SUCH_VALUE)
            return {};

        DCHECK_EQ(cr, CR_BUFFER_SMALL);
        DCHECK_EQ(propertyType, expectedPropertyType);

        std::vector<uint8_t> propertyData(propertySize, 0);
        cr = ::CM_Get_DevNode_PropertyW(devInst, propertyKey, &propertyType, (PBYTE)propertyData.data(), &propertySize, 0);

        DCHECK_EQ(cr, CR_SUCCESS);
        DCHECK_EQ(propertyType, expectedPropertyType);

        return std::move(propertyData);
    }

    template<typename T> T PropertyDataCast(const std::vector<uint8_t>& propertyData)
    {
        if (propertyData.empty())
            return {};

        return *const_cast<T*>(reinterpret_cast<const T*>(propertyData.data()));
    }

    template<> std::wstring PropertyDataCast(const std::vector<uint8_t>& propertyData)
    {
        if (propertyData.empty())
            return {};

        return std::wstring(reinterpret_cast<const wchar_t*>(propertyData.data()), propertyData.size() / sizeof(wchar_t));
    }

    template<> std::string PropertyDataCast(const std::vector<uint8_t>& propertyData)
    {
        if (propertyData.empty())
            return {};

        return utf8::narrow(PropertyDataCast<std::wstring>(propertyData));
    }

    template<> std::vector<std::string> PropertyDataCast(const std::vector<uint8_t>& propertyData)
    {
        std::wstring strList(PropertyDataCast<std::wstring>(propertyData));

        std::vector<std::string> outList;
        for (size_t i = 0; i != std::wstring::npos && i < strList.size(); i = strList.find(L'\0', i), ++i)
        {
            std::wstring elem(&strList[i]);
            if (!elem.empty())
                outList.emplace_back(utf8::narrow(elem));
        }

        return std::move(outList);
    }

    std::string GetDeviceInterface(const std::wstring& deviceInstanceId, const GUID* intefaceGuid)
    {
        ULONG listSize;
        CONFIGRET cr = CM_Get_Device_Interface_List_Size(&listSize, (LPGUID)intefaceGuid, (WCHAR*)deviceInstanceId.data(), CM_GET_DEVICE_INTERFACE_LIST_PRESENT);

        DCHECK(cr == CR_SUCCESS);

        std::vector<uint8_t> listData(listSize * sizeof(WCHAR), 0);
        cr = CM_Get_Device_Interface_ListW((LPGUID)intefaceGuid, (WCHAR*)deviceInstanceId.data(), (PZZWSTR)listData.data(), listSize, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);

        DCHECK(cr == CR_SUCCESS);

        auto interfaces = PropertyDataCast<std::vector<std::string>>(listData);

        if (interfaces.empty())
            return {};

        return interfaces.front();
    }

    std::string FindParentDeviceInterface(DEVINST devInst, const GUID* intefaceGuid)
    {
        std::string outInterface;
        DEVINST parentDevInst;

        while (CM_Get_Parent(&parentDevInst, devInst, 0) == CR_SUCCESS)
        {
            std::wstring instanceId = PropertyDataCast<std::wstring>(GetDevNodeProperty(parentDevInst, &DEVPKEY_Device_InstanceId, DEVPROP_TYPE_STRING));

            outInterface = GetDeviceInterface(instanceId, intefaceGuid);
            if (!outInterface.empty())
                break;

            devInst = parentDevInst;
        };

        return std::move(outInterface);
    }
}

bool RawInputDevice::DeviceNodeInfo::QueryInfo(const std::string& interfaceName)
{
    DCHECK(!interfaceName.empty());

    m_DeviceInstanceId = PropertyDataCast<std::string>(GetDeviceInterfaceProperty(utf8::widen(interfaceName), &DEVPKEY_Device_InstanceId, DEVPROP_TYPE_STRING));

    DEVINST deviceInstanceHandle;
    CONFIGRET cr = ::CM_Locate_DevNodeW(&deviceInstanceHandle, utf8::widen(m_DeviceInstanceId).data(), CM_LOCATE_DEVNODE_NORMAL);

    if (cr != CR_SUCCESS)
        return false;

    m_Manufacturer = PropertyDataCast<std::string>(GetDevNodeProperty(deviceInstanceHandle, &DEVPKEY_Device_Manufacturer, DEVPROP_TYPE_STRING));
    m_FriendlyName = PropertyDataCast<std::string>(GetDevNodeProperty(deviceInstanceHandle, &DEVPKEY_NAME, DEVPROP_TYPE_STRING));
    m_DeviceService = PropertyDataCast<std::string>(GetDevNodeProperty(deviceInstanceHandle, &DEVPKEY_Device_Service, DEVPROP_TYPE_STRING));
    m_DeviceClass = PropertyDataCast<std::string>(GetDevNodeProperty(deviceInstanceHandle, &DEVPKEY_Device_Class, DEVPROP_TYPE_STRING));


    // TODO extract VID/PID from hardwareId
    m_DeviceHardwareIds = PropertyDataCast<std::vector<std::string>>(GetDevNodeProperty(deviceInstanceHandle, &DEVPKEY_Device_HardwareIds, DEVPROP_TYPE_STRING_LIST));

    GUID hid_guid;
    HidD_GetHidGuid(&hid_guid);

    m_IsHidDevice = !GetDeviceInterface(utf8::widen(m_DeviceInstanceId).data(), &hid_guid).empty();

    if (m_IsHidDevice)
    {
        m_ParentUsbDeviceInterface = FindParentDeviceInterface(deviceInstanceHandle, &GUID_DEVINTERFACE_USB_DEVICE);
        m_ParentUsbHubInterface = FindParentDeviceInterface(deviceInstanceHandle, &GUID_DEVINTERFACE_USB_HUB);
    }

    // TODO how to get USB_DEVICE_DESCRIPTOR
    // 1. call IOCTL_USB_GET_HUB_INFORMATION_EX on usb hub to get USB_HUB_INFORMATION_EX.HighestPortNumber
    // 2. for each port 1..HighestPortNumber call IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME
    // 3. compare with our device's DEVPKEY_Device_Driver to find needed portnumber
    // 4. call IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX with our portnumber to get USB_NODE_CONNECTION_INFORMATION_EX.DeviceDescriptor of type USB_DEVICE_DESCRIPTOR

    return !m_FriendlyName.empty() || !m_Manufacturer.empty();
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

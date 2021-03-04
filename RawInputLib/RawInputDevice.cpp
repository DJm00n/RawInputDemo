#include "pch.h"
#include "framework.h"

#include "RawInputDevice.h"

#pragma warning(push, 0)
#include <hidsdi.h> // HidD_* API

#include <initguid.h>
#include <Cfgmgr32.h>
#include <Devpkey.h>
#pragma warning(pop)

RawInputDevice::RawInputDevice(HANDLE handle)
    : m_Handle(handle)
    , m_IsValid(false)
{}

RawInputDevice::~RawInputDevice() = default;

bool RawInputDevice::QueryDeviceInfo()
{
    if (!QueryRawInputDeviceInfo())
        return false;

    if (!QueryDeviceNodeInfo())
        return false;

    // optional HID device info
    if (IsHidDevice() && !QueryHidDeviceInfo())
    {
        DBGPRINT("Cannot get HID info from '%s' interface.", m_InterfacePath.c_str());
        return false;
    }

    return true;
}

namespace
{
    ScopedHandle OpenInterface(const std::string& deviceInterface, bool readOnly)
    {
        DWORD desired_access = readOnly ? 0 : (GENERIC_WRITE | GENERIC_READ);
        DWORD share_mode =  FILE_SHARE_READ | FILE_SHARE_WRITE;

        HANDLE handle = ::CreateFileW(utf8::widen(deviceInterface).c_str(), desired_access, share_mode, 0, OPEN_EXISTING, 0, 0);

        return ScopedHandle(handle);
    }
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

    m_InterfaceHandle = OpenInterface(m_InterfacePath, false);

    if (!IsValidHandle(m_InterfaceHandle.get()))
    {
        /* System devices, such as keyboards and mice, cannot be opened in
        read-write mode, because the system takes exclusive control over
        them.  This is to prevent keyloggers.  However, feature reports
        can still be sent and received.  Retry opening the device, but
        without read/write access. */
        m_InterfaceHandle = OpenInterface(m_InterfacePath, true);
        m_IsReadOnlyInterface = true;
    }

    return !m_InterfacePath.empty() && IsValidHandle(m_InterfaceHandle.get());
}

namespace
{
    std::vector<uint8_t> GetDeviceInterfaceProperty(const std::string& deviceInterfaceName, const DEVPROPKEY* propertyKey, DEVPROPTYPE expectedPropertyType)
    {
        std::wstring devInterface = utf8::widen(deviceInterfaceName);
        DEVPROPTYPE propertyType = DEVPROP_TYPE_EMPTY;
        ULONG propertySize = 0;
        CONFIGRET cr = ::CM_Get_Device_Interface_PropertyW(devInterface.c_str(), propertyKey, &propertyType, nullptr, &propertySize, 0);

        DCHECK_EQ(cr, CR_BUFFER_SMALL);
        DCHECK_EQ(propertyType, expectedPropertyType);

        std::vector<uint8_t> propertyData(propertySize, 0);
        cr = ::CM_Get_Device_Interface_PropertyW(devInterface.c_str(), propertyKey, &propertyType, propertyData.data(), &propertySize, 0);

        DCHECK_EQ(cr, CR_SUCCESS);
        DCHECK_EQ(propertyType, expectedPropertyType);

        return std::move(propertyData);
    }

    DEVINST OpenDevNode(const std::string& deviceInstanceId)
    {
        std::wstring devInstance = utf8::widen(deviceInstanceId);
        DEVINST devNodeHandle;
        CONFIGRET cr = ::CM_Locate_DevNodeW(&devNodeHandle, devInstance.data(), CM_LOCATE_DEVNODE_NORMAL);

        DCHECK_EQ(cr, CR_SUCCESS);

        return devNodeHandle;
    }

    std::vector<uint8_t> GetDevNodeProperty(DEVINST devInst, const DEVPROPKEY* propertyKey, DEVPROPTYPE expectedPropertyType)
    {
        DEVPROPTYPE propertyType = DEVPROP_TYPE_EMPTY;
        ULONG propertySize = 0;
        CONFIGRET cr = ::CM_Get_DevNode_PropertyW(devInst, propertyKey, &propertyType, nullptr, &propertySize, 0);

        if (cr == CR_NO_SUCH_VALUE)
            return {};

        DCHECK_EQ(cr, CR_BUFFER_SMALL);
        DCHECK_EQ(propertyType, expectedPropertyType);

        std::vector<uint8_t> propertyData(propertySize, 0);
        cr = ::CM_Get_DevNode_PropertyW(devInst, propertyKey, &propertyType, propertyData.data(), &propertySize, 0);

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

        std::wstring wstr(reinterpret_cast<const std::wstring::value_type*>(propertyData.data()), propertyData.size() / sizeof(std::wstring::value_type));

        return std::move(wstr);
    }

    template<> std::string PropertyDataCast(const std::vector<uint8_t>& propertyData)
    {
        if (propertyData.empty())
            return {};

        std::wstring wstr(PropertyDataCast<std::wstring>(propertyData));

        return utf8::narrow(wstr.data(), wstr.size());
    }

    template<> std::vector<std::string> PropertyDataCast(const std::vector<uint8_t>& propertyData)
    {
        std::string strList(PropertyDataCast<std::string>(propertyData));

        std::vector<std::string> outList;
        for (size_t i = 0; i != std::wstring::npos && i < strList.size(); i = strList.find('\0', i), ++i)
        {
            std::string elem(&strList[i]);
            if (!elem.empty())
                outList.emplace_back(elem);
        }

        return std::move(outList);
    }

    std::string GetDeviceInterface(const std::string& deviceInstanceId, LPGUID intefaceGuid)
    {
        std::wstring deviceID = utf8::widen(deviceInstanceId);
        ULONG listSize;
        CONFIGRET cr = CM_Get_Device_Interface_List_SizeW(&listSize, intefaceGuid, deviceID.data(), CM_GET_DEVICE_INTERFACE_LIST_PRESENT);

        DCHECK(cr == CR_SUCCESS);

        std::vector<uint8_t> listData(listSize * sizeof(WCHAR), 0);
        cr = ::CM_Get_Device_Interface_ListW(intefaceGuid, deviceID.data(), reinterpret_cast<PZZWSTR>(listData.data()), listSize, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);

        DCHECK(cr == CR_SUCCESS);

        auto interfaces = PropertyDataCast<std::vector<std::string>>(listData);

        if (interfaces.empty())
            return {};

        return interfaces.front();
    }
}

bool RawInputDevice::QueryDeviceNodeInfo()
{
    DCHECK(!m_InterfacePath.empty());

    m_DeviceInstanceId = PropertyDataCast<std::string>(GetDeviceInterfaceProperty(m_InterfacePath, &DEVPKEY_Device_InstanceId, DEVPROP_TYPE_STRING));

    DEVINST devNodeHandle = OpenDevNode(m_DeviceInstanceId);

    m_ManufacturerString = PropertyDataCast<std::string>(GetDevNodeProperty(devNodeHandle, &DEVPKEY_Device_Manufacturer, DEVPROP_TYPE_STRING));
    m_ProductString = PropertyDataCast<std::string>(GetDevNodeProperty(devNodeHandle, &DEVPKEY_NAME, DEVPROP_TYPE_STRING));
    m_DeviceService = PropertyDataCast<std::string>(GetDevNodeProperty(devNodeHandle, &DEVPKEY_Device_Service, DEVPROP_TYPE_STRING));
    m_DeviceClass = PropertyDataCast<std::string>(GetDevNodeProperty(devNodeHandle, &DEVPKEY_Device_Class, DEVPROP_TYPE_STRING));

    // TODO extract VID/PID/COL from hardwareId
    m_DeviceHardwareIds = PropertyDataCast<std::vector<std::string>>(GetDevNodeProperty(devNodeHandle, &DEVPKEY_Device_HardwareIds, DEVPROP_TYPE_STRING_LIST));

    GUID hid_guid;
    ::HidD_GetHidGuid(&hid_guid);

    m_IsHidDevice = !GetDeviceInterface(m_DeviceInstanceId, &hid_guid).empty();

    return !m_ProductString.empty() || !m_ManufacturerString.empty();
}

bool RawInputDevice::QueryHidDeviceInfo()
{
    DCHECK(IsValidHandle(m_InterfaceHandle.get()));

    std::wstring buffer;
    buffer.resize(128);

    if (::HidD_GetManufacturerString(m_InterfaceHandle.get(), buffer.data(), static_cast<ULONG>(buffer.size())))
        m_ManufacturerString = utf8::narrow(buffer);

    if (::HidD_GetProductString(m_InterfaceHandle.get(), buffer.data(), static_cast<ULONG>(buffer.size())))
        m_ProductString = utf8::narrow(buffer);

    if (::HidD_GetSerialNumberString(m_InterfaceHandle.get(), &buffer.front(), static_cast<ULONG>(buffer.size())))
        m_SerialNumberString = utf8::narrow(buffer);

    HIDD_ATTRIBUTES attrib;
    attrib.Size = sizeof(HIDD_ATTRIBUTES);

    if (::HidD_GetAttributes(m_InterfaceHandle.get(), &attrib))
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

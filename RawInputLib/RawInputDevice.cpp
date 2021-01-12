#include "pch.h"
#include "framework.h"

#include "RawInputDevice.h"

#include <hidsdi.h> // HidD_* API

#include <initguid.h>
#include <Cfgmgr32.h>
#include <Devpkey.h>

RawInputDevice::RawInputDevice(HANDLE handle)
    : m_Handle(handle)
    , m_IsValid(false)
{}

RawInputDevice::~RawInputDevice() = default;

bool RawInputDevice::QueryDeviceInfo()
{
    if (!m_RawInput.QueryInfo(m_Handle))
        return false;

    if (!m_DeviceNodeInfo.QueryInfo(m_RawInput.m_InterfaceName))
        return false;

    // optional HID device info
    if (!m_HidInfo.QueryInfo(m_RawInput.m_InterfaceHandle))
    {
        //DBGPRINT("Cannot get optional HID info from '%s' interface.", m_RawInput.m_InterfaceName.c_str());
    }

    return true;
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

    DWORD desired_access = GENERIC_WRITE | GENERIC_READ;
    DWORD share_mode = FILE_SHARE_READ | FILE_SHARE_WRITE;

    m_InterfaceHandle.reset(::CreateFileW(buffer.c_str(), desired_access, share_mode, 0, OPEN_EXISTING, 0, 0));

    if (!IsValidHandle(m_InterfaceHandle.get()))
    {
        /* System devices, such as keyboards and mice, cannot be opened in
           read-write mode, because the system takes exclusive control over
           them.  This is to prevent keyloggers.  However, feature reports
           can still be sent and received.  Retry opening the device, but
           without read/write access. */
        desired_access = 0;
        m_InterfaceHandle.reset(::CreateFileW(buffer.c_str(), desired_access, share_mode, 0, OPEN_EXISTING, 0, 0));
    }

    return !m_InterfaceName.empty() && IsValidHandle(m_InterfaceHandle.get());
}

bool RawInputDevice::HIDInfo::QueryInfo(const ScopedHandle& interfaceHandle)
{
    DCHECK(IsValidHandle(interfaceHandle.get()));

    std::wstring buffer;
    buffer.resize(RawInputDevice::kIdLengthCap);

    if (::HidD_GetManufacturerString(interfaceHandle.get(), buffer.data(), RawInputDevice::kIdLengthCap))
        m_ManufacturerString = utf8::narrow(buffer);

    if (::HidD_GetProductString(interfaceHandle.get(), buffer.data(), RawInputDevice::kIdLengthCap))
        m_ProductString = utf8::narrow(buffer);

    if (::HidD_GetSerialNumberString(interfaceHandle.get(), &buffer.front(), RawInputDevice::kIdLengthCap))
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

    std::wstring PropertyDataToString(const std::vector<uint8_t>& propertyData)
    {
        return { (wchar_t*)propertyData.data(), propertyData.size() / sizeof(wchar_t) };
    }

    std::vector<std::wstring> PropertyDataToStringList(const std::vector<uint8_t>& propertyData)
    {
        std::wstring strList = PropertyDataToString(propertyData);

        std::vector<std::wstring> outList;
        for (size_t i = 0; i != std::wstring::npos && i < strList.size(); i = strList.find(L'\0', i), ++i)
        {
            std::wstring elem(&strList[i]);
            if (!elem.empty())
                outList.emplace_back(elem);
        }

        return std::move(outList);
    }

    std::wstring GetInterfaceAlias(const std::wstring& deviceInterfaceName, GUID* intefaceGuid)
    {
        ULONG aliasSize = static_cast<ULONG>(deviceInterfaceName.size() + 1);

        std::wstring alias(aliasSize, 0);
        CONFIGRET cr = ::CM_Get_Device_Interface_AliasW(deviceInterfaceName.c_str(), intefaceGuid, alias.data(), &aliasSize, 0);

        // ensure that that it properly sized
        alias.resize(aliasSize > 0 ? aliasSize - 1 : 0);

        DCHECK(cr == CR_SUCCESS || cr == CR_NO_SUCH_DEVICE_INTERFACE);

        return std::move(alias);
    }
}

bool RawInputDevice::DeviceNodeInfo::QueryInfo(const std::string& interfaceName)
{
    DCHECK(!interfaceName.empty());

    auto deviceId = GetDeviceInterfaceProperty(utf8::widen(interfaceName), &DEVPKEY_Device_InstanceId, DEVPROP_TYPE_STRING);

    m_InstanceId = utf8::narrow(PropertyDataToString(deviceId));

    DEVINST devInst;
    CONFIGRET cr = ::CM_Locate_DevNodeW(&devInst, (DEVINSTID_W)deviceId.data(), CM_LOCATE_DEVNODE_NORMAL);

    if (cr != CR_SUCCESS)
        return false;

    m_Manufacturer = utf8::narrow(PropertyDataToString(GetDevNodeProperty(devInst, &DEVPKEY_Device_Manufacturer, DEVPROP_TYPE_STRING)));
    m_FriendlyName = utf8::narrow(PropertyDataToString(GetDevNodeProperty(devInst, &DEVPKEY_NAME, DEVPROP_TYPE_STRING)));
    m_DeviceService = utf8::narrow(PropertyDataToString(GetDevNodeProperty(devInst, &DEVPKEY_Device_Service, DEVPROP_TYPE_STRING)));
    m_DeviceClass = utf8::narrow(PropertyDataToString(GetDevNodeProperty(devInst, &DEVPKEY_Device_Class, DEVPROP_TYPE_STRING)));

    // TODO extract VID/PID from hardwareId
    //auto hardwareIds = PropertyDataToStringList(GetDevNodeProperty(devInst, &DEVPKEY_Device_HardwareIds, DEVPROP_TYPE_STRING_LIST));

    return !m_FriendlyName.empty() || !m_Manufacturer.empty();
}

bool RawInputDevice::QueryRawDeviceInfo(HANDLE handle, RID_DEVICE_INFO* deviceInfo)
{
    UINT size = sizeof(RID_DEVICE_INFO);
    UINT result = ::GetRawInputDeviceInfoW(handle, RIDI_DEVICEINFO, deviceInfo, &size);

    if (result == static_cast<UINT>(-1))
    {
        //PLOG(ERROR) << "GetRawInputDeviceInfo() failed";
        return false;
    }
    DCHECK_EQ(size, result);

    return true;
}

#include "pch.h"
#include "framework.h"

#include "RawInputDevice.h"

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
    if (!QueryRawDeviceName())
        return false;

    if (!QueryDevNodeInfo())
        return false;

    return true;
}

bool RawInputDevice::QueryRawDeviceName()
{
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

    m_DeviceInterfaceName = utf8::narrow(buffer);

    return true;
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
        return { (wchar_t*)propertyData.data(), propertyData.size() };
    }
}

bool RawInputDevice::QueryDevNodeInfo()
{
    DCHECK(!m_DeviceInterfaceName.empty());

    auto deviceId = GetDeviceInterfaceProperty(utf8::widen(m_DeviceInterfaceName), &DEVPKEY_Device_InstanceId, DEVPROP_TYPE_STRING);

    DEVINST devInst;
    CONFIGRET cr = ::CM_Locate_DevNodeW(&devInst, (DEVINSTID_W)deviceId.data(), CM_LOCATE_DEVNODE_NORMAL);

    if (cr != CR_SUCCESS)
        return false;

    m_FriendlyName = utf8::narrow(PropertyDataToString(GetDevNodeProperty(devInst, &DEVPKEY_NAME, DEVPROP_TYPE_STRING)));
    m_Manufacturer = utf8::narrow(PropertyDataToString(GetDevNodeProperty(devInst, &DEVPKEY_Device_Manufacturer, DEVPROP_TYPE_STRING)));

    auto hardwareIds = GetDevNodeProperty(devInst, &DEVPKEY_Device_HardwareIds, DEVPROP_TYPE_STRING_LIST);
    //std::wstring s((wchar_t*)hardwareIds.data(), hardwareIds.size());

    return !m_Manufacturer.empty() || !m_FriendlyName.empty();
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

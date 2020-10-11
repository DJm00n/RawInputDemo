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

bool RawInputDevice::QueryDevNodeInfo()
{
    DCHECK(!m_DeviceInterfaceName.empty());

    DEVPROPTYPE propertyType;
    ULONG propertySize = 0;
    CONFIGRET cr = ::CM_Get_Device_Interface_PropertyW(utf8::widen(m_DeviceInterfaceName).c_str(), &DEVPKEY_Device_InstanceId, &propertyType, nullptr, &propertySize, 0);

    DCHECK_EQ(propertyType, DEVPROP_TYPE_STRING);
    if (cr != CR_BUFFER_SMALL)
        return false;

    std::wstring deviceId;
    deviceId.resize(propertySize);
    cr = ::CM_Get_Device_Interface_PropertyW(utf8::widen(m_DeviceInterfaceName).c_str(), &DEVPKEY_Device_InstanceId, &propertyType, (PBYTE)deviceId.data(), &propertySize, 0);

    if (cr != CR_SUCCESS)
        return false;

    m_DeviceInstanceId = utf8::narrow(deviceId);

    DEVINST devInst;
    cr = ::CM_Locate_DevNodeW(&devInst, (DEVINSTID_W)deviceId.c_str(), CM_LOCATE_DEVNODE_NORMAL);

    if (cr != CR_SUCCESS)
        return false;

    propertySize = 0;
    cr = ::CM_Get_DevNode_PropertyW(devInst, &DEVPKEY_NAME, &propertyType, nullptr, &propertySize, 0);

    DCHECK_EQ(propertyType, DEVPROP_TYPE_STRING);
    if (cr != CR_BUFFER_SMALL)
        return false;

    std::wstring friendlyString;
    friendlyString.resize(propertySize);
    cr = ::CM_Get_DevNode_PropertyW(devInst, &DEVPKEY_NAME, &propertyType, (PBYTE)friendlyString.data(), &propertySize, 0);

    if (cr == CR_SUCCESS)
        m_FriendlyName = utf8::narrow(friendlyString);

    propertySize = 0;
    cr = ::CM_Get_DevNode_PropertyW(devInst, &DEVPKEY_Device_Manufacturer, &propertyType, nullptr, &propertySize, 0);

    DCHECK_EQ(propertyType, DEVPROP_TYPE_STRING);
    if (cr != CR_BUFFER_SMALL)
        return false;

    std::wstring manufacturer;
    manufacturer.resize(propertySize);
    cr = ::CM_Get_DevNode_PropertyW(devInst, &DEVPKEY_Device_Manufacturer, &propertyType, (PBYTE)manufacturer.data(), &propertySize, 0);

    if (cr == CR_SUCCESS)
        m_Manufacturer = utf8::narrow(manufacturer);

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

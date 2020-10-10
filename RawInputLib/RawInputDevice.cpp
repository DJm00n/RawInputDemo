#include "pch.h"
#include "framework.h"

#include "RawInputDevice.h"

#include <Cfgmgr32.h>

RawInputDevice::RawInputDevice(HANDLE handle)
    : m_Handle(handle)
    , m_IsValid(false)
{}

RawInputDevice::~RawInputDevice() = default;

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
    result = ::GetRawInputDeviceInfoW(m_Handle, RIDI_DEVICENAME, &buffer[0], &size);
    if (result == static_cast<UINT>(-1))
    {
        //PLOG(ERROR) << "GetRawInputDeviceInfo() failed";
        return false;
    }
    DCHECK_EQ(size, result);

    m_Name = utf8::narrow(buffer);

    return QueryDeviceName();
}

bool RawInputDevice::QueryDeviceName()
{
    DCHECK(!m_Name.empty());

    std::string name = m_Name;

    // remove prefix
    const std::string prefix("\\\\?\\");
    if (name.find(prefix) != std::string::npos)
        name = name.substr(prefix.size(), name.size() - prefix.size());

    // split by parts
    auto deviceIdParts = stringutils::split(name, '#');

    // convert device path into device instance id
    std::wstring deviceId = utf8::widen(deviceIdParts[0] + "\\" + deviceIdParts[1] + "\\" + deviceIdParts[2]);

    DEVINST devInst;
    CONFIGRET cr = CM_Locate_DevNodeW(&devInst, (DEVINSTID_W)deviceId.c_str(), CM_LOCATE_DEVNODE_NORMAL);
    if (cr != CR_SUCCESS)
        return false;

    constexpr ULONG bufferSize = 512;
    std::array<wchar_t, bufferSize> buffer;
    ULONG size = bufferSize;

    cr = CM_Get_DevNode_Registry_PropertyW(devInst, CM_DRP_MFG, nullptr, buffer.data(), &size, 0);

    if (cr == CR_SUCCESS)
        m_Manufacturer = utf8::narrow(buffer.data(), size);

    size = bufferSize;

    cr = CM_Get_DevNode_Registry_PropertyW(devInst, CM_DRP_DEVICEDESC, nullptr, buffer.data(), &size, 0);

    if (cr == CR_SUCCESS)
        m_DeviceDesc = utf8::narrow(buffer.data(), size);

    DBGPRINT("Device Info: %s, Device Desc: %s, Manufacturer: %s", m_Name.c_str(), m_DeviceDesc.c_str(), m_Manufacturer.c_str());

    return true;
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

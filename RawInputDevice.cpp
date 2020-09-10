#include "framework.h"

#include "RawInputDevice.h"

#include "RawInputDeviceMouse.h"
#include "RawInputDeviceKeyboard.h"
#include "RawInputDeviceHid.h"

RawInputDevice::RawInputDevice(HANDLE handle)
    : m_Handle(handle)
{}

RawInputDevice::~RawInputDevice() = default;

bool RawInputDevice::QueryRawDeviceName()
{
    UINT size = 0;

    UINT result = ::GetRawInputDeviceInfo(m_Handle, RIDI_DEVICENAME, nullptr, &size);
    if (result == static_cast<UINT>(-1))
    {
        //PLOG(ERROR) << "GetRawInputDeviceInfo() failed";
        return false;
    }
    //DCHECK_EQ(0u, result);

    std::wstring buffer(size, 0);
    result = ::GetRawInputDeviceInfo(m_Handle, RIDI_DEVICENAME, &buffer[0], &size);
    if (result == static_cast<UINT>(-1))
    {
        //PLOG(ERROR) << "GetRawInputDeviceInfo() failed";
        return false;
    }
    //DCHECK_EQ(size, result);

    m_Name = utf8::narrow(buffer);

    return true;
}

std::unique_ptr<RawInputDevice> RawInputDevice::CreateRawInputDevice(HANDLE handle)
{
    RID_DEVICE_INFO info;
    if (!QueryRawDeviceInfo(handle, &info))
        return nullptr;

    switch (info.dwType)
    {
    case RIM_TYPEMOUSE:
        return std::make_unique<RawInputDeviceMouse>(handle);
    case RIM_TYPEKEYBOARD:
        return std::make_unique<RawInputDeviceKeyboard>(handle);
    case RIM_TYPEHID:
        return std::make_unique<RawInputDeviceHid>(handle);
    }

    DBGPRINT("Unknown device type %d.", info.dwType);

    return nullptr;
}

bool RawInputDevice::QueryRawDeviceInfo(HANDLE handle, RID_DEVICE_INFO* deviceInfo)
{
    UINT size = sizeof(RID_DEVICE_INFO);
    UINT result = GetRawInputDeviceInfo(handle, RIDI_DEVICEINFO, deviceInfo, &size);

    if (result == static_cast<UINT>(-1))
    {
        //PLOG(ERROR) << "GetRawInputDeviceInfo() failed";
        return false;
    }
    //DCHECK_EQ(size, result);

    return true;
}

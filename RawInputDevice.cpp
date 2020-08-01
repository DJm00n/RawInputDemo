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

    std::unique_ptr<wchar_t[]> buffer(new wchar_t[size]);
    result = ::GetRawInputDeviceInfo(m_Handle, RIDI_DEVICENAME, buffer.get(), &size);
    if (result == static_cast<UINT>(-1))
    {
        //PLOG(ERROR) << "GetRawInputDeviceInfo() failed";
        return false;
    }
    //DCHECK_EQ(size, result);

    m_Name = toUtf8(buffer.get());

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

    DBGPRINT(L"Unknown device type %d.", info.dwType);

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

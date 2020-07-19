#include "framework.h"

#include "RawInputDevice.h"

#include "RawInputDeviceMouse.h"
#include "RawInputDeviceKeyboard.h"
#include "RawInputDeviceHid.h"

RawInputDevice::RawInputDevice(HANDLE handle)
    : m_Handle(handle)
    , m_IsValid(false)
{
}

RawInputDevice::~RawInputDevice()
{
}

std::unique_ptr<RawInputDevice> RawInputDevice::CreateRawInputDevice(UINT type, HANDLE handle)
{
    switch (type)
    {
    case RIM_TYPEMOUSE:
        return std::make_unique<RawInputDeviceMouse>(handle);
    case RIM_TYPEKEYBOARD:
        return std::make_unique<RawInputDeviceKeyboard>(handle);
    case RIM_TYPEHID:
        return std::make_unique<RawInputDeviceHid>(handle);
    }

    DBGPRINT(L"Unknown device type %d.", type);

    return nullptr;
}

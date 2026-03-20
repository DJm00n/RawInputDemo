#include "pch.h"
#include "framework.h"

#include "RawInputDeviceFactory.h"
#include "RawInputDeviceHid.h"

std::unique_ptr<RawInputDevice>
RawInputDeviceFactory<RawInputDeviceHid>::Create(HANDLE handle) const
{
    return RawInputDeviceHid::Create(handle);
}
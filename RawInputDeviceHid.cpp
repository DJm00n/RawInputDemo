#include "framework.h"

#include "RawInputDeviceHid.h"

RawInputDeviceHid::RawInputDeviceHid(HANDLE handle)
    : RawInputDevice(handle)
{
    m_IsValid = true;
}

RawInputDeviceHid::~RawInputDeviceHid()
{

}

void RawInputDeviceHid::OnInput(const RAWINPUT* input)
{

}

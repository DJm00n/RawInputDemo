#include "framework.h"

#include "RawInputDeviceKeyboard.h"

RawInputDeviceKeyboard::RawInputDeviceKeyboard(HANDLE handle)
    : RawInputDevice(handle)
{
    m_IsValid = true;
}

RawInputDeviceKeyboard::~RawInputDeviceKeyboard()
{

}

void RawInputDeviceKeyboard::OnInput(const RAWINPUT* input)
{

}

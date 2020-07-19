#pragma once

#include "RawInputDevice.h"

class RawInputDeviceMouse : public RawInputDevice
{
public:
    RawInputDeviceMouse(HANDLE handle);
    ~RawInputDeviceMouse();

    void OnInput(const RAWINPUT* input) override;
};

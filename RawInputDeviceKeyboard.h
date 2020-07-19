#pragma once

#include "RawInputDevice.h"

class RawInputDeviceKeyboard : public RawInputDevice
{
public:
    RawInputDeviceKeyboard(HANDLE handle);
    ~RawInputDeviceKeyboard();

    void OnInput(const RAWINPUT* input) override;
};

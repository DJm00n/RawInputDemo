#pragma once

#include "RawInputDevice.h"

class RawInputDeviceMouse : public RawInputDevice
{
public:
    RawInputDeviceMouse(HANDLE handle);
    ~RawInputDeviceMouse();

protected:
    void OnInput(const RAWINPUT* input) override;

    bool QueryDeviceInfo();

    bool QueryMouseInfo();

private:
    RID_DEVICE_INFO_MOUSE m_MouseInfo = {};
};

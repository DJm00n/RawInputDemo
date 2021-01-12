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

private:
    struct MouseInfo
    {
        bool QueryInfo(HANDLE handle);

        uint16_t m_NumberOfButtons;
        uint16_t m_SampleRate;
        bool m_HasVerticalWheel;
        bool m_HasHorizontalWheel;
    } m_MouseInfo;
};

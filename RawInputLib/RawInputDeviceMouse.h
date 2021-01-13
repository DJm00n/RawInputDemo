#pragma once

#include "RawInputDevice.h"

class RawInputDeviceMouse : public RawInputDevice
{
    friend class RawInputDeviceFactory<RawInputDeviceMouse>;

public:
    ~RawInputDeviceMouse();

protected:
    RawInputDeviceMouse(HANDLE handle);

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

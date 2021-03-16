#pragma once

#include "RawInputDevice.h"

#include <chrono>

class RawInputDeviceMouse : public RawInputDevice
{
    friend class RawInputDeviceFactory<RawInputDeviceMouse>;

public:
    ~RawInputDeviceMouse();

    RawInputDeviceMouse(RawInputDeviceMouse&) = delete;
    void operator=(RawInputDeviceMouse) = delete;

protected:
    RawInputDeviceMouse(HANDLE handle);

    void OnInput(const RAWINPUT* input) override;

    bool QueryDeviceInfo() override;

private:
    struct MouseInfo
    {
        bool QueryInfo(HANDLE handle);

        uint16_t m_NumberOfButtons;
        uint16_t m_SampleRate;
        bool m_HasVerticalWheel;
        bool m_HasHorizontalWheel;
    } m_MouseInfo;

    int relativeX = 0;
    int relativeY = 0;

    int eventPerSec = 0;
    std::chrono::seconds lastSec;
    std::chrono::system_clock::time_point lastUpdate;
};

#pragma once

#include "RawInputDevice.h"

class RawInputDeviceMouse : public RawInputDevice
{
    friend class RawInputDeviceManager;
    friend class RawInputDeviceFactory<RawInputDeviceMouse>;

public:
    ~RawInputDeviceMouse();

    RawInputDeviceMouse(RawInputDeviceMouse&) = delete;
    void operator=(RawInputDeviceMouse) = delete;

    uint32_t GetType() const override { return RIM_TYPEMOUSE; }

protected:
    RawInputDeviceMouse(HANDLE handle);

    void OnInput(const RAWINPUT* input) override;

    bool Initialize() override;

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

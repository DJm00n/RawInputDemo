#pragma once

#include "RawInputDevice.h"

class RawInputDeviceKeyboard : public RawInputDevice
{
public:
    RawInputDeviceKeyboard(HANDLE handle);
    ~RawInputDeviceKeyboard();

protected:
    void OnInput(const RAWINPUT* input) override;

    bool QueryDeviceInfo();

    bool QueryKeyboardInfo();

    bool KeyboardSetLeds(ScopedHandle& device_handle);

private:
    RID_DEVICE_INFO_KEYBOARD m_KeyboardInfo = {};
    std::array<uint8_t, 256> m_KeyState;
};

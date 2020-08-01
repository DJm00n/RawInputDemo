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

    // "Returns an open handle for the HID device, or an invalid handle if the
    // device could not be opened."
    ScopedHandle OpenKeyboardHandle() const;

    bool QueryKeyboardInfo();
    bool QueryProductString();

    bool KeyboardSetLeds(ScopedHandle& hid_handle);

private:
    RID_DEVICE_INFO_KEYBOARD m_KeyboardInfo = {};
    std::string m_ProductString;
};

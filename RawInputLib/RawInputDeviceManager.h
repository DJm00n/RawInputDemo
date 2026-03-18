#pragma once

#include "RawInputDevice.h"

class RawInputDeviceManager
{
public:
    RawInputDeviceManager();
    ~RawInputDeviceManager();

    RawInputDeviceManager(RawInputDeviceManager&) = delete;
    void operator=(RawInputDeviceManager) = delete;

    // Call from your main WndProc when WM_INPUTLANGCHANGE is received.
    // hkl — new keyboard layout handle from lParam.
    void OnInputLanguageChanged(HKL hkl);

    std::vector<std::shared_ptr<RawInputDevice>> GetRawInputDevices() const;

private:
    struct RawInputManagerImpl;
    std::unique_ptr<RawInputManagerImpl> m_RawInputManagerImpl;
};
#pragma once

#include "RawInputDevice.h"

class RawInputDeviceManager
{
public:
    RawInputDeviceManager();
    ~RawInputDeviceManager();

    RawInputDeviceManager(RawInputDeviceManager&) = delete;
    void operator=(RawInputDeviceManager) = delete;

    std::vector<RawInputDevice*> GetRawInputDevices() const;

private:
    struct RawInputManagerImpl;
    std::unique_ptr<RawInputManagerImpl> m_RawInputManagerImpl;
};
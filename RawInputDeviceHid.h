#pragma once

#include "RawInputDevice.h"

class RawInputDeviceHid : public RawInputDevice
{
public:
    RawInputDeviceHid(HANDLE handle);
    ~RawInputDeviceHid();

    uint16_t GetVendorId() const;
    uint16_t GetProductId() const;
    int16_t GetVersionNumber() const;
    std::string GetProductString() const;

    void OnInput(const RAWINPUT* input) override;
};

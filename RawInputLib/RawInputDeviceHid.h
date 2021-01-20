#pragma once

#include "RawInputDevice.h"

#include <hidsdi.h>

class RawInputDeviceManager;

class RawInputDeviceHid : public RawInputDevice
{
    friend class RawInputDeviceFactory<RawInputDeviceHid>;

    static constexpr size_t kAxesLengthCap = 16;
    static constexpr size_t kButtonsLengthCap = 32;

public:
    ~RawInputDeviceHid();

    uint16_t GetUsagePage() const { return m_UsagePage; }
    uint16_t GetUsageId() const { return m_UsageId; }

protected:
    RawInputDeviceHid(HANDLE handle);

    void OnInput(const RAWINPUT* input) override;

    bool QueryDeviceInfo() override;

    bool QueryDeviceCapabilities();
    void QueryButtonCapabilities(uint16_t button_count);
    void QueryNormalButtonCapabilities(HIDP_BUTTON_CAPS button_caps[], uint16_t button_count, std::vector<bool>* button_indices_used);
    void QueryAxisCapabilities(uint16_t axis_count);

private:
    // Axis state and capabilities for a single RawInput axis.
    struct RawGamepadAxis
    {
        HIDP_VALUE_CAPS caps;
        float value;
        bool active;
        unsigned long bitmask;
    };

private:
    uint16_t m_UsagePage = 0;
    uint16_t m_UsageId = 0;

    size_t m_ButtonsLength = 0;
    bool m_Buttons[kButtonsLengthCap];

    size_t m_AxesLength = 0;
    RawGamepadAxis m_Axes[kAxesLengthCap];

    // Buffer used for querying device capabilities. |m_PPD_buffer| owns the
    // memory pointed to by |m_Preparsed_data|.
    std::unique_ptr<uint8_t[]> m_PPDBuffer;
    PHIDP_PREPARSED_DATA m_PreparsedData = nullptr;
};

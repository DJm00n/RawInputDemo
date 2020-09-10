#pragma once

#include "RawInputDevice.h"

#include <hidsdi.h>

class RawInputDeviceHid : public RawInputDevice
{
public:
    static constexpr size_t kAxesLengthCap = 16;
    static constexpr size_t kButtonsLengthCap = 32;

    RawInputDeviceHid(HANDLE handle);
    ~RawInputDeviceHid();

    uint16_t GetVendorId() const { return static_cast<uint16_t>(m_HidInfo.dwVendorId); }
    uint16_t GetProductId() const { return static_cast<uint16_t>(m_HidInfo.dwProductId); }
    uint16_t GetVersionNumber() const { return static_cast<uint16_t>(m_HidInfo.dwVersionNumber); }
    uint16_t GetUsagePage() const { return static_cast<uint16_t>(m_HidInfo.usUsagePage); }
    uint16_t GetUsageId() const { return static_cast<uint16_t>(m_HidInfo.usUsage); }
    std::string GetProductString() const { return m_ProductString; }

protected:
    void OnInput(const RAWINPUT* input) override;

    bool QueryDeviceInfo();
    // "Returns an open handle for the HID device, or an invalid handle if the
    // device could not be opened."
    ScopedHandle OpenHidDevice() const;

    bool QueryHidInfo();
    bool QueryManufacturerString(ScopedHandle& device_handle);
    bool QueryProductString(ScopedHandle& device_handle);
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
    RID_DEVICE_INFO_HID m_HidInfo = {};
    std::string m_ManufacturerString;
    std::string m_ProductString;

    size_t m_ButtonsLength = 0;
    bool m_Buttons[kButtonsLengthCap];

    size_t m_AxesLength = 0;
    RawGamepadAxis m_Axes[kAxesLengthCap];

    // Buffer used for querying device capabilities. |m_PPD_buffer| owns the
    // memory pointed to by |m_Preparsed_data|.
    std::unique_ptr<uint8_t[]> m_PPDBuffer;
    PHIDP_PREPARSED_DATA m_PreparsedData = nullptr;
};

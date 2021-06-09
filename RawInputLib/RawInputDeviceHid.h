#pragma once

#include "RawInputDevice.h"

#include <hidsdi.h>

class RawInputDeviceManager;

class RawInputDeviceHid : public RawInputDevice
{
    friend class RawInputDeviceFactory<RawInputDeviceHid>;

    static constexpr size_t kAxesLengthCap = 16;
    static constexpr size_t kButtonsLengthCap = 32;
    static constexpr uint8_t kInvalidXInputUserId = 0xff; // XUSER_INDEX_ANY

public:
    ~RawInputDeviceHid();

    RawInputDeviceHid(RawInputDeviceHid&) = delete;
    void operator=(RawInputDeviceHid) = delete;

    uint16_t GetUsagePage() const { return m_UsagePage; }
    uint16_t GetUsageId() const { return m_UsageId; }

    bool IsXInputDevice() const { return !m_XInputInterfacePath.empty(); }
    uint8_t GetXInputUserIndex() const { return m_XInputUserIndex; }
    std::string GetXInputInterfacePath() const { return m_XInputInterfacePath; }

    bool IsXboxGipDevice() const { return !m_XboxGipInterfacePath.empty(); }

    bool IsBluetoothLEDevice() const { return !m_BluetoothLEInterfacePath.empty(); }

protected:
    RawInputDeviceHid(HANDLE handle);

    void OnInput(const RAWINPUT* input) override;

    bool QueryDeviceInfo() override;

    bool QueryDeviceCapabilities();
    void QueryButtonCapabilities(uint16_t button_count);
    void QueryNormalButtonCapabilities(HIDP_BUTTON_CAPS button_caps[], uint16_t button_count, std::vector<bool>* button_indices_used);
    void QueryAxisCapabilities(uint16_t axis_count);

    bool QueryXInputDeviceInterface();
    bool QueryXInputDeviceInfo();

    bool QueryXboxGIPDeviceInterface();
    bool QueryXboxGIPDeviceInfo();

    bool QueryBluetoothLEDeviceInterface();
    bool QueryBluetoothLEDeviceInfo();

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
    // HID top-level collection's info
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

    std::string m_XInputInterfacePath;

    // Index of the XInput controller. Can be a value in the range 0–3.
    // Or |kInvalidXInputUserId| if not an XInput controller.
    // https://docs.microsoft.com/windows/win32/xinput/getting-started-with-xinput#multiple-controllers
    uint8_t m_XInputUserIndex = kInvalidXInputUserId; // XUSER_INDEX_ANY

    std::string m_XboxGipInterfacePath;

    std::string m_BluetoothLEInterfacePath;
};

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

private:
    struct KeyboardInfo
    {
        bool QueryInfo(HANDLE rawInputDeviceHandle);

        uint16_t Type = 0;
        uint16_t SubType = 0;
        uint16_t KeyboardMode = 0;
        uint16_t NumberOfFunctionKeys = 0;
        uint16_t NumberOfIndicators = 0;
        uint16_t NumberOfKeysTotal = 0;
    } m_KeyboardInfo;

    struct ExtendedKeyboardInfo
    {
        bool QueryInfo(const ScopedHandle& interfaceHandle);

        uint8_t FormFactor = 0;
        uint8_t KeyType = 0;
        uint8_t PhysicalLayout = 0;
        uint8_t VendorSpecificPhysicalLayout = 0;
        uint8_t IETFLanguageTagIndex = 0;
        uint8_t ImplementedInputAssistControls = 0;
    } m_ExtendedKeyboardInfo;

    std::array<uint8_t, 256> m_KeyState;
};

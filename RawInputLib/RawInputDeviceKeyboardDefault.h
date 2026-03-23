#pragma once

#include "RawInputDeviceKeyboard.h"

// Aggregated keyboard device that receives input from all physical keyboards.
// Unlike RawInputDeviceKeyboard, this device:
//   - has no physical handle (NULL)
//   - generates character events via ToUnicodeEx
//   - tracks keyboard layout changes
//
// One instance is held by RawInputDeviceManager and receives every
// WM_INPUT keyboard message regardless of which physical device sent it.
class RawInputDeviceKeyboardDefault final : public RawInputDeviceKeyboard
{
    friend class RawInputDeviceManager;

public:
    ~RawInputDeviceKeyboardDefault() = default;

    RawInputDeviceKeyboardDefault(RawInputDeviceKeyboardDefault&) = delete;
    void operator=(RawInputDeviceKeyboardDefault) = delete;

protected:
    explicit RawInputDeviceKeyboardDefault();

    bool Initialize() override;

    void OnInput(const RAWINPUT* input) override;
    void OnInputLanguageChanged(HKL hkl) override;

private:
    std::array<uint8_t, 256> m_KeyState;
    HKL m_CurrentHKL = nullptr;
    wchar_t m_PendingSurrogate = 0;
};
#pragma once

#include "RawInputDevice.h"

#include <hidsdi.h>
#include <hidpi.h>

class RawInputDeviceManager;

class RawInputDeviceHid : public RawInputDevice
{
    static constexpr size_t  kAxesLengthCap = 16;
    static constexpr size_t  kButtonsLengthCap = 32;
    static constexpr size_t  kHatsLengthCap = 4;

public:
    ~RawInputDeviceHid();

    RawInputDeviceHid(RawInputDeviceHid&) = delete;
    void operator=(RawInputDeviceHid) = delete;

    // Inspects the HID descriptor and constructs the appropriate subclass:
    // RawInputDeviceGamepad, RawInputDeviceWheel, or RawInputDeviceHid.
    // Returns nullptr if the device cannot be initialised.
    static std::unique_ptr<RawInputDevice> Create(HANDLE handle);

    uint32_t GetType() const override { return RIM_TYPEHID; }

    uint16_t GetUsagePage() const { return m_UsagePage; }
    uint16_t GetUsageId()   const { return m_UsageId; }

    // Normalised axis value: [-1, +1] for absolute, raw delta for relative.
    float   GetAxis(size_t i)   const { return i < m_AxesLength ? m_Axes[i].value : 0.f; }
    bool    GetButton(size_t i) const { return i < m_ButtonsLength ? m_Buttons[i] : false; }
    // Hat / POV in hundredths of a degree [0, 35900], or -1 = centred.
    int32_t GetHat(size_t i)    const { return i < m_HatsLength ? m_Hats[i].value : -1; }

    size_t GetAxesLength()    const { return m_AxesLength; }
    size_t GetButtonsLength() const { return m_ButtonsLength; }
    size_t GetHatsLength()    const { return m_HatsLength; }

protected:
    explicit RawInputDeviceHid(HANDLE handle);

    void OnInput(const RAWINPUT* input) override;
    bool QueryDeviceInfo() override;

    struct AxisState
    {
        // ---- hot path ----
        float   value = 0.f;
        int32_t logicalMin = 0;
        int32_t logicalMax = 0;
        int32_t signMask = 0;
        bool    isSigned = false;
        bool    isAbsolute = true;

        // ---- metadata for subclasses ----
        uint16_t usagePage = 0;
        uint16_t usage = 0;
        int32_t  physicalMin = 0;
        int32_t  physicalMax = 0;
        uint32_t units = 0;
        int16_t  unitsExp = 0;
    };

    struct HatState
    {
        int32_t  value = -1;
        int32_t  logicalMin = 0;
        int32_t  logicalMax = 0;
        uint16_t povGranularity = 0;
    };

    const AxisState& GetAxisState(size_t i) const { return m_Axes[i]; }
    const HatState& GetHatState(size_t i)  const { return m_Hats[i]; }

private:
    bool QueryDeviceCapabilities();
    void QueryButtonCapabilities(uint16_t count);
    void QueryAxisCapabilities(uint16_t count);

    enum class Kind : uint8_t { None, Axis, Hat, Button };
    struct DataIndexEntry
    {
        Kind    kind = Kind::None;
        uint8_t index = 0;
        uint8_t reportId = 0;
    };

    std::unordered_map<uint8_t, std::vector<bool>> m_ButtonReportMasks;

    struct InputReport
    {
        std::vector<HIDP_DATA> parsedData;
        uint32_t               maxCount = 0;
    };

    struct PreparsedData
    {
        std::unique_ptr<uint8_t[]> buffer;
        PHIDP_PREPARSED_DATA       data = nullptr;

        bool Load(HANDLE handle);
        explicit operator bool() const { return data != nullptr; }
    };

    uint16_t m_UsagePage = 0;
    uint16_t m_UsageId = 0;

	PreparsedData m_PreparsedData;

    size_t    m_AxesLength = 0;
    AxisState m_Axes[kAxesLengthCap]{};

    size_t m_ButtonsLength = 0;
    bool   m_Buttons[kButtonsLengthCap]{};

    size_t   m_HatsLength = 0;
    HatState m_Hats[kHatsLengthCap]{};

    std::vector<DataIndexEntry> m_DataIndexTable;
    InputReport                 m_InputReport;
};
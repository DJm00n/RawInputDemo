#include "pch.h"
#include "framework.h"

#include "RawInputDeviceHid.h"
#include "CfgMgr32Wrapper.h"

#include <hidusage.h>

#include <winioctl.h>
#include <usbioctl.h>

namespace
{
    unsigned long GetBitmask(unsigned short bits)
    {
        return static_cast<unsigned long>(1 << bits) - 1;
    }
}

RawInputDeviceHid::RawInputDeviceHid(HANDLE handle)
    : RawInputDevice(handle)
{
    m_IsValid = QueryDeviceInfo();

    //DBGPRINT("New HID device Interface: %s", GetInterfacePath().c_str());
}

RawInputDeviceHid::~RawInputDeviceHid()
{
    //DBGPRINT("Removed HID device: '%s', Interface: `%s`", GetProductString().c_str(), GetInterfacePath().c_str());
}

inline void HexDump2(const uint8_t* src, size_t len) {
    for (size_t i = 0; i < len; i++) {
        std::cout << std::format("{:02d} ", src[i]);
    }
    std::cout << std::format("({} bytes)\n", (int)len);
}

void RawInputDeviceHid::OnInput(const RAWINPUT* input)
{
    //TODO

    if (input == nullptr || input->header.dwType != RIM_TYPEHID)
    {
        DBGPRINT("Wrong HID input.");
        return;
    }

    //const RAWHID& hid = input->data.hid;

    /*for (DWORD i = 0; i < hid.dwCount; ++i)
    {
        size_t reportSize = hid.dwSizeHid;
        const uint8_t* report = (hid.bRawData + (i * reportSize));

        fmt::print("HID Input Report:\n");
        HexDump2(report, reportSize);
    }*/
}

bool RawInputDeviceHid::QueryDeviceInfo()
{
    if (!RawInputDevice::QueryDeviceInfo())
        return false;

    // We can now use the name to query the OS for a file handle that is used to
    // read the product string from the device. If the OS does not return a valid
    // handle this device is invalid.
    if (!IsValidHandle(m_InterfaceHandle.get()))
        return false;

    // Fetch information about the buttons and axes on this device. This sets
    // |m_ButtonsLength| and |m_AxesLength| to their correct values and populates
    // |m_Axes| with capabilities info.
    if (!QueryDeviceCapabilities())
        return false;

    // Gamepads must have at least one button or axis.
    //if (m_ButtonsLength == 0 && m_AxesLength == 0)
    //    return false;

    // optional XInput device info
    if (QueryXInputDeviceInterface() && !QueryXInputDeviceInfo())
    {
        DBGPRINT("Cannot get XInput info from '%s' interface.", m_XInputInterfacePath.c_str());
        return false;
    }

    // optional Xbox One GIP device info
    if (QueryXboxGIPDeviceInterface() && !QueryXboxGIPDeviceInfo())
    {
        DBGPRINT("Cannot get Xbox One GIP info from '%s' interface.", m_XboxGipInterfacePath.c_str());
        return false;
    }

    // optional Bluetooth LE device info
    if (QueryBluetoothLEDeviceInterface() && !QueryBluetoothLEDeviceInfo())
    {
        DBGPRINT("Cannot get Bluetooth LE info from '%s' interface.", m_BluetoothLEInterfacePath.c_str());
        return false;
    }

    std::string parentDeviceInstanceId = GetParentDevice(m_DeviceInstanceId);
    parentDeviceInstanceId = GetParentDevice(parentDeviceInstanceId);
    DEVINST devNodeHandle = OpenDevNode(parentDeviceInstanceId);
    auto deviceStack = PropertyDataCast<std::vector<std::string>>(GetDevNodeProperty(devNodeHandle, &DEVPKEY_Device_Stack, DEVPROP_TYPE_STRING_LIST));

    auto busTypeGuid = PropertyDataCast<GUID>(GetDevNodeProperty(devNodeHandle, &DEVPKEY_Device_BusTypeGuid, DEVPROP_TYPE_GUID));

    auto devClass = PropertyDataCast<std::string>(GetDevNodeProperty(devNodeHandle, &DEVPKEY_Device_Class, DEVPROP_TYPE_STRING));

    return true;
}

bool RawInputDeviceHid::QueryDeviceCapabilities()
{
    UINT size = 0;

    UINT result = ::GetRawInputDeviceInfoW(m_Handle, RIDI_PREPARSEDDATA, nullptr, &size);
    if (result == static_cast<UINT>(-1))
    {
        //PLOG(ERROR) << "GetRawInputDeviceInfo() failed";
        return false;
    }
    DCHECK_EQ(0u, result);

    m_PPDBuffer = std::make_unique<uint8_t[]>(size);
    m_PreparsedData = reinterpret_cast<PHIDP_PREPARSED_DATA>(m_PPDBuffer.get());
    result = ::GetRawInputDeviceInfoW(m_Handle, RIDI_PREPARSEDDATA, m_PPDBuffer.get(), &size);
    if (result == static_cast<UINT>(-1))
    {
        //PLOG(ERROR) << "GetRawInputDeviceInfo() failed";
        return false;
    }
    DCHECK_EQ(size, result);

    HIDP_CAPS caps;
    NTSTATUS status = HidP_GetCaps(m_PreparsedData, &caps);
    DCHECK_EQ(HIDP_STATUS_SUCCESS, status);

    m_UsagePage = caps.UsagePage;
    m_UsageId = caps.Usage;

    if (caps.NumberInputButtonCaps > 0)
        QueryButtonCapabilities(caps.NumberInputButtonCaps);

    if (caps.NumberInputValueCaps > 0)
        QueryAxisCapabilities(caps.NumberInputValueCaps);

    return true;
}

void RawInputDeviceHid::QueryButtonCapabilities(uint16_t button_count)
{
    if (button_count > 0)
    {
        auto button_caps = std::make_unique<HIDP_BUTTON_CAPS[]>(button_count);

        NTSTATUS status = HidP_GetButtonCaps(HidP_Input, button_caps.get(), &button_count, m_PreparsedData);
        DCHECK_EQ(HIDP_STATUS_SUCCESS, status);

        // Keep track of which button indices are in use.
        std::vector<bool> button_indices_used(kButtonsLengthCap, false);

        // Collect all inputs from the Button usage page.
        QueryNormalButtonCapabilities(button_caps.get(), button_count, &button_indices_used);
    }
}

void RawInputDeviceHid::QueryNormalButtonCapabilities(HIDP_BUTTON_CAPS button_caps[], uint16_t button_count, std::vector<bool> * button_indices_used)
{
    DCHECK(button_caps);
    DCHECK(button_indices_used);

    // Collect all inputs from the Button usage page and assign button indices
    // based on the usage value.
    for (size_t i = 0; i < button_count; ++i)
    {
        uint16_t usage_page = button_caps[i].UsagePage;
        uint16_t usage_min = button_caps[i].Range.UsageMin;
        uint16_t usage_max = button_caps[i].Range.UsageMax;

        if (usage_min == 0 || usage_max == 0)
            continue;

        size_t button_index_min = usage_min - 1;
        size_t button_index_max = usage_max - 1;
        if (usage_page == HID_USAGE_PAGE_BUTTON && button_index_min < kButtonsLengthCap)
        {
            button_index_max = std::min(kButtonsLengthCap - 1, button_index_max);
            m_ButtonsLength = std::max(m_ButtonsLength, button_index_max + 1);
            for (size_t j = button_index_min; j <= button_index_max; ++j)
                (*button_indices_used)[j] = true;
        }
    }
}

void RawInputDeviceHid::QueryAxisCapabilities(uint16_t axis_count)
{
    auto axes = std::make_unique<HIDP_VALUE_CAPS[]>(axis_count);

    NTSTATUS status = HidP_GetValueCaps(HidP_Input, axes.get(), &axis_count, m_PreparsedData);

    DCHECK_EQ(HIDP_STATUS_SUCCESS, status);

    bool mapped_all_axes = true;

    for (size_t i = 0; i < axis_count; i++)
    {
        size_t axis_index = axes[i].Range.UsageMin - HID_USAGE_GENERIC_X;
        if (axis_index < kAxesLengthCap && !m_Axes[axis_index].active)
        {
            m_Axes[axis_index].caps = axes[i];
            m_Axes[axis_index].value = 0;
            m_Axes[axis_index].active = true;
            m_Axes[axis_index].bitmask = GetBitmask(axes[i].BitSize);
            m_AxesLength = std::max(m_AxesLength, axis_index + 1);
        }
        else
        {
            mapped_all_axes = false;
        }
    }

    if (!mapped_all_axes)
    {
        // For axes whose usage puts them outside the standard axesLengthCap range.
        size_t next_index = 0;
        for (size_t i = 0; i < axis_count; i++)
        {
            size_t usage = axes[i].Range.UsageMin - HID_USAGE_GENERIC_X;
            if (usage >= kAxesLengthCap &&
                axes[i].UsagePage <= HID_USAGE_PAGE_GAME)
            {
                for (; next_index < kAxesLengthCap; ++next_index)
                {
                    if (!m_Axes[next_index].active)
                        break;
                }
                if (next_index < kAxesLengthCap)
                {
                    m_Axes[next_index].caps = axes[i];
                    m_Axes[next_index].value = 0;
                    m_Axes[next_index].active = true;
                    m_Axes[next_index].bitmask = GetBitmask(axes[i].BitSize);
                    m_AxesLength = std::max(m_AxesLength, next_index + 1);
                }
            }

            if (next_index >= kAxesLengthCap)
                break;
        }
    }
}

bool RawInputDeviceHid::QueryXInputDeviceInterface()
{
    DCHECK(IsValidHandle(m_InterfaceHandle.get()));

    // https://docs.microsoft.com/windows/win32/xinput/xinput-and-directinput
    stringutils::ci_string tmp(m_InterfacePath.c_str(), m_InterfacePath.size());
    if (tmp.find("IG_") == stringutils::ci_string::npos)
        return false;

    // Xbox 360 XUSB Interface
    // {EC87F1E3-C13B-4100-B5F7-8B84D54260CB}
    static constexpr GUID XUSB_INTERFACE_CLASS_GUID = { 0xEC87F1E3, 0xC13B, 0x4100, { 0xB5, 0xF7, 0x8B, 0x84, 0xD5, 0x42, 0x60, 0xCB } };

    m_XInputInterfacePath = SearchParentDeviceInterface(m_DeviceInstanceId, &XUSB_INTERFACE_CLASS_GUID);

    return !m_XInputInterfacePath.empty();
}

bool RawInputDeviceHid::QueryXInputDeviceInfo()
{
    if (m_XInputInterfacePath.empty())
        return false;

    std::string deviceInstanceId = GetDeviceFromInterface(m_XInputInterfacePath);

    ScopedHandle XInputInterfaceHandle = OpenDeviceInterface(m_XInputInterfacePath);

    if (!IsValidHandle(XInputInterfaceHandle.get()))
        return false;

    std::array<uint8_t, 3> gamepadStateRequest0101{ 0x01, 0x01, 0x00 };
    std::array<uint8_t, 3> ledStateData;
    DWORD len = 0;

    // https://github.com/nefarius/XInputHooker/issues/1
    // https://gist.github.com/mmozeiko/b8ccc54037a5eaf35432396feabbe435
    constexpr DWORD IOCTL_XUSB_GET_LED_STATE = 0x8000E008;

    if (!::DeviceIoControl(XInputInterfaceHandle.get(),
        IOCTL_XUSB_GET_LED_STATE,
        gamepadStateRequest0101.data(),
        static_cast<DWORD>(gamepadStateRequest0101.size()),
        ledStateData.data(),
        static_cast<DWORD>(ledStateData.size()),
        &len,
        nullptr))
    {
        // GetLastError()
        return false;
    }

    DCHECK_EQ(len, ledStateData.size());

    // https://www.partsnotincluded.com/xbox-360-controller-led-animations-info/
    // https://github.com/paroj/xpad/blob/5978d1020344c3288701ef70ea9a54dfc3312733/xpad.c#L1382-L1402
    constexpr uint8_t XINPUT_LED_TO_PORT_MAP[] =
    {
        kInvalidXInputUserId,   // All off
        kInvalidXInputUserId,   // All blinking, then previous setting
        0,                      // 1 flashes, then on
        1,                      // 2 flashes, then on
        2,                      // 3 flashes, then on
        3,                      // 4 flashes, then on
        0,                      // 1 on
        1,                      // 2 on
        2,                      // 3 on
        3,                      // 4 on
        kInvalidXInputUserId,   // Rotate
        kInvalidXInputUserId,   // Blink, based on previous setting
        kInvalidXInputUserId,   // Slow blink, based on previous setting
        kInvalidXInputUserId,   // Rotate with two lights
        kInvalidXInputUserId,   // Persistent slow all blink
        kInvalidXInputUserId,   // Blink once, then previous setting
    };

    const uint8_t ledState = ledStateData[2];

    DCHECK_LE(ledState, std::size(XINPUT_LED_TO_PORT_MAP));

    m_XInputUserIndex = XINPUT_LED_TO_PORT_MAP[ledState];

    return true;
}

bool RawInputDeviceHid::QueryXboxGIPDeviceInterface()
{
    DCHECK(IsValidHandle(m_InterfaceHandle.get()));

    // Xbox One GIP Interface
    // {020BC73C-0DCA-4EE3-96D5-AB006ADA5938}
    static constexpr GUID GUID_DEVINTERFACE_DC1_CONTROLLER = { 0x020BC73C, 0x0DCA, 0x4EE3, { 0x96, 0xD5, 0xAB, 0x00, 0x6A, 0xDA, 0x59, 0x38 } };

    m_XboxGipInterfacePath = SearchParentDeviceInterface(m_DeviceInstanceId, &GUID_DEVINTERFACE_DC1_CONTROLLER);

    return !m_XboxGipInterfacePath.empty();
}

bool RawInputDeviceHid::QueryXboxGIPDeviceInfo()
{
    // Test for XBOXGIP driver software PID
    if (!(m_VendorId == 0x045E && m_ProductId == 0x02FF))
        return false;

    /*std::string deviceInstanceId = GetDeviceFromInterface(m_XboxGipInterfacePath);

    // Get Vendor ID and Product ID from a real USB device
    // https://docs.microsoft.com/windows-hardware/drivers/install/standard-usb-identifiers
    unsigned int vid, pid;
    std::array<char, 50> serial = { 0 };
    ::sscanf(deviceInstanceId.c_str(), "USB\\VID_%04X&PID_%04X\\%s", &vid, &pid, serial.data());

    m_VendorId = static_cast<uint16_t>(vid);
    m_ProductId = static_cast<uint16_t>(pid);
    */

    std::string serial = m_UsbDevice->m_SerialNumber;

    size_t serialLen = ::strlen(serial.data());
    if (serialLen <= 12)
    {
        m_SerialNumberString.assign(serial.data(), serialLen);
    }
    else
    {
        // Serial number is in odd format
        for (int i = 0; i < serialLen; ++i)
        {
            if (i % 2 != 0)
                m_SerialNumberString.push_back(serial[i]);
        }
    }

    m_VendorId = m_UsbDevice->m_VendorId;
    m_ProductId = m_UsbDevice->m_ProductId;
    m_VersionNumber = m_UsbDevice->m_VersionNumber;

    return true;
}

bool RawInputDeviceHid::QueryBluetoothLEDeviceInterface()
{
    DCHECK(IsValidHandle(m_InterfaceHandle.get()));

    // GATT Service 0x1812 Human Interface Device
    // https://www.bluetooth.com/specifications/assigned-numbers/
    stringutils::ci_string tmp(m_InterfacePath.c_str(), m_InterfacePath.size());
    if (tmp.find("{00001812-0000-1000-8000-00805f9b34fb}") == stringutils::ci_string::npos)
        return false;

    // Bluetooth LE Service device interface GUID
    // {6e3bb679-4372-40c8-9eaa-4509df260cd8}
    static constexpr GUID GUID_BLUETOOTH_GATT_SERVICE_DEVICE_INTERFACE = { 0x6e3bb679, 0x4372, 0x40c8, { 0x9e, 0xaa, 0x45, 0x09, 0xdf, 0x26, 0x0c, 0xd8} };

    m_BluetoothLEInterfacePath = SearchParentDeviceInterface(m_DeviceInstanceId, &GUID_BLUETOOTH_GATT_SERVICE_DEVICE_INTERFACE);

    return !m_BluetoothLEInterfacePath.empty();
}

bool RawInputDeviceHid::QueryBluetoothLEDeviceInfo()
{
    if (m_BluetoothLEInterfacePath.empty())
        return false;

    std::string deviceInstanceId = GetDeviceFromInterface(m_BluetoothLEInterfacePath);
    DEVINST devNodeHandle = OpenDevNode(deviceInstanceId);

    m_ProductString = PropertyDataCast<std::string>(GetDevNodeProperty(devNodeHandle, &DEVPKEY_NAME, DEVPROP_TYPE_STRING));

    static constexpr DEVPROPKEY PKEY_DeviceInterface_Bluetooth_DeviceAddress = { { 0x2BD67D8B, 0x8BEB, 0x48D5, 0x87, 0xE0, 0x6C, 0xDA, 0x34, 0x28, 0x04, 0x0A }, 1 }; // DEVPROP_TYPE_STRING
    static constexpr DEVPROPKEY PKEY_DeviceInterface_Bluetooth_Manufacturer = { { 0x2BD67D8B, 0x8BEB, 0x48D5, 0x87, 0xE0, 0x6C, 0xDA, 0x34, 0x28, 0x04, 0x0A }, 4 }; // DEVPROP_TYPE_STRING
    static constexpr DEVPROPKEY PKEY_DeviceInterface_Bluetooth_VendorId = { { 0x2BD67D8B, 0x8BEB, 0x48D5, 0x87, 0xE0, 0x6C, 0xDA, 0x34, 0x28, 0x04, 0x0A }, 7 }; // DEVPROP_TYPE_UINT16
    static constexpr DEVPROPKEY PKEY_DeviceInterface_Bluetooth_ProductId = { { 0x2BD67D8B, 0x8BEB, 0x48D5, 0x87, 0xE0, 0x6C, 0xDA, 0x34, 0x28, 0x04, 0x0A }, 8 }; // DEVPROP_TYPE_UINT16
    static constexpr DEVPROPKEY PKEY_DeviceInterface_Bluetooth_ProductVersion = { { 0x2BD67D8B, 0x8BEB, 0x48D5, 0x87, 0xE0, 0x6C, 0xDA, 0x34, 0x28, 0x04, 0x0A }, 9 }; // DEVPROP_TYPE_UINT16

    m_ManufacturerString = PropertyDataCast<std::string>(GetDevNodeProperty(devNodeHandle, &PKEY_DeviceInterface_Bluetooth_Manufacturer, DEVPROP_TYPE_STRING));
    m_SerialNumberString = PropertyDataCast<std::string>(GetDevNodeProperty(devNodeHandle, &PKEY_DeviceInterface_Bluetooth_DeviceAddress, DEVPROP_TYPE_STRING));

    m_VendorId = PropertyDataCast<uint16_t>(GetDevNodeProperty(devNodeHandle, &PKEY_DeviceInterface_Bluetooth_VendorId, DEVPROP_TYPE_UINT16));
    m_ProductId = PropertyDataCast<uint16_t>(GetDevNodeProperty(devNodeHandle, &PKEY_DeviceInterface_Bluetooth_ProductId, DEVPROP_TYPE_UINT16));
    m_VersionNumber = PropertyDataCast<uint16_t>(GetDevNodeProperty(devNodeHandle, &PKEY_DeviceInterface_Bluetooth_ProductVersion, DEVPROP_TYPE_UINT16));

    return !m_ProductString.empty();
}

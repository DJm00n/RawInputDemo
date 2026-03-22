// RawInputInfo.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>

#include <windows.h>
#include <RawInputDeviceManager.h>
#include <RawInputDeviceHid.h>
#include <RawInputDeviceKeyboard.h>
#include <RawInputDeviceMouse.h>

#include <fmt/format.h>
#include <algorithm>

void HexDump(const uint8_t* src, size_t len) {
    if (!len)
        printf("Empty ");

    for (size_t i = 0; i < len; i++) {
        if (i % 8 == 0) {
            //printf("%04x ", uint32_t(i));
        }

        printf("%02x ", src[i]);

        if ((i + 1) % 8 == 0)
        {
            //printf("\n");
        }
    }
    //printf("\n");
    printf("(%d bytes)\n", (int)len);
}

std::string BCDVersionToString(uint16_t bcd)
{
    uint8_t major1 = ((bcd & (0xf << 16)) >> 16);
    uint8_t major2 = ((bcd & (0xf << 8)) >> 8);
    uint8_t minor1 = ((bcd & (0xf << 4)) >> 4);
    uint8_t minor2 = ((bcd & (0xf << 0)) >> 0);

    std::string ver;
    if (major1)
        ver += std::to_string(major1);

    ver += std::to_string(major2);
    ver.append(1, '.');
    ver += std::to_string(minor1);
    ver += std::to_string(minor2);

    return ver;
}

void DumpDeviceInfo(const RawInputDevice* device)
{
    fmt::print("++++++++++++++++++++++++++\n");

    const RawInputDeviceKeyboard* keyboardDevice = dynamic_cast<const RawInputDeviceKeyboard*>(device);
    const RawInputDeviceMouse* mouseDevice = dynamic_cast<const RawInputDeviceMouse*>(device);
    const RawInputDeviceHid* hidDevice = dynamic_cast<const RawInputDeviceHid*>(device);

    std::string deviceType = (keyboardDevice ? "Keyboard" : (mouseDevice ? "Mouse" : (hidDevice ? "HID" : "unknown")));


    fmt::print("New {} device: {}\n", deviceType, device->GetInterfacePath());

    fmt::print("  ->VID:{:04X},PID:{:04X},VER:{}\n", device->GetVendorId(), device->GetProductId(), BCDVersionToString(device->GetVersionNumber()));
    fmt::print("  ->Manufacturer: {}\n", device->GetManufacturerString());
    fmt::print("  ->Product: {}\n", device->GetProductString());
    fmt::print("  ->Serial Number: {}\n", device->GetSerialNumberString());

    if (hidDevice)
    {
        fmt::print("  ->UsagePage:{:04X},UsageId:{:04X}\n", hidDevice->GetUsagePage(), hidDevice->GetUsageId());

        if (hidDevice->IsXInputDevice())
        {
            fmt::print("It is XInput Device with UserID: {}\n", hidDevice->GetXInputUserIndex());
        }

        if (hidDevice->IsXboxGipDevice())
        {
            fmt::print("It is Xbox GIP Device\n");
        }

        if (hidDevice->IsBluetoothLEDevice())
        {
            fmt::print("It is BluetoothLE Device\n");
        }
    }



    if (device->IsUsbDevice())
    {
        fmt::print("It have USB Device Interface: {}\n", device->GetUsbInterfacePath());

        const std::vector<uint8_t>& configurationDesc = device->GetUsbConfigurationDescriptor();

        fmt::print("  ->USB Configuration Descriptor Dump: \n");
        HexDump(configurationDesc.data(), configurationDesc.size());

        if (device->IsHidDevice())
        {
            const std::vector<uint8_t>& hidDesc = device->GetUsbHidReportDescriptor();

            fmt::print("  ->HID Report Descriptor Dump: \n");
            HexDump(hidDesc.data(), hidDesc.size());
        }
    }

    fmt::print("--------------------------\n");
}

int main()
{
    RawInputDeviceManager rawDeviceManager;

    std::cout << "RawInputDeviceManager is working!\n";

    std::vector<std::shared_ptr<RawInputDevice>> devices;

    while (true)
    {
        std::vector<std::shared_ptr<RawInputDevice>> enumeratedDevices = rawDeviceManager.GetRawInputDevices();

        // Find new devices
        for (const auto& device : enumeratedDevices)
        {
            if (std::find(devices.begin(), devices.end(), device) == devices.end())
            {
                DumpDeviceInfo(device.get());
                devices.emplace_back(device);
            }
        }

        // Remove devices that are no longer present
        devices.erase(
            std::remove_if(devices.begin(), devices.end(),
                [&enumeratedDevices](const std::shared_ptr<RawInputDevice>& device)
                {
                    return std::find(enumeratedDevices.begin(), enumeratedDevices.end(), device)
                        == enumeratedDevices.end();
                }),
            devices.end());
    }


    return 0;
}

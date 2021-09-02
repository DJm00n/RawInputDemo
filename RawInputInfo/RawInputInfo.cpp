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

RawInputDeviceManager rawDeviceManager;


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
            fmt::print("It have XInput Device Interface: {}\n", hidDevice->GetXInputInterfacePath().c_str());
            fmt::print("  ->UserID: {}\n", hidDevice->GetXInputUserIndex());
        }

        if (hidDevice->IsXboxGipDevice())
        {
            fmt::print("It have Xbox One GIP Device Interface: {}\n", hidDevice->GetXboxGipInterfacePath());
            fmt::print("  ->Serial: {}\n", device->GetSerialNumberString());
        }

        if (hidDevice->IsBluetoothLEDevice())
        {
            fmt::print("->It have BluetoothLE Device Interface: {}\n", hidDevice->GetBluetoothLEInterfacePath());
            fmt::print("  ->MAC Address: {}\n", device->GetSerialNumberString());
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
    std::cout << "RawInputDeviceManager is working!\n";

    std::vector<RawInputDevice*> devices;


    while (true)
    {
        std::vector<RawInputDevice*> enumeratedDevices;
        for (RawInputDevice* device : rawDeviceManager.GetRawInputDevices())
        {
            auto deviceIt = std::find(std::begin(devices), std::end(devices), device);
            if (deviceIt == devices.end())
            {
                // new device
                DumpDeviceInfo(device);

                devices.emplace_back(device);
            }

            enumeratedDevices.emplace_back(device);
        }


        // Clear out old devices that weren't part of this enumeration pass.
        auto deviceIt = devices.begin();
        while (deviceIt != devices.end())
        {
            if (std::find(std::begin(enumeratedDevices), std::end(enumeratedDevices), *deviceIt) == enumeratedDevices.end())
            {
                // removed device
                deviceIt = devices.erase(deviceIt);
            }
            else
            {
                ++deviceIt;
            }
        }

    }


    return 0;
}

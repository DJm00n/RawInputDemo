#include "pch.h"
#include "framework.h"

#include "RawInputDeviceManager.h"

#include "RawInputDevice.h"

RawInputDeviceManager::RawInputDeviceManager()
{

}

void RawInputDeviceManager::Register(HWND hWndTarget)
{
    RAWINPUTDEVICE rid[1];

    rid[0].usUsagePage = HID_USAGE_PAGE_GENERIC;
    rid[0].usUsage = 0;
    rid[0].dwFlags = RIDEV_PAGEONLY | RIDEV_DEVNOTIFY | RIDEV_INPUTSINK;
    rid[0].hwndTarget = hWndTarget;

    if (!RegisterRawInputDevices(rid, ARRAYSIZE(rid), sizeof(RAWINPUTDEVICE)))
    {
        DBGPRINT("Cannot register raw input devices.");
    }
}

void RawInputDeviceManager::Unregister()
{
    RAWINPUTDEVICE rid[1];

    rid[0].usUsagePage = HID_USAGE_PAGE_GENERIC;
    rid[0].usUsage = 0;
    rid[0].dwFlags = RIDEV_REMOVE | RIDEV_PAGEONLY;
    rid[0].hwndTarget = 0;

    if (!RegisterRawInputDevices(rid, ARRAYSIZE(rid), sizeof(RAWINPUTDEVICE)))
    {
        DBGPRINT("Cannot unregister raw input devices.");
    }
}

void RawInputDeviceManager::EnumerateDevices()
{
    UINT count = 0;
    UINT result = ::GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST));
    if (result == static_cast<UINT>(-1)) {
        //PLOG(ERROR) << "GetRawInputDeviceList() failed";
        return;
    }
    DCHECK_EQ(0u, result);

    std::unique_ptr<RAWINPUTDEVICELIST[]> device_list(new RAWINPUTDEVICELIST[count]);
    result = ::GetRawInputDeviceList(device_list.get(), &count, sizeof(RAWINPUTDEVICELIST));
    if (result == static_cast<UINT>(-1)) {
        //PLOG(ERROR) << "GetRawInputDeviceList() failed";
        return;
    }
    DCHECK_EQ(count, result);

    std::unordered_set<HANDLE> enumerated_device_handles;
    for (UINT i = 0; i < count; ++i)
    {
        HANDLE device_hDevice = device_list[i].hDevice;
        auto controller_it = m_Devices.find(device_hDevice);

        RawInputDevice* device;
        if (controller_it != m_Devices.end())
        {
            device = controller_it->second.get();
        }
        else
        {
            auto new_device = RawInputDevice::CreateRawInputDevice(device_hDevice);
            if (!new_device->IsValid())
            {
                continue;
            }

            auto emplace_result = m_Devices.emplace(device_hDevice, std::move(new_device));
            device = emplace_result.first->second.get();

            // TODO LOG
        }

        enumerated_device_handles.insert(device_hDevice);
    }

    // Clear out old controllers that weren't part of this enumeration pass.
    auto controller_it = m_Devices.begin();
    while (controller_it != m_Devices.end())
    {
        if (enumerated_device_handles.find(controller_it->first) == enumerated_device_handles.end())
        {
            controller_it = m_Devices.erase(controller_it);
        }
        else
        {
            ++controller_it;
        }
    }
}

void RawInputDeviceManager::OnInput(HWND /*hWndInput*/, UINT /*rimCode*/, HRAWINPUT hRawInput)
{
    UINT size = 0;
    UINT result = GetRawInputData(hRawInput, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));

    if (result == static_cast<UINT>(-1)) {
        //PLOG(ERROR) << "GetRawInputData() failed";
        return;
    }
    DCHECK_EQ(0u, result);

    std::unique_ptr<uint8_t[]> buffer(new uint8_t[size]);
    RAWINPUT* input = reinterpret_cast<RAWINPUT*>(buffer.get());

    result = GetRawInputData(hRawInput, RID_INPUT, input, &size, sizeof(RAWINPUTHEADER));

    if (result == static_cast<UINT>(-1)) {
        //PLOG(ERROR) << "GetRawInputData() failed";
        return;
    }
    DCHECK_EQ(size, result);

    // Notify device about the event
    if (input->header.hDevice != nullptr)
    {
        auto it = m_Devices.find(input->header.hDevice);
        if (it == m_Devices.end())
        {
            //PLOG(ERROR) << "Device not found";
            return;
        }

        it->second->OnInput(input);
    }
}

void RawInputDeviceManager::OnInputDeviceChange(HWND /*hWndInput*/, UINT gidcCode, HANDLE hDevice)
{
    DCHECK(gidcCode == GIDC_ARRIVAL || gidcCode == GIDC_REMOVAL);
    CHECK_NE(hDevice, nullptr);

    if (gidcCode == GIDC_ARRIVAL)
    {
        if (m_Devices.find(hDevice) != m_Devices.end())
        {
            //PLOG(ERROR) << "Device already exist";
            return;
        }

        m_Devices.emplace(hDevice, RawInputDevice::CreateRawInputDevice(hDevice));
    }
    else
    {
        auto it = m_Devices.find(hDevice);
        if (it == m_Devices.end())
        {
            //PLOG(ERROR) << "Device not found";
            return;
        }

        m_Devices.erase(it);
    }
}


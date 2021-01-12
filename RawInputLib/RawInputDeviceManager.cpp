#include "pch.h"
#include "framework.h"

#include "RawInputDeviceManager.h"

#include "RawInputDevice.h"
#include "RawInputDeviceMouse.h"
#include "RawInputDeviceKeyboard.h"
#include "RawInputDeviceHid.h"

RawInputDeviceManager::RawInputDeviceManager()
{

}

void RawInputDeviceManager::Register(HWND hWnd)
{
    RAWINPUTDEVICE rid[1];

    rid[0].usUsagePage = HID_USAGE_PAGE_GENERIC;
    rid[0].usUsage = 0;
    rid[0].dwFlags = RIDEV_PAGEONLY | RIDEV_DEVNOTIFY | RIDEV_INPUTSINK;
    rid[0].hwndTarget = hWnd;

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
    if (result == static_cast<UINT>(-1))
    {
        //PLOG(ERROR) << "GetRawInputDeviceList() failed";
        return;
    }
    DCHECK_EQ(0u, result);

    auto device_list = std::make_unique<RAWINPUTDEVICELIST[]>(count);
    result = ::GetRawInputDeviceList(device_list.get(), &count, sizeof(RAWINPUTDEVICELIST));
    if (result == static_cast<UINT>(-1))
    {
        //PLOG(ERROR) << "GetRawInputDeviceList() failed";
        return;
    }
    DCHECK_EQ(count, result);

    std::unordered_set<HANDLE> enumerated_device_handles;
    for (UINT i = 0; i < count; ++i)
    {
        const HANDLE device_handle = device_list[i].hDevice;
        const DWORD device_type = device_list[i].dwType;
        auto controller_it = m_Devices.find(device_handle);

        RawInputDevice* device;
        if (controller_it != m_Devices.end())
        {
            device = controller_it->second.get();
        }
        else
        {

            auto new_device = CreateRawInputDevice(device_handle, device_type);
            if (!new_device || !new_device->IsValid())
            {
                continue;
            }

            auto emplace_result = m_Devices.emplace(device_handle, std::move(new_device));
            device = emplace_result.first->second.get();

            // TODO LOG
        }

        enumerated_device_handles.insert(device_handle);
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

void RawInputDeviceManager::OnInput(HWND /*hWnd*/, UINT /*rimCode*/, HRAWINPUT rawInputHandle)
{
    // rimCode could be:
    // RIM_INPUT - foreground input
    // RIM_INPUTSINK - background input

    UINT size = 0;
    UINT result = GetRawInputData(rawInputHandle, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));

    if (result == static_cast<UINT>(-1))
    {
        //PLOG(ERROR) << "GetRawInputData() failed";
        return;
    }
    DCHECK_EQ(0u, result);

    auto buffer = std::make_unique<uint8_t[]>(size);
    RAWINPUT* input = reinterpret_cast<RAWINPUT*>(buffer.get());

    result = GetRawInputData(rawInputHandle, RID_INPUT, input, &size, sizeof(RAWINPUTHEADER));

    if (result == static_cast<UINT>(-1)) {
        //PLOG(ERROR) << "GetRawInputData() failed";
        return;
    }
    DCHECK_EQ(size, result);

    // Notify device about the event
    if (IsValidHandle(input->header.hDevice))
    {
        auto it = m_Devices.find(input->header.hDevice);
        if (it == m_Devices.end())
        {
            DBGPRINT("Device 0x%x not found", input->header.hDevice);
            return;
        }

        it->second->OnInput(input);
    }
}

void RawInputDeviceManager::OnInputDeviceChange(HWND /*hWnd*/, UINT gidcCode, HANDLE handle)
{
    DCHECK(gidcCode == GIDC_ARRIVAL || gidcCode == GIDC_REMOVAL);
    CHECK_NE(handle, nullptr);
    CHECK(IsValidHandle(handle));

    if (gidcCode == GIDC_ARRIVAL)
    {
        if (m_Devices.find(handle) != m_Devices.end())
        {
            DBGPRINT("Device 0x%x already exist", handle);
            return;
        }

        RID_DEVICE_INFO info = {};
        if (!RawInputDevice::QueryRawDeviceInfo(handle, &info))
        {
            DBGPRINT("Cannot get Extended Keyboard info from 0x%x", handle);
            return;
        }

        auto new_device = CreateRawInputDevice(handle, info.dwType);

        if (!new_device || !new_device->IsValid())
        {
            DBGPRINT("Error while creating device of type %d", info.dwType);
            return;
        }

        m_Devices.emplace(handle, std::move(new_device));
    }
    else
    {
        auto it = m_Devices.find(handle);
        if (it == m_Devices.end())
        {
            DBGPRINT("Device 0x%x not found", handle);
            return;
        }

        m_Devices.erase(it);
    }
}

std::unique_ptr<RawInputDevice> RawInputDeviceManager::CreateRawInputDevice(HANDLE handle, DWORD deviceType)
{
    switch (deviceType)
    {
    case RIM_TYPEMOUSE:
        return std::make_unique<RawInputDeviceMouse>(handle);
    case RIM_TYPEKEYBOARD:
        return std::make_unique<RawInputDeviceKeyboard>(handle);
    case RIM_TYPEHID:
        return std::make_unique<RawInputDeviceHid>(handle);
    }

    DBGPRINT("Unknown device type %d.", deviceType);

    return nullptr;
}


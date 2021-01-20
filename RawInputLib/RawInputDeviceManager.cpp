#include "pch.h"
#include "framework.h"

#include "RawInputDeviceManager.h"

#include "RawInputDevice.h"
#include "RawInputDeviceMouse.h"
#include "RawInputDeviceKeyboard.h"
#include "RawInputDeviceHid.h"

namespace
{
    void DumpInfo(const RawInputDevice* device)
    {
        //DBGPRINT("Interface path: %s", device->GetInterfacePath().c_str());
        //DBGPRINT("Manufacturer String: %s", device->GetManufacturerString().c_str());
        //DBGPRINT("Product String: %s", device->GetProductString().c_str());
        //DBGPRINT("IsHidDevice: %d", device->IsHidDevice());
        //DBGPRINT("VID/PID: [%04X:%04X]", device->GetVendorId(), device->GetProductId());
        //DBGPRINT("GetProductId: %d", );
        //DBGPRINT("GetVersionNumber: %d", device->GetVersionNumber());
    }
}

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

            auto new_device = CreateRawInputDevice(device_type, device_handle);
            if (!new_device || !new_device->IsValid())
            {
                DBGPRINT("Invalid device: '%d'", device_handle);
                continue;
            }

            auto emplace_result = m_Devices.emplace(device_handle, std::move(new_device));
            device = emplace_result.first->second.get();

            // TODO LOG
            DumpInfo(device);
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

void RawInputDeviceManager::OnInput(HWND hWnd, UINT rimCode, HRAWINPUT dataHandle)
{
    CHECK(IsValidHandle(hWnd));
    DCHECK(rimCode == RIM_INPUT || rimCode == RIM_INPUTSINK);
    CHECK(IsValidHandle(dataHandle));

    UINT size = 0;
    UINT result = GetRawInputData(dataHandle, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));

    if (result == static_cast<UINT>(-1))
    {
        //PLOG(ERROR) << "GetRawInputData() failed";
        return;
    }
    DCHECK_EQ(0u, result);

    auto buffer = std::make_unique<uint8_t[]>(size);
    RAWINPUT* input = reinterpret_cast<RAWINPUT*>(buffer.get());

    result = GetRawInputData(dataHandle, RID_INPUT, input, &size, sizeof(RAWINPUTHEADER));

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

void RawInputDeviceManager::OnInputDeviceChange(HWND hWnd, UINT gidcCode, HANDLE handle)
{
    CHECK(IsValidHandle(hWnd));
    DCHECK(gidcCode == GIDC_ARRIVAL || gidcCode == GIDC_REMOVAL);
    CHECK(IsValidHandle(handle));

    if (gidcCode == GIDC_ARRIVAL)
    {
        if (m_Devices.find(handle) != m_Devices.end())
        {
            DBGPRINT("Device 0x%x already exist", handle);
            return;
        }

        RID_DEVICE_INFO info = {};
        UINT size = sizeof(info);
        UINT result = ::GetRawInputDeviceInfoW(handle, RIDI_DEVICEINFO, &info, &size);

        if (result == static_cast<UINT>(-1))
        {
            DBGPRINT("GetRawInputDeviceInfo() failed for 0x%x handle", handle);
            return;
        }
        DCHECK_EQ(size, result);

        auto new_device = CreateRawInputDevice(info.dwType, handle);

        if (!new_device || !new_device->IsValid())
        {
            DBGPRINT("Error while creating device of type %d", info.dwType);
            return;
        }

        // TODO LOG
        DumpInfo(new_device.get());

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

std::unique_ptr<RawInputDevice> RawInputDeviceManager::CreateRawInputDevice(DWORD deviceType, HANDLE handle) const
{
    switch (deviceType)
    {
    case RIM_TYPEMOUSE:
        return RawInputDeviceFactory<RawInputDeviceMouse>().Create(handle);
    case RIM_TYPEKEYBOARD:
        return RawInputDeviceFactory<RawInputDeviceKeyboard>().Create(handle);
    case RIM_TYPEHID:
        return RawInputDeviceFactory<RawInputDeviceHid>().Create(handle);
    }

    DBGPRINT("Unknown device type %d.", deviceType);

    return nullptr;
}


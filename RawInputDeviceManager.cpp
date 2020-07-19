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
        DBGPRINT(L"Cannot register raw input devices.");
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
        DBGPRINT(L"Cannot unregister raw input devices.");
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
    //DCHECK_EQ(0u, result);

    std::unique_ptr<RAWINPUTDEVICELIST[]> device_list(new RAWINPUTDEVICELIST[count]);
    result = ::GetRawInputDeviceList(device_list.get(), &count, sizeof(RAWINPUTDEVICELIST));
    if (result == static_cast<UINT>(-1)) {
        //PLOG(ERROR) << "GetRawInputDeviceList() failed";
        return;
    }
    //DCHECK_EQ(count, result);

    std::unordered_set<HANDLE> enumerated_device_handles;
    for (UINT i = 0; i < count; ++i)
    {
        HANDLE device_handle = device_list[i].hDevice;
        auto controller_it = m_Devices.find(device_handle);

        RawInputDevice* device;
        if (controller_it != m_Devices.end())
        {
            device = controller_it->second.get();
        }
        else
        {
            auto new_device = RawInputDevice::CreateRawInputDevice(device_list[i].dwType, device_handle);
            if (!new_device->IsValid())
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

bool RawInputDeviceManager::OnMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT* lResult)
{
    switch (message)
    {
    case WM_INPUT:
    {
        UINT code = GET_RAWINPUT_CODE_WPARAM(wParam);

        if (code != RIM_INPUT && code != RIM_INPUTSINK)
            break;

        HRAWINPUT dataHandle = (HRAWINPUT)lParam;

        *lResult = OnInput(dataHandle);

        if (*lResult == 0 && code == RIM_INPUT)
            *lResult = DefWindowProc(hWnd, message, wParam, lParam);

        return true;
    }
    case WM_INPUT_DEVICE_CHANGE:
    {
        if (wParam != GIDC_ARRIVAL && wParam != GIDC_REMOVAL)
            break;

        bool arrival = (wParam == GIDC_ARRIVAL);
        HANDLE handle = (HANDLE)lParam;

        *lResult = OnInputDeviceChange(arrival, handle);

        return true;
    }
    }

    return false;
}

LRESULT RawInputDeviceManager::OnInput(HRAWINPUT inputHandle)
{
    UINT size = 0;
    UINT result = GetRawInputData(inputHandle, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));

    if (result == static_cast<UINT>(-1)) {
        //PLOG(ERROR) << "GetRawInputData() failed";
        return 0;
    }
    //DCHECK_EQ(0u, result);

    std::unique_ptr<uint8_t[]> buffer(new uint8_t[size]);
    RAWINPUT* input = reinterpret_cast<RAWINPUT*>(buffer.get());

    result = GetRawInputData(inputHandle, RID_INPUT, input, &size, sizeof(RAWINPUTHEADER));


    if (result == static_cast<UINT>(-1)) {
        //PLOG(ERROR) << "GetRawInputData() failed";
        return 0;
    }
    //DCHECK_EQ(size, result);

    // Notify device about the event
    if (input->header.hDevice != nullptr)
    {
        auto it = m_Devices.find(input->header.hDevice);
        if (it != m_Devices.end())
            it->second->OnInput(input);
    }

    return ::DefRawInputProc(&input, 1, sizeof(RAWINPUTHEADER));
}

LRESULT RawInputDeviceManager::OnInputDeviceChange(bool arrival, HANDLE handle)
{
    if (handle == nullptr)
        return 1;

    auto it = m_Devices.find(handle);

    if (arrival)
    {
        if (it != m_Devices.end())
        {
            //PLOG(ERROR) << "Device already exist";
            return 0;
        }

        RID_DEVICE_INFO deviceInfo;
        UINT size = sizeof(RID_DEVICE_INFO);
        UINT result = GetRawInputDeviceInfo(handle, RIDI_DEVICEINFO, &deviceInfo, &size);

        if (result == static_cast<UINT>(-1)) {
            //PLOG(ERROR) << "GetRawInputDeviceInfo() failed";
            return 0;
        }
        //DCHECK_EQ(size, result);

        m_Devices.emplace(handle, RawInputDevice::CreateRawInputDevice(deviceInfo.dwType, handle));
    }
    else
    {
        if (it == m_Devices.end())
        {
            //PLOG(ERROR) << "Device not found";
            return 0;
        }

        m_Devices.erase(it);
    }

    return 0;
}


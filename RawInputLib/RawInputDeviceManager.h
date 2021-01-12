#pragma once

#include "RawInputDevice.h"

#include <unordered_map>

class RawInputDeviceManager
{
public:
    RawInputDeviceManager();

    RawInputDeviceManager(RawInputDeviceManager&) = delete;
    void operator=(RawInputDeviceManager) = delete;

    void Register(HWND hWnd);
    void Unregister();

    void EnumerateDevices();

    void OnInput(HWND hWnd, UINT rimCode, HRAWINPUT rawInputHandle);
    void OnInputDeviceChange(HWND hWnd, UINT gidcCode, HANDLE handle);

private:
    static std::unique_ptr<RawInputDevice> CreateRawInputDevice(HANDLE handle, DWORD deviceType);

    std::unordered_map<HANDLE, std::unique_ptr<RawInputDevice>> m_Devices;
};
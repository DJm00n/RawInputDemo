#pragma once

#include "RawInputDevice.h"

#include <unordered_map>

class RawInputDeviceManager
{
public:
    RawInputDeviceManager();

    RawInputDeviceManager(RawInputDeviceManager&) = delete;
    void operator=(RawInputDeviceManager) = delete;

    void Register(HWND hWndTarget);
    void Unregister();

    void EnumerateDevices();

    void OnInput(HWND hWndInput, UINT rimCode, HRAWINPUT hRawInput);
    void OnInputDeviceChange(HWND hWndInput, UINT gidcCode, HANDLE hDevice);

private:
    std::unordered_map<HANDLE, std::unique_ptr<RawInputDevice>> m_Devices;
};
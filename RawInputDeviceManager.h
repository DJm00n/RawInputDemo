#pragma once

#include "RawInputDevice.h"

class RawInputDeviceManager
{
public:
    RawInputDeviceManager();

    void Register(HWND hWndTarget);
    void Unregister();

    void EnumerateDevices();

    void OnInput(HWND hWndInput, UINT rimCode, HRAWINPUT hRawInput);
    void OnInputDeviceChange(HWND hWndInput, UINT gidcCode, HANDLE hDevice);

private:
    std::unordered_map<HANDLE, std::unique_ptr<RawInputDevice>> m_Devices;
};
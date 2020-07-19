#pragma once

#include "RawInputDevice.h"

class RawInputDeviceManager
{
public:
    RawInputDeviceManager();

    void Register(HWND hWndTarget);
    void Unregister();

    void EnumerateDevices();

    bool OnMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT* lResult);

private:
    LRESULT OnInput(HRAWINPUT hRawInput);
    LRESULT OnInputDeviceChange(bool arrival, HANDLE handle);

    std::unordered_map<HANDLE, std::unique_ptr<RawInputDevice>> m_Devices;
};
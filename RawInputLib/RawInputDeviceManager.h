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

    void OnInput(HWND hWnd, UINT rimCode, HRAWINPUT dataHandle);
    void OnInputDeviceChange(HWND hWnd, UINT gidcCode, HANDLE havndle);

private:
    void ProcessPendingInput();

    void OnInput(HANDLE handle, const RAWINPUT* input);

    std::unique_ptr<RawInputDevice> CreateRawInputDevice(DWORD deviceType, HANDLE handle) const;

    std::unordered_map<HANDLE, std::unique_ptr<RawInputDevice>> m_Devices;

    std::vector<uint8_t> m_InputDataBuffer;
};
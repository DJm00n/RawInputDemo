#include "pch.h"
#include "framework.h"

#include "RawInputDeviceManager.h"

#include "RawInputDevice.h"
#include "RawInputDeviceFactory.h"
#include "RawInputDeviceMouse.h"
#include "RawInputDeviceKeyboard.h"
#include "RawInputDeviceHid.h"

#include <array>
#include <unordered_map>
#include <thread>
#include <future>
#include <memory>

namespace
{
    void DumpInfo(const RawInputDevice* device)
    {
        DBGPRINT("Interface path: %s", device->GetInterfacePath().c_str());
        DBGPRINT("Manufacturer String: %s", device->GetManufacturerString().c_str());
        DBGPRINT("Product String: %s", device->GetProductString().c_str());

    }

    bool IsVirtualRIDDevice(const std::string& path)
    {
        // Case-insensitive, covers both keyboard and mouse variants
        stringutils::ci_string ci(path.c_str(), path.size());
        return ci.find("\\microsoft keyboard rid\\") != ci.npos
            || ci.find("\\microsoft mouse rid\\") != ci.npos;
    }

    // Window class name for the message-only sink window.
    constexpr LPCWSTR RAW_SINK_CLASS = L"RawInputSink";
}

struct RawInputDeviceManager::RawInputManagerImpl
{
    RawInputManagerImpl();
    ~RawInputManagerImpl();

    void ThreadRun(std::promise<void> readyPromise);

    bool SetDeviceEnabled(USHORT usUsage, bool enabled);
    bool Register();
    bool Unregister();

    void OnDeviceConnected(HANDLE deviceHandle);
    void OnDeviceDisconnected(HANDLE deviceHandle);

    void EnumerateDevices();

    void OnInput(const RAWINPUT* input);

    std::unique_ptr<RawInputDevice> CreateRawInputDevice(DWORD deviceType, HANDLE deviceHandle) const;

    std::thread       m_Thread;
    HWND              m_hWnd     = nullptr;

    std::vector<BYTE> m_InputBuffer;

    std::unordered_map<HANDLE, std::unique_ptr<RawInputDevice>> m_Devices;

    std::unique_ptr<RawInputDeviceKeyboard> m_DefaultKeyboard;
    std::unique_ptr<RawInputDeviceMouse>    m_DefaultMouse;
};

// ---------------------------------------------------------------------------
// RawInputManagerImpl
// ---------------------------------------------------------------------------

RawInputDeviceManager::RawInputManagerImpl::RawInputManagerImpl()
    : m_InputBuffer(sizeof(RAWINPUT) + 64, 0)
{
    // The promise/future pair replaces m_ReadyEvent (CreateEvent / WaitForSingleObject).
    // The worker thread signals readiness by calling promise.set_value(); the
    // constructor blocks on future.get() until that happens.
    std::promise<void> readyPromise;
    std::future<void>  readyFuture = readyPromise.get_future();

    m_Thread = std::thread(&RawInputManagerImpl::ThreadRun, this, std::move(readyPromise));

    // Block until the worker thread has created the window, registered
    // devices, and finished EnumerateDevices.
    readyFuture.get();
}

RawInputDeviceManager::RawInputManagerImpl::~RawInputManagerImpl()
{
    ::PostMessageW(m_hWnd, WM_QUIT, 0, 0);
    m_Thread.join();
}

void RawInputDeviceManager::RawInputManagerImpl::ThreadRun(std::promise<void> readyPromise)
{
    HINSTANCE hInstance = ::GetModuleHandleW(nullptr);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = [](HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT
        {
            if (uMsg == WM_NCCREATE)
            {
                auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
                ::SetWindowLongPtrW(hWnd, GWLP_USERDATA,
                    reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
                return ::DefWindowProcW(hWnd, uMsg, wParam, lParam);
            }

            auto* self = reinterpret_cast<RawInputManagerImpl*>(
                ::GetWindowLongPtrW(hWnd, GWLP_USERDATA));

            if (!self)
                return ::DefWindowProcW(hWnd, uMsg, wParam, lParam);

            switch (uMsg)
            {
            case WM_INPUT:
            {
                UINT size = static_cast<UINT>(self->m_InputBuffer.size());
                while (::GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam),
                    RID_INPUT, self->m_InputBuffer.data(), &size, sizeof(RAWINPUTHEADER)) == UINT_MAX)
                {
                    if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER)
                        return 0;

                    // Buffer too small — size is updated by GetRawInputData to required size
                    self->m_InputBuffer.resize(size);
                }
                self->OnInput(reinterpret_cast<RAWINPUT*>(self->m_InputBuffer.data()));
                return 0;
            }

            case WM_INPUT_DEVICE_CHANGE:
                if (wParam == GIDC_ARRIVAL)
                    self->OnDeviceConnected(reinterpret_cast<HANDLE>(lParam));
                else
                    self->OnDeviceDisconnected(reinterpret_cast<HANDLE>(lParam));
                return 0;

            case WM_INPUTLANGCHANGE:
                for (auto& [handle, device] : self->m_Devices)
                    device->OnInputLanguageChanged(reinterpret_cast<HKL>(lParam));
                return 0;
            }

            return ::DefWindowProcW(hWnd, uMsg, wParam, lParam);
        };
    wc.hInstance = hInstance;
    wc.lpszClassName = RAW_SINK_CLASS;
    ::RegisterClassExW(&wc);

    m_hWnd = ::CreateWindowExW(0, RAW_SINK_CLASS, nullptr, 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, this);
    CHECK(IsValidHandle(m_hWnd));

    CHECK(Register());

    m_DefaultKeyboard.reset(static_cast<RawInputDeviceKeyboard*>(CreateRawInputDevice(RIM_TYPEKEYBOARD, NULL).release()));
    m_DefaultMouse.reset(static_cast<RawInputDeviceMouse*>(CreateRawInputDevice(RIM_TYPEMOUSE, NULL).release()));

    EnumerateDevices();

    readyPromise.set_value();

    MSG msg;
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        ::DispatchMessageW(&msg);
    }

    CHECK(Unregister());
    CHECK(::DestroyWindow(m_hWnd));
    m_hWnd = nullptr;

    ::UnregisterClassW(RAW_SINK_CLASS, hInstance);
}

bool RawInputDeviceManager::RawInputManagerImpl::SetDeviceEnabled(USHORT usUsage, bool enabled)
{
    RAWINPUTDEVICE rid =
    {
        HID_USAGE_PAGE_GENERIC,
        usUsage,
        enabled ? DWORD(RIDEV_DEVNOTIFY | RIDEV_INPUTSINK)
                : RIDEV_REMOVE,
        enabled ? m_hWnd : nullptr
    };

    return ::RegisterRawInputDevices(&rid, 1, sizeof(RAWINPUTDEVICE));
}

bool RawInputDeviceManager::RawInputManagerImpl::Register()
{
    CHECK(SetDeviceEnabled(HID_USAGE_GENERIC_MOUSE, true));
    CHECK(SetDeviceEnabled(HID_USAGE_GENERIC_KEYBOARD, true));
    CHECK(SetDeviceEnabled(HID_USAGE_GENERIC_GAMEPAD, true));
    CHECK(SetDeviceEnabled(HID_USAGE_GENERIC_JOYSTICK, true));

    return true;
}

bool RawInputDeviceManager::RawInputManagerImpl::Unregister()
{
    CHECK(SetDeviceEnabled(HID_USAGE_GENERIC_MOUSE, false));
    CHECK(SetDeviceEnabled(HID_USAGE_GENERIC_KEYBOARD, false));
    CHECK(SetDeviceEnabled(HID_USAGE_GENERIC_GAMEPAD, false));
    CHECK(SetDeviceEnabled(HID_USAGE_GENERIC_JOYSTICK, false));

    return true;
}

void RawInputDeviceManager::RawInputManagerImpl::OnDeviceConnected(HANDLE deviceHandle)
{
	std::string interfacePath = RawInputDevice::QueryRawDeviceInterfacePath(deviceHandle);
    if (IsVirtualRIDDevice(interfacePath))
    {
        DBGPRINT("Skipping virtual device. Handle=0x%08x, Path: %s", deviceHandle, interfacePath.c_str());
        return;
    }

    RID_DEVICE_INFO deviceInfo;
    CHECK(RawInputDevice::QueryRawDeviceInfo(deviceHandle, &deviceInfo));

    std::string deviceTypeStr;
    switch (deviceInfo.dwType)
    {
    case RIM_TYPEMOUSE:    deviceTypeStr = "Mouse";    break;
    case RIM_TYPEKEYBOARD: deviceTypeStr = "Keyboard"; break;
    case RIM_TYPEHID:      deviceTypeStr = "HID";      break;
    }

    if (m_Devices.find(deviceHandle) != m_Devices.end())
    {
        //DBGPRINT("Skipping already detected %s device. Handle=0x%08x, Path: %s", deviceTypeStr.c_str(), deviceHandle, interfacePath.c_str());
        return;
    }

    auto new_device = CreateRawInputDevice(deviceInfo.dwType, deviceHandle);

    auto emplace_result = m_Devices.emplace(deviceHandle, std::move(new_device));
    CHECK(emplace_result.second);

    DBGPRINT("Connected %s device. Handle=0x%08x, Path: %s", deviceTypeStr.c_str(), deviceHandle, interfacePath.c_str());

    //DumpInfo(emplace_result.first->second.get());
}

void RawInputDeviceManager::RawInputManagerImpl::OnDeviceDisconnected(HANDLE deviceHandle)
{
    auto it = m_Devices.find(deviceHandle);

    std::string deviceTypeStr;
    switch (it->second->GetType())
    {
    case RIM_TYPEMOUSE:    deviceTypeStr = "Mouse";    break;
    case RIM_TYPEKEYBOARD: deviceTypeStr = "Keyboard"; break;
    case RIM_TYPEHID:      deviceTypeStr = "HID";      break;
    }

    DBGPRINT("Disconnected %s device. Handle=0x%08x, Path: %s", deviceTypeStr.c_str(), deviceHandle, it->second->GetInterfacePath().c_str());
    CHECK(m_Devices.erase(deviceHandle));
}

void RawInputDeviceManager::RawInputManagerImpl::EnumerateDevices()
{
    std::vector<RAWINPUTDEVICELIST> deviceList(32);
    UINT count = static_cast<UINT>(deviceList.size());
    UINT result;

    while ((result = ::GetRawInputDeviceList(deviceList.data(), &count, sizeof(RAWINPUTDEVICELIST))) == UINT_MAX)
    {
        if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        {
            DBGPRINT("GetRawInputDeviceList() failed. GetLastError=%d", ::GetLastError());
            return;
        }

        // Buffer too small — count is updated by GetRawInputDeviceList to required count
        deviceList.resize(count);
    }

    deviceList.resize(result);

    std::unordered_set<HANDLE> current;
    current.reserve(deviceList.size());
    for (const auto& d : deviceList)
        current.insert(d.hDevice);

    // Remove devices no longer present.
    for (auto it = m_Devices.begin(); it != m_Devices.end(); )
    {
        if (!current.count(it->first))
        {
            OnDeviceDisconnected(it->first);
            it = m_Devices.erase(it);
        }
        else
            ++it;
    }

    // Add newly appeared devices.
    for (const auto& d : deviceList)
        if (!m_Devices.count(d.hDevice))
            OnDeviceConnected(d.hDevice);
}

void RawInputDeviceManager::RawInputManagerImpl::OnInput(const RAWINPUT* input)
{
    HANDLE hDevice = input->header.hDevice;

    // Route to default device first — it always receives all input of its type.
    switch (input->header.dwType)
    {
    case RIM_TYPEKEYBOARD: m_DefaultKeyboard->OnInput(input); break;
    case RIM_TYPEMOUSE:    m_DefaultMouse->OnInput(input);    break;
    }

    // Also route to the specific physical device if known.
    if (hDevice != NULL)
    {
        auto it = m_Devices.find(hDevice);
        if (it != m_Devices.end())
            it->second->OnInput(input);
    }
}

std::unique_ptr<RawInputDevice> RawInputDeviceManager::RawInputManagerImpl::CreateRawInputDevice(DWORD deviceType, HANDLE handle) const
{
    switch (deviceType)
    {
    case RIM_TYPEMOUSE:    return RawInputDeviceFactory<RawInputDeviceMouse>().Create(handle);
    case RIM_TYPEKEYBOARD: return RawInputDeviceFactory<RawInputDeviceKeyboard>().Create(handle);
    case RIM_TYPEHID:      return RawInputDeviceFactory<RawInputDeviceHid>().Create(handle);
    }

    DBGPRINT("Unknown device type %d.", deviceType);
    return nullptr;
}

RawInputDeviceManager::RawInputDeviceManager()
    : m_RawInputManagerImpl(std::make_unique<RawInputManagerImpl>())
{
}

RawInputDeviceManager::~RawInputDeviceManager() = default;

void RawInputDeviceManager::OnInputLanguageChanged(HKL hkl)
{
    if (m_RawInputManagerImpl->m_hWnd)
    {
        ::PostMessageW(m_RawInputManagerImpl->m_hWnd, WM_INPUTLANGCHANGE, 0, reinterpret_cast<LPARAM>(hkl));
    }
}

std::vector<std::shared_ptr<RawInputDevice>> RawInputDeviceManager::GetRawInputDevices() const
{
    std::vector<std::shared_ptr<RawInputDevice>> devices;
    for (auto& dev : m_RawInputManagerImpl->m_Devices)
        devices.emplace_back(dev.second.get());

    return devices;
}

RawInputDeviceKeyboard* RawInputDeviceManager::GetDefaultKeyboard() const
{
    return m_RawInputManagerImpl->m_DefaultKeyboard.get();
}

RawInputDeviceMouse* RawInputDeviceManager::GetDefaultMouse() const
{
    return m_RawInputManagerImpl->m_DefaultMouse.get();
}

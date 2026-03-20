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
    RawInputDevice* FindDevice(DWORD deviceType, HANDLE deviceHandle) const;

    std::thread       m_Thread;
    HWND              m_hWnd     = nullptr;

    std::vector<BYTE> m_InputBuffer;

    std::unordered_map<HANDLE, std::unique_ptr<RawInputDevice>> m_Devices;
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
                for (;;)
                {
                    UINT size = static_cast<UINT>(self->m_InputBuffer.size());
                    UINT result = ::GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam),
                        RID_INPUT, self->m_InputBuffer.data(), &size, sizeof(RAWINPUTHEADER));

                    if (result != (UINT)-1)
                    {
                        self->OnInput(reinterpret_cast<RAWINPUT*>(self->m_InputBuffer.data()));
                        break;
                    }

                    // Buffer too small — size is updated by GetRawInputData to required size
                    self->m_InputBuffer.resize(size);
                }
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
    RID_DEVICE_INFO deviceInfo;
    CHECK(RawInputDevice::QueryRawDeviceInfo(deviceHandle, &deviceInfo));

    if (FindDevice(deviceInfo.dwType, deviceHandle) != nullptr)
    {
        DBGPRINT("Skipping already detected device 0x%x of type %d", deviceHandle, deviceInfo.dwType);
        return;
    }

    auto new_device = CreateRawInputDevice(deviceInfo.dwType, deviceHandle);

    auto emplace_result = m_Devices.emplace(deviceHandle, std::move(new_device));
    CHECK(emplace_result.second);

    std::string deviceTypeStr;
    switch (deviceInfo.dwType)
    {
    case RIM_TYPEMOUSE:    deviceTypeStr = "mouse";    break;
    case RIM_TYPEKEYBOARD: deviceTypeStr = "keyboard"; break;
    case RIM_TYPEHID:      deviceTypeStr = "HID";      break;
    }
    DBGPRINT("Connected raw input %s. Handle=%x", deviceTypeStr.c_str(), deviceHandle);
    DumpInfo(emplace_result.first->second.get());
}

void RawInputDeviceManager::RawInputManagerImpl::OnDeviceDisconnected(HANDLE deviceHandle)
{
    DBGPRINT("Disconnected raw input device. Handle=%x", deviceHandle);
    CHECK(m_Devices.erase(deviceHandle));
}

void RawInputDeviceManager::RawInputManagerImpl::EnumerateDevices()
{
    UINT count = 0;
    UINT result = ::GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST));
    if (result == UINT_MAX)
    {
        DBGPRINT("GetRawInputDeviceList() failed. GetLastError=%d", ::GetLastError());
        return;
    }
    DCHECK_EQ(0u, result);

    std::vector<RAWINPUTDEVICELIST> device_list;
    do
    {
        device_list.resize(count);
        result = ::GetRawInputDeviceList(device_list.data(), &count, sizeof(RAWINPUTDEVICELIST));
    } while (result == UINT_MAX && GetLastError() == ERROR_INSUFFICIENT_BUFFER);

    device_list.resize(result);

    // Build a set of currently present handles for O(1) lookup.
    std::unordered_set<HANDLE> current;
    current.reserve(device_list.size());
    for (const auto& d : device_list)
        current.insert(d.hDevice);

    // Remove devices that are no longer present.
    std::vector<HANDLE> toRemove;
    for (auto it = m_Devices.begin(); it != m_Devices.end(); ++it)
        if (!current.count(it->first))
            toRemove.push_back(it->first);

    for (HANDLE h : toRemove)
    {
        auto it = m_Devices.find(h);
        auto handle = std::move(it->first);
        m_Devices.erase(it);
        OnDeviceDisconnected(handle);
    }

    // Add devices that are not yet tracked.
    for (const auto& d : device_list)
    {
        if (m_Devices.find(d.hDevice) == m_Devices.end())
            OnDeviceConnected(d.hDevice);
    }
}

void RawInputDeviceManager::RawInputManagerImpl::OnInput(const RAWINPUT* input)
{
    CHECK(input);

    if (!input)
        return;

    UINT rimCode = GET_RAWINPUT_CODE_WPARAM(input->header.wParam);
    DCHECK(rimCode == RIM_INPUT || rimCode == RIM_INPUTSINK);

    if (RawInputDevice* device = FindDevice(input->header.dwType, input->header.hDevice))
    {
        device->OnInput(input);
    }
    else
    {
        DBGPRINT("Cannot process input. Device 0x%x of type %d is not found", input->header.hDevice, input->header.dwType);
    }

    //Sleep(10);
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

RawInputDevice* RawInputDeviceManager::RawInputManagerImpl::FindDevice(DWORD deviceType, HANDLE deviceHandle) const
{
    if (deviceHandle)
    {
        auto it = m_Devices.find(deviceHandle);
        if (it == m_Devices.end())
            return nullptr;
        return it->second.get();
    }

    // In some cases the handle is not provided.
    // Fall back to the first device of the matching type.
    // See https://stackoverflow.com/q/57552844
    auto it = std::find_if(m_Devices.begin(), m_Devices.end(),
        [deviceType](const decltype(m_Devices)::const_reference& device)
        {
            return device.second->GetType() == deviceType;
        });

    if (it == m_Devices.end())
        return nullptr;

    return it->second.get();
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

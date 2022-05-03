#include "pch.h"
#include "framework.h"

#include "RawInputDeviceManager.h"

#include "RawInputDevice.h"
#include "RawInputDeviceMouse.h"
#include "RawInputDeviceKeyboard.h"
#include "RawInputDeviceHid.h"

#include <array>
#include <unordered_map>
#include <thread>

namespace
{
    void DumpInfo(const RawInputDevice* /*device*/)
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

struct RawInputDeviceManager::RawInputManagerImpl
{
    RawInputManagerImpl();
    ~RawInputManagerImpl();

    void ThreadRun();

    bool Register(HWND hWnd);
    bool Unregister();

    void OnInputMessage(HRAWINPUT dataHandle);

    void OnInputDeviceConnected(HANDLE deviceHandle, bool isConnected);

    void EnumerateDevices();

    void OnInput(const RAWINPUT* input);

    std::unique_ptr<RawInputDevice> CreateRawInputDevice(DWORD deviceType, HANDLE handle) const;

    std::thread m_Thread;
    std::atomic<bool> m_Running = true;
    HANDLE m_WakeUpEvent = INVALID_HANDLE_VALUE;
    DWORD m_ParentThreadId = 0;
    std::vector<uint8_t> m_InputBuffer;

    std::unordered_map<HANDLE, std::unique_ptr<RawInputDevice>> m_Devices;
};

RawInputDeviceManager::RawInputManagerImpl::RawInputManagerImpl()
    : m_Thread(&RawInputManagerImpl::ThreadRun, this)
    , m_ParentThreadId(::GetCurrentThreadId())
{
}

RawInputDeviceManager::RawInputManagerImpl::~RawInputManagerImpl()
{
    m_Running = false;
    SetEvent(m_WakeUpEvent);
    m_Thread.join();
}

void RawInputDeviceManager::RawInputManagerImpl::ThreadRun()
{
    m_WakeUpEvent = ::CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    CHECK(IsValidHandle(m_WakeUpEvent));

    HINSTANCE hInstance = ::GetModuleHandleW(nullptr);
    HWND hWnd = ::CreateWindowExW(0, L"Static", nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, 0);
    CHECK(IsValidHandle(hWnd));

    SUBCLASSPROC subClassProc = [](HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData) -> LRESULT
    {
        auto manager = reinterpret_cast<RawInputManagerImpl*>(dwRefData);
        CHECK(manager);

        switch (uMsg)
        {
        case WM_INPUT_DEVICE_CHANGE:
        {
            CHECK(wParam == GIDC_ARRIVAL || wParam == GIDC_REMOVAL);
            HANDLE deviceHandle = reinterpret_cast<HANDLE>(lParam);
            bool isConnected = (wParam == GIDC_ARRIVAL);
            manager->OnInputDeviceConnected(deviceHandle, isConnected);

            return 0;
        }
        case WM_INPUT:
        {
            HRAWINPUT dataHandle = reinterpret_cast<HRAWINPUT>(lParam);
            manager->OnInputMessage(dataHandle);

            return 0;
        }
        }

        return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
    };

    CHECK(::SetWindowSubclass(hWnd, subClassProc, 0, reinterpret_cast<DWORD_PTR>(this)));

    //CHECK(::AttachThreadInput(::GetCurrentThreadId(), m_ParentThreadId, true));

    CHECK(Register(hWnd));

    // enumerate devices before start
    EnumerateDevices();

    // main message loop
    while (m_Running)
    {
        // wait for new messages
        ::MsgWaitForMultipleObjectsEx(1, &m_WakeUpEvent, INFINITE, QS_ALLEVENTS, MWMO_INPUTAVAILABLE);

        MSG msg;
        while (::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                m_Running = false;
                break;
            }

            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }

    CHECK(Unregister());

    //CHECK(::AttachThreadInput(::GetCurrentThreadId(), m_ParentThreadId, false));

    CHECK(::RemoveWindowSubclass(hWnd, subClassProc, 0));
    CHECK(::DestroyWindow(hWnd));
}

bool RawInputDeviceManager::RawInputManagerImpl::Register(HWND hWnd)
{
    RAWINPUTDEVICE rid[] =
    {
        // TODO see https://github.com/niello/misc/blob/master/RawInputLocale/main.cpp
        // for RIDEV_NOLEGACY support
        /*{
            HID_USAGE_PAGE_GENERIC,
            HID_USAGE_GENERIC_MOUSE,
            RIDEV_DEVNOTIFY | RIDEV_INPUTSINK | RIDEV_NOLEGACY,
            hWnd
        },
        {
            HID_USAGE_PAGE_GENERIC,
            HID_USAGE_GENERIC_KEYBOARD,
            RIDEV_DEVNOTIFY | RIDEV_INPUTSINK | RIDEV_NOLEGACY,
            hWnd
        },*/
        {
            HID_USAGE_PAGE_GENERIC,
            0,
            RIDEV_DEVNOTIFY | RIDEV_INPUTSINK | RIDEV_PAGEONLY,
            hWnd
        }
    };

    return ::RegisterRawInputDevices(rid, static_cast<UINT>(std::size(rid)), sizeof(RAWINPUTDEVICE));
}

bool RawInputDeviceManager::RawInputManagerImpl::Unregister()
{
    RAWINPUTDEVICE rid[] =
    {
        {
            HID_USAGE_PAGE_GENERIC,
            0,
            RIDEV_REMOVE | RIDEV_PAGEONLY,
            0
        }
    };

    return ::RegisterRawInputDevices(rid, static_cast<UINT>(std::size(rid)), sizeof(RAWINPUTDEVICE));
}

void RawInputDeviceManager::RawInputManagerImpl::OnInputMessage(HRAWINPUT dataHandle)
{
    CHECK(IsValidHandle(dataHandle));

    UINT size = 0;
    UINT result = ::GetRawInputData(dataHandle, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));

    if (result == UINT_MAX)
    {
        DBGPRINT("GetRawInputData() failed. GetLastError=%d", ::GetLastError());
        return;
    }
    DCHECK_EQ(0u, result);

    if (m_InputBuffer.size() < size)
        m_InputBuffer.resize(size);

    RAWINPUT* input = reinterpret_cast<RAWINPUT*>(m_InputBuffer.data());

    result = ::GetRawInputData(dataHandle, RID_INPUT, input, &size, sizeof(RAWINPUTHEADER));

    if (result == UINT_MAX)
    {
        DBGPRINT("GetRawInputData() failed. GetLastError=%d", ::GetLastError());
        return;
    }
    DCHECK_EQ(size, result);

    OnInput(input);
}

void RawInputDeviceManager::RawInputManagerImpl::OnInputDeviceConnected(HANDLE deviceHandle, bool isConnected)
{
    if (isConnected)
    {
        RID_DEVICE_INFO deviceInfo;
        CHECK(RawInputDevice::QueryRawDeviceInfo(deviceHandle, &deviceInfo));

        auto new_device = CreateRawInputDevice(deviceInfo.dwType, deviceHandle);
        //CHECK(new_device && new_device->IsValid());

        auto emplace_result = m_Devices.emplace(deviceHandle, std::move(new_device));
        CHECK(emplace_result.second);

        // TODO LOG
        DBGPRINT("Connected raw input device. Handle=%x", deviceHandle);
        DumpInfo(emplace_result.first->second.get());
    }
    else
    {
        DBGPRINT("Disconnected raw input device. Handle=%x", deviceHandle);
        m_Devices.erase(deviceHandle);
    }
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
    // The list of devices can change between calls to GetRawInputDeviceList,
    // so call it in a loop if the function returns ERROR_INSUFFICIENT_BUFFER
    do
    {
        device_list.resize(count);
        result = ::GetRawInputDeviceList(device_list.data(), &count, sizeof(RAWINPUTDEVICELIST));
    } while (result == UINT_MAX && GetLastError() == ERROR_INSUFFICIENT_BUFFER);

    device_list.resize(result);

    for (auto& device : device_list)
    {
        if (m_Devices.find(device.hDevice) == m_Devices.end())
            OnInputDeviceConnected(device.hDevice, true);
    }

    std::list<decltype(m_Devices)::iterator> removed_devices;
    for (auto it = m_Devices.begin(); it != m_Devices.end(); ++it)
    {
        auto deviceListIt = std::find_if(device_list.begin(), device_list.end(),
            [&it](const RAWINPUTDEVICELIST& device)
            {
                return it->first == device.hDevice;
            });

        if (deviceListIt == device_list.end())
            removed_devices.push_back(it);
    }

    // Clear out old devices that weren't part of this enumeration pass.
    for(auto& device : removed_devices)
        OnInputDeviceConnected(device->first, false);
}

void RawInputDeviceManager::RawInputManagerImpl::OnInput(const RAWINPUT* input)
{
    CHECK(input);

    if (!input)
        return;

    HANDLE deviceHandle = input->header.hDevice;
    UINT rimCode = GET_RAWINPUT_CODE_WPARAM(input->header.wParam);
    DCHECK(rimCode == RIM_INPUT || rimCode == RIM_INPUTSINK);

    auto it = m_Devices.find(deviceHandle);
    if (it == m_Devices.end())
    {
        // In some cases handle is not provided.
        // Try to interpret input by its type.
        // See https://stackoverflow.com/q/57552844
        it = std::find_if(m_Devices.begin(), m_Devices.end(),
            [&input](const decltype(m_Devices)::const_reference& device)
            {
                switch (input->header.dwType)
                {
                case RIM_TYPEKEYBOARD:
                    return dynamic_cast<RawInputDeviceKeyboard*>(device.second.get()) != nullptr;
                case RIM_TYPEMOUSE:
                    return dynamic_cast<RawInputDeviceMouse*>(device.second.get()) != nullptr;
                default:
                    return false;
                }
            });
    }

    if (it == m_Devices.end())
    {
        DBGPRINT("Cannot process input. Device 0x%x of type %d is not found", deviceHandle, input->header.dwType);
        return;
    }

    it->second->OnInput(input);
}

std::unique_ptr<RawInputDevice> RawInputDeviceManager::RawInputManagerImpl::CreateRawInputDevice(DWORD deviceType, HANDLE handle) const
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

RawInputDeviceManager::RawInputDeviceManager()
    : m_RawInputManagerImpl(std::make_unique<RawInputManagerImpl>())
{
}

RawInputDeviceManager::~RawInputDeviceManager() = default;

std::vector<RawInputDevice*> RawInputDeviceManager::GetRawInputDevices() const
{
    std::vector<RawInputDevice*> devices;
    for (auto& dev : m_RawInputManagerImpl->m_Devices)
    {
        devices.emplace_back(dev.second.get());
    }

    return devices;
}

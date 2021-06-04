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
    void OnInputBuffered();

    void OnInputDeviceChange();

    void OnInput(const RAWINPUT* input);

    std::unique_ptr<RawInputDevice> CreateRawInputDevice(DWORD deviceType, HANDLE handle) const;

    std::thread m_Thread;
    std::atomic<bool> m_Running = true;
    HANDLE m_WakeUpEvent = INVALID_HANDLE_VALUE;
    DWORD m_ParentThreadId = 0;

    // up to 32 raw input messages (~1.5 kilobyte)
    static constexpr size_t c_InputBufferSize = (sizeof(RAWINPUT) * 32);

    std::array<uint8_t, c_InputBufferSize> m_InputDataBuffer;
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
    // TODO do we need buffered raw input processing?
    constexpr bool buffered = false;

    m_WakeUpEvent = ::CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    CHECK(IsValidHandle(m_WakeUpEvent));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpszClassName = L"Message";
    wc.lpfnWndProc = [](HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT
    {
        RawInputManagerImpl* manager = reinterpret_cast<RawInputManagerImpl*>(GetWindowLongPtrW(hWnd, 0));

        if (manager)
        {
            switch (message)
            {
            case WM_INPUT_DEVICE_CHANGE:
            {
                manager->OnInputDeviceChange();
                return 0;
            }
            case WM_INPUT:
            {
                HRAWINPUT dataHandle = reinterpret_cast<HRAWINPUT>(lParam);
                manager->OnInputMessage(dataHandle);
                return 0;
            }
            }
        }

        return ::DefWindowProcW(hWnd, message, wParam, lParam);
    };
    wc.cbWndExtra = sizeof(RawInputManagerImpl*); // add some space for this pointer
    wc.hInstance = ::GetModuleHandleW(nullptr);

    ATOM classAtom = ::RegisterClassExW(&wc);
    if (classAtom == 0)
    {
        DBGPRINT("Cannot register window class. GetLastError=%d", ::GetLastError());
        return;
    }

    HWND hWnd = ::CreateWindowExW(0, reinterpret_cast<LPCWSTR>(classAtom), nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, 0);
    if (!IsValidHandle(hWnd))
    {
        DBGPRINT("Cannot create hidden raw input window. GetLastError=%d", ::GetLastError());
        return;
    }

    ::SetWindowLongPtrW(hWnd, 0, reinterpret_cast<LONG_PTR>(this));

    //if (!::AttachThreadInput(::GetCurrentThreadId(), m_ParentThreadId, true))
    //    DBGPRINT("Cannot attach raw input thread input to parent thread's input. GetLastError=%d", ::GetLastError());

    if (!Register(hWnd))
    {
        DBGPRINT("Cannot register raw input devices.");
        return;
    }

    // enumerate devices before start
    OnInputDeviceChange();

    // main message loop
    while (m_Running)
    {
        MSG msg;

        if (buffered)
            OnInputBuffered();

        while (true)
        {
            bool haveMessage = false;

            if (buffered)
            {
                haveMessage = ::PeekMessageW(&msg, 0, 0, WM_INPUT - 1, PM_REMOVE) ||
                    ::PeekMessageW(&msg, 0, WM_INPUT + 1, 0, PM_REMOVE);
            }
            else
            {
                haveMessage = ::PeekMessageW(&msg, 0, 0, 0, PM_REMOVE);
            }

            if (!haveMessage)
                break;

            ::DispatchMessageW(&msg);
        }

        // wait for new messages
        ::MsgWaitForMultipleObjectsEx(1, &m_WakeUpEvent, INFINITE, QS_ALLEVENTS, MWMO_INPUTAVAILABLE);
    }

    if (!Unregister())
        DBGPRINT("Cannot unregister raw input devices.");

    //if (!::AttachThreadInput(::GetCurrentThreadId(), m_ParentThreadId, false))
    //    DBGPRINT("Cannot deattach raw input thread input to parent thread's input. GetLastError=%d", ::GetLastError());

    if (hWnd)
        ::DestroyWindow(hWnd);

    if (classAtom)
        ::UnregisterClassW(reinterpret_cast<LPCWSTR>(classAtom), wc.hInstance);
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

    if (result == static_cast<UINT>(-1))
    {
        DBGPRINT("GetRawInputData() failed. GetLastError=%d", ::GetLastError());
        return;
    }
    DCHECK_EQ(0u, result);

    RAWINPUT* input = reinterpret_cast<RAWINPUT*>(m_InputDataBuffer.data());

    result = ::GetRawInputData(dataHandle, RID_INPUT, input, &size, sizeof(RAWINPUTHEADER));

    if (result == static_cast<UINT>(-1))
    {
        DBGPRINT("GetRawInputData() failed. GetLastError=%d", ::GetLastError());
        return;
    }
    DCHECK_EQ(size, result);

    OnInput(input);
}

void RawInputDeviceManager::RawInputManagerImpl::OnInputBuffered()
{
    while (true)
    {
        UINT size = static_cast<UINT>(m_InputDataBuffer.size());
        RAWINPUT* input = reinterpret_cast<RAWINPUT*>(m_InputDataBuffer.data());

        UINT result = ::GetRawInputBuffer(input, &size, sizeof(RAWINPUTHEADER));

        if (result == 0 || result == static_cast<UINT>(-1))
            break;

        // hack for a undefined QWORD used in NEXTRAWINPUTBLOCK macro
        using QWORD = __int64;

        for (; result; result--, input = NEXTRAWINPUTBLOCK(input))
        {
            OnInput(input);
        }
    }
}

void RawInputDeviceManager::RawInputManagerImpl::OnInputDeviceChange()
{
    UINT count = 0;
    UINT result = ::GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST));
    if (result == static_cast<UINT>(-1))
    {
        DBGPRINT("GetRawInputDeviceList() failed. GetLastError=%d", ::GetLastError());
        return;
    }
    DCHECK_EQ(0u, result);

    auto device_list = std::make_unique<RAWINPUTDEVICELIST[]>(count);
    result = ::GetRawInputDeviceList(device_list.get(), &count, sizeof(RAWINPUTDEVICELIST));
    if (result == static_cast<UINT>(-1))
    {
        DBGPRINT("GetRawInputDeviceList() failed. GetLastError=%d", ::GetLastError());
        return;
    }

    std::unordered_set<HANDLE> enumerated_device_handles;
    for (UINT i = 0; i < result; ++i)
    {
        const HANDLE device_handle = device_list[i].hDevice;

        auto deviceIt = m_Devices.find(device_handle);
        if (deviceIt == m_Devices.end())
        {
            const DWORD device_type = device_list[i].dwType;
            auto new_device = CreateRawInputDevice(device_type, device_handle);
            if (!new_device || !new_device->IsValid())
            {
                DBGPRINT("Invalid device: '%d'", device_handle);
                continue;
            }

            auto emplace_result = m_Devices.emplace(device_handle, std::move(new_device));
            CHECK(emplace_result.second);

            // TODO LOG
            DumpInfo(emplace_result.first->second.get());
        }

        enumerated_device_handles.insert(device_handle);
    }

    // Clear out old devices that weren't part of this enumeration pass.
    auto deviceIt = m_Devices.begin();
    while (deviceIt != m_Devices.end())
    {
        if (enumerated_device_handles.find(deviceIt->first) == enumerated_device_handles.end())
        {
            deviceIt = m_Devices.erase(deviceIt);
        }
        else
        {
            ++deviceIt;
        }
    }
}

void RawInputDeviceManager::RawInputManagerImpl::OnInput(const RAWINPUT* input)
{
    CHECK(input);

    if (!input)
        return;

    HANDLE handle = input->header.hDevice;
    if (!IsValidHandle(handle))
        return;

    UINT rimCode = GET_RAWINPUT_CODE_WPARAM(input->header.wParam);
    DCHECK(rimCode == RIM_INPUT || rimCode == RIM_INPUTSINK);

    auto it = m_Devices.find(handle);
    if (it == m_Devices.end())
    {
        DBGPRINT("Device 0x%x not found", handle);
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

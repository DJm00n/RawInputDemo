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

    constexpr UINT RAW_BATCH_SIZE = 128;

    // GetRawInputBuffer requires the buffer to be aligned to pointer size.
    // On WOW64 (32-bit process on 64-bit kernel) the kernel lays out RAWINPUT
    // with 64-bit padding, so the kernel-side struct is larger than
    // sizeof(RAWINPUT) in the process. Multiply the initial size by 2 to
    // avoid an immediate reallocation on the very first call.
    constexpr std::align_val_t RAW_BUF_ALIGN{ sizeof(void*) };

#ifdef _WIN64
    constexpr UINT RAW_WOW64_FACTOR = 1;
#else
    constexpr UINT RAW_WOW64_FACTOR = 2;
#endif

    constexpr UINT RAW_BUF_INITIAL = RAW_BATCH_SIZE * sizeof(RAWINPUT) * RAW_WOW64_FACTOR;

    // Window class name for the message-only sink window.
    constexpr LPCWSTR RAW_SINK_CLASS = L"RawInputSink";

    // ---------------------------------------------------------------------------
    // Aligned buffer — wraps operator new[] / operator delete[] with explicit
    // alignment. Replaces _aligned_malloc / _aligned_free.
    // std::vector does not guarantee alignment beyond alignof(max_align_t),
    // which happens to be sufficient on MSVC (16 bytes) but is an
    // implementation detail. This makes the requirement explicit and portable.
    // ---------------------------------------------------------------------------
    struct AlignedDeleter
    {
        std::align_val_t align;
        void operator()(std::byte* p) const
        {
            ::operator delete[](p, align);
        }
    };

    using AlignedBuffer = std::unique_ptr<std::byte[], AlignedDeleter>;

    AlignedBuffer AllocAligned(UINT size, std::align_val_t align)
    {
        return AlignedBuffer(
            static_cast<std::byte*>(::operator new[](size, align)),
            AlignedDeleter{ align });
    }
}

struct RawInputDeviceManager::RawInputManagerImpl
{
    RawInputManagerImpl();
    ~RawInputManagerImpl();

    void ThreadRun(std::promise<void> readyPromise);

    // ---------------------------------------------------------------------------
    // Window procedure
    //
    // StaticWndProc is registered as lpfnWndProc for RAW_SINK_CLASS.
    // On WM_NCCREATE it retrieves `this` from CREATESTRUCT::lpCreateParams,
    // stores it in GWLP_USERDATA, and delegates all subsequent messages to
    // the instance method WndProc.
    //
    // This replaces SetWindowSubclass: subclassing is appropriate when
    // inserting into a foreign window class (e.g. Static, Button). For an
    // owned class, registering lpfnWndProc directly is the standard pattern
    // (ATL CWindowImpl, WTL, etc.) and avoids the extra indirection layer.
    // ---------------------------------------------------------------------------
    static LRESULT CALLBACK StaticWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    bool Register();
    bool Unregister();

    // Drains the entire raw input queue in batches via GetRawInputBuffer.
    // Called from the message loop on every WM_INPUT. The message itself is
    // used only as a wake-up signal; its lParam (HRAWINPUT) is released via
    // DefWindowProc without reading data from it — data comes exclusively
    // from the buffer. This avoids mixing GetRawInputData and
    // GetRawInputBuffer, which compete for the same kernel queue.
    void DrainRawInputQueue();

    void OnDeviceConnected(HANDLE deviceHandle);
    void OnDeviceDisonnected(HANDLE deviceHandle);

    void EnumerateDevices();

    void OnInput(const RAWINPUT* input);
    void OnKeyboardEvent(const RAWKEYBOARD& keyboard);

    std::unique_ptr<RawInputDevice> CreateRawInputDevice(DWORD deviceType, HANDLE deviceHandle) const;
    RawInputDevice* FindDevice(DWORD deviceType, HANDLE deviceHandle) const;

    std::thread       m_Thread;
    HWND              m_hWnd     = nullptr;
    DWORD             m_ThreadId = 0;       // set by ThreadRun, used for PostThreadMessage
    DWORD             m_ParentThreadId = 0;

    // Raw input buffer — aligned, grows on demand, never shrinks.
    AlignedBuffer m_InputBuffer;
    UINT          m_InputBufferSize = 0;

    uint8_t m_KeyState[256] = {};   // our thread's keyboard state
    wchar_t m_PendingSurrogate = 0;

    std::unordered_map<HANDLE, std::unique_ptr<RawInputDevice>> m_Devices;
};

// ---------------------------------------------------------------------------
// StaticWndProc / WndProc
// ---------------------------------------------------------------------------

LRESULT CALLBACK RawInputDeviceManager::RawInputManagerImpl::StaticWndProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    RawInputManagerImpl* self = nullptr;

    if (uMsg == WM_NCCREATE)
    {
        // CREATESTRUCT::lpCreateParams carries `this` from CreateWindowEx.
        // Store it in GWLP_USERDATA so every subsequent message can retrieve it.
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<RawInputManagerImpl*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<RawInputManagerImpl*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    if (self)
        return self->WndProc(hWnd, uMsg, wParam, lParam);

    return ::DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

LRESULT RawInputDeviceManager::RawInputManagerImpl::WndProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INPUT_DEVICE_CHANGE:
    {
        CHECK(wParam == GIDC_ARRIVAL || wParam == GIDC_REMOVAL);
        HANDLE deviceHandle = reinterpret_cast<HANDLE>(lParam);

        switch (wParam)
        {
        case GIDC_ARRIVAL:
            OnDeviceConnected(deviceHandle);
            break;
        case GIDC_REMOVAL:
            OnDeviceDisonnected(deviceHandle);
            break;
        }
        return 0;
    }

    default:
        return ::DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }
}

// ---------------------------------------------------------------------------
// RawInputManagerImpl
// ---------------------------------------------------------------------------

RawInputDeviceManager::RawInputManagerImpl::RawInputManagerImpl()
    : m_ParentThreadId(::GetCurrentThreadId())
    , m_InputBuffer(AllocAligned(RAW_BUF_INITIAL, RAW_BUF_ALIGN))
    , m_InputBufferSize(RAW_BUF_INITIAL)
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
    ::PostThreadMessageW(m_ThreadId, WM_QUIT, 0, 0);
    m_Thread.join();
}

void RawInputDeviceManager::RawInputManagerImpl::ThreadRun(std::promise<void> readyPromise)
{
    m_ThreadId = ::GetCurrentThreadId();

    HINSTANCE hInstance = ::GetModuleHandleW(nullptr);

    // Register our own window class with StaticWndProc as lpfnWndProc.
    // This means GWLP_USERDATA is entirely ours — no risk of collision with
    // a system class (e.g. Static) that might use it internally.
    // RegisterClassEx returns 0 if the class is already registered
    // (ERROR_CLASS_ALREADY_EXISTS), which is harmless in this context.
    WNDCLASSEXW wc  = { sizeof(wc) };
    wc.lpfnWndProc  = StaticWndProc;
    wc.hInstance    = hInstance;
    wc.lpszClassName = RAW_SINK_CLASS;
    ::RegisterClassExW(&wc);

    // Pass `this` as lpParam — retrieved in StaticWndProc on WM_NCCREATE.
    m_hWnd = ::CreateWindowExW(0, RAW_SINK_CLASS, nullptr, 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance,
        this);
    CHECK(IsValidHandle(m_hWnd));

    CHECK(Register());

    EnumerateDevices();

    // Unblock the constructor — window is ready, devices are registered.
    readyPromise.set_value();

    // Main message loop.
    // WM_INPUT is intercepted before DispatchMessage so the full queue can be
    // drained in one pass. The message is then dispatched so that WndProc
    // calls DefWindowProc / CleanupRawInput to release the HRAWINPUT handle.
    //
    // No HANDLE-based wake-up event needed: the destructor posts WM_QUIT
    // directly to this thread's queue via PostThreadMessage, which is
    // sufficient to unblock GetMessageW.
    MSG msg;
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if (msg.message == WM_INPUT)
            DrainRawInputQueue();
    
        ::DispatchMessageW(&msg);
    }

    CHECK(Unregister());

    CHECK(::DestroyWindow(m_hWnd));
    m_hWnd = nullptr;

    ::UnregisterClassW(RAW_SINK_CLASS, hInstance);
}

bool RawInputDeviceManager::RawInputManagerImpl::Register()
{
    RAWINPUTDEVICE rid[] =
    {
        {
            HID_USAGE_PAGE_GENERIC,
            0,
            RIDEV_DEVNOTIFY | RIDEV_INPUTSINK | RIDEV_PAGEONLY,
            m_hWnd
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

void RawInputDeviceManager::RawInputManagerImpl::DrainRawInputQueue()
{
    for (;;)
    {
        UINT cbBuffer = m_InputBufferSize;

        UINT count = ::GetRawInputBuffer(
            reinterpret_cast<RAWINPUT*>(m_InputBuffer.get()),
            &cbBuffer,
            sizeof(RAWINPUTHEADER));

        if (count == 0)
            break; // queue empty

        if (count == static_cast<UINT>(-1))
        {
            // Buffer too small for the first element. cbBuffer now holds the
            // required size as seen by the kernel. On WOW64 this is already
            // the 64-bit-padded size; apply the factor again to amortise
            // future growth.
            UINT needed = cbBuffer * RAW_WOW64_FACTOR;
            if (needed < m_InputBufferSize * 2)
                needed = m_InputBufferSize * 2;

            m_InputBuffer     = AllocAligned(needed, RAW_BUF_ALIGN);
            m_InputBufferSize = needed;
            continue;
        }

        // NEXTRAWINPUTBLOCK is the only correct way to advance the pointer.
        // The macro reads dwSize from the current header (already set to the
        // kernel-side padded size) and applies RAWINPUT_ALIGN. Manual pointer
        // arithmetic breaks on WOW64.
        RAWINPUT* input = reinterpret_cast<RAWINPUT*>(m_InputBuffer.get());
        for (UINT i = 0; i < count; ++i)
        {
            OnInput(input);
            input = NEXTRAWINPUTBLOCK(input);
        }
        // Do not break — there may be more events in the queue.
    }
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

void RawInputDeviceManager::RawInputManagerImpl::OnDeviceDisonnected(HANDLE deviceHandle)
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
    for (auto it = m_Devices.begin(); it != m_Devices.end(); )
    {
        if (!current.count(it->first))
        {
            HANDLE handle = it->first;
            it = m_Devices.erase(it);
            OnDeviceDisonnected(handle);
        }
        else
        {
            ++it;
        }
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

    if (input->header.dwType == RIM_TYPEKEYBOARD)
    {
        OnKeyboardEvent(input->data.keyboard);
    }
}

void RawInputDeviceManager::RawInputManagerImpl::OnKeyboardEvent(const RAWKEYBOARD& keyboard)
{
    if (keyboard.MakeCode == KEYBOARD_OVERRUN_MAKE_CODE || keyboard.VKey >= 0xFF)
        return;

    const bool isKeyDown = !(keyboard.Flags & RI_KEY_BREAK);
    const bool isE0      =  (keyboard.Flags & RI_KEY_E0) != 0;
    const UINT scanCode  = keyboard.MakeCode | (isE0 ? 0x100 : 0);

    const auto updateKeyState = [&](UINT vk)
    {
        if (isKeyDown) m_KeyState[vk] |= 0x80;
        else           m_KeyState[vk] &= ~0x80;

        if (UINT vkEx = ::MapVirtualKeyW(MAKEWORD(keyboard.MakeCode, isE0 ? 0xe0 : 0x00), MAPVK_VSC_TO_VK_EX))
        {
            if (isKeyDown) m_KeyState[vkEx] |= 0x80;
            else           m_KeyState[vkEx] &= ~0x80;
        }
    };

    const auto emitUtf16Sequence = [&](const wchar_t* seq, int len,
                                       const std::function<void(char32_t)>& callback)
    {
        if (!callback)
            return;

        for (int i = 0; i < len; )
        {
            wchar_t wc = seq[i++];
            if (IS_HIGH_SURROGATE(wc)) { m_PendingSurrogate = wc; continue; }

            char32_t cp;
            if (IS_LOW_SURROGATE(wc) && m_PendingSurrogate)
            {
                cp = 0x10000u
                    + ((static_cast<char32_t>(m_PendingSurrogate) - 0xD800u) << 10)
                    +  (static_cast<char32_t>(wc)                 - 0xDC00u);
            }
            else
            {
                cp = static_cast<char32_t>(wc);
            }
            m_PendingSurrogate = 0;
            callback(cp);
        }
    };

    const auto callToUnicodeEx = [&]
    {
        wchar_t buf[16] = {};
        const int result = ::ToUnicodeEx(
            keyboard.VKey, scanCode, m_KeyState,
            buf, static_cast<int>(std::size(buf)), 0,
            ::GetKeyboardLayout(m_ParentThreadId));

        if (result > 0) emitUtf16Sequence(buf,  result, OnCharacter);
        if (result < 0) emitUtf16Sequence(buf, -result, OnDeadKey);
    };

    if (!isKeyDown) callToUnicodeEx();  // до updateKeyState: Alt+Numpad

    updateKeyState(keyboard.VKey);

    switch (keyboard.VKey)
    {
    case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL:
        if (isKeyDown) m_KeyState[keyboard.VKey] ^= 0x01;
        break;

    case VK_SHIFT:   case VK_LSHIFT:   case VK_RSHIFT:
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_MENU:    case VK_LMENU:    case VK_RMENU:
        break;

    default:
        if (isKeyDown) callToUnicodeEx();  // после updateKeyState: обычные клавиши
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
            switch (deviceType)
            {
            case RIM_TYPEKEYBOARD: return dynamic_cast<RawInputDeviceKeyboard*>(device.second.get()) != nullptr;
            case RIM_TYPEMOUSE:    return dynamic_cast<RawInputDeviceMouse*>   (device.second.get()) != nullptr;
            case RIM_TYPEHID:      return dynamic_cast<RawInputDeviceHid*>     (device.second.get()) != nullptr;
            default:               return false;
            }
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

std::vector<RawInputDevice*> RawInputDeviceManager::GetRawInputDevices() const
{
    std::vector<RawInputDevice*> devices;
    for (auto& dev : m_RawInputManagerImpl->m_Devices)
        devices.emplace_back(dev.second.get());
    return devices;
}

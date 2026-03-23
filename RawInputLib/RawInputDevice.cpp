#include "pch.h"
#include "framework.h"

#include "RawInputDevice.h"

#include "CfgMgr32Wrapper.h"

#pragma warning(push, 0)
#include <hidsdi.h> // HidD_* API

#include <initguid.h>
#pragma warning(pop)

#include <winioctl.h>
#include <usbioctl.h>

RawInputDevice::RawInputDevice(HANDLE handle)
    : m_Handle(handle)
{}

RawInputDevice::~RawInputDevice() = default;

bool RawInputDevice::Initialize()
{
    if (!QueryRawInputDeviceInfo())  return false;

	TryQueryDeviceNodeInfo();
    TryQueryUsbInfo();
    TryQueryXboxInfo();
    TryQueryBluetoothLEInfo();

    ResolveIdentity();

    return true;
}

bool RawInputDevice::QueryRawInputDeviceInfo()
{
    DCHECK(IsValidHandle(m_Handle));

    m_InterfacePath = QueryRawDeviceInterfacePath(m_Handle);
    if (m_InterfacePath.empty())
        return false;

    ScopedHandle interfaceHandle = OpenDeviceInterface(m_InterfacePath);
    if (!IsValidHandle(interfaceHandle.get()))
    {
        /* System devices, such as keyboards and mice, cannot be opened in
        read-write mode, because the system takes exclusive control over
        them.  This is to prevent keyloggers.  However, feature reports
        can still be sent and received.  Retry opening the device, but
        without read/write access. */
        interfaceHandle = OpenDeviceInterface(m_InterfacePath, true);
        if (IsValidHandle(interfaceHandle.get()))
            m_IsInterfaceReadOnly = true;
    }

    return true;
}

void RawInputDevice::TryQueryDeviceNodeInfo()
{
    DCHECK(!m_InterfacePath.empty());

    std::string instanceId = GetDeviceFromInterface(m_InterfacePath);
    if (instanceId.empty())
        return;

    auto info = DeviceNodeInfo{ instanceId };

    // TODO implement fHasSpecificHardwareMatch from DirectInput code?

    DEVINST node = OpenDevNode(instanceId);

    info.manufacturer = PropertyDataCast<std::string>(GetDevNodeProperty(node, &DEVPKEY_Device_Manufacturer, DEVPROP_TYPE_STRING));
    info.displayName = PropertyDataCast<std::string>(GetDevNodeProperty(node, &DEVPKEY_NAME, DEVPROP_TYPE_STRING));
    info.service = PropertyDataCast<std::string>(GetDevNodeProperty(node, &DEVPKEY_Device_Service, DEVPROP_TYPE_STRING));
    info.deviceClass = PropertyDataCast<std::string>(GetDevNodeProperty(node, &DEVPKEY_Device_Class, DEVPROP_TYPE_STRING));
    info.stack = PropertyDataCast<std::vector<std::string>>(GetDevNodeProperty(node, &DEVPKEY_Device_Stack, DEVPROP_TYPE_STRING_LIST));
    // TODO extract VID/PID/COL from hardwareId?
    info.hardwareIds = PropertyDataCast<std::vector<std::string>>(GetDevNodeProperty(node, &DEVPKEY_Device_HardwareIds, DEVPROP_TYPE_STRING_LIST));
    info.busTypeGuid = PropertyDataCast<GUID>(GetDevNodeProperty(node, &DEVPKEY_Device_BusTypeGuid, DEVPROP_TYPE_GUID));

    if (!info.instanceId.empty())
        m_DevNode = std::move(info);
}

void RawInputDevice::TryQueryUsbInfo()
{
    if (!m_DevNode)
        return;

    std::string usbInterfacePath = SearchParentDeviceInterface(m_DevNode->instanceId, &GUID_DEVINTERFACE_USB_DEVICE);
    if (!usbInterfacePath.empty())
    {
        m_UsbInfo = UsbDeviceInfo(GetDeviceFromInterface(m_InterfacePath));
	}
}

void RawInputDevice::TryQueryXboxInfo()
{
    if (!m_DevNode)
        return;

    XboxInfo info;
    bool hasAnything = false;

    // XInput
    // {EC87F1E3-C13B-4100-B5F7-8B84D54260CB}
    static constexpr GUID XUSB_INTERFACE_CLASS_GUID =
    { 0xEC87F1E3, 0xC13B, 0x4100, { 0xB5, 0xF7, 0x8B, 0x84, 0xD5, 0x42, 0x60, 0xCB } };

    std::string xInputInterfacePath = SearchParentDeviceInterface(m_DevNode->instanceId, &XUSB_INTERFACE_CLASS_GUID);
    if (!xInputInterfacePath.empty())
    {
        info.xInputInterfacePath = xInputInterfacePath;

        ScopedHandle h = OpenDeviceInterface(xInputInterfacePath);
        if (IsValidHandle(h.get()))
        {
            static constexpr uint8_t kInvalidXInputUserId = 0xff;
            std::array<uint8_t, 3> req{ 0x01, 0x01, 0x00 };
            std::array<uint8_t, 3> led{};
            DWORD len = 0;

            constexpr DWORD IOCTL_XUSB_GET_LED_STATE = 0x8000E008;
            if (::DeviceIoControl(h.get(), IOCTL_XUSB_GET_LED_STATE,
                req.data(), static_cast<DWORD>(req.size()),
                led.data(), static_cast<DWORD>(led.size()),
                &len, nullptr))
            {


                DCHECK_EQ(len, led.size());

                static constexpr uint8_t kLedToPort[] = {
                    kInvalidXInputUserId, kInvalidXInputUserId,
                    0, 1, 2, 3, 0, 1, 2, 3,
                    kInvalidXInputUserId, kInvalidXInputUserId,
                    kInvalidXInputUserId, kInvalidXInputUserId,
                    kInvalidXInputUserId, kInvalidXInputUserId,
                };

                const uint8_t ledState = led[2];
                DCHECK_LT(ledState, std::size(kLedToPort));
                info.xInputUserIndex = kLedToPort[ledState];
            }

        }
		hasAnything = true;
    }

    // GIP
    // {020BC73C-0DCA-4EE3-96D5-AB006ADA5938}
    static constexpr GUID GUID_DEVINTERFACE_DC1_CONTROLLER =
    { 0x020BC73C, 0x0DCA, 0x4EE3, { 0x96, 0xD5, 0xAB, 0x00, 0x6A, 0xDA, 0x59, 0x38 } };

    std::string gipInterfacePath = SearchParentDeviceInterface(m_DevNode->instanceId, &GUID_DEVINTERFACE_DC1_CONTROLLER);
    if (!gipInterfacePath.empty())
    {
        info.gipInterfacePath = gipInterfacePath;

		// Fixup serial number for Xbox One GIP controllers.
        if ((IsUsbDevice() && m_UsbInfo->m_VendorId == 0x045E && m_UsbInfo->m_ProductId == 0x02FF))
        {
            const std::string& serial = m_UsbInfo->m_SerialNumber;

            if (serial.size() <= 12)
            {
                info.gipSerial = serial;
            }
            else
            {
                info.gipSerial.clear();
                for (size_t i = 0; i < serial.size(); ++i)
                    if (i % 2 != 0)
                        info.gipSerial.push_back(serial[i]);
            }
        }
    }

    if (hasAnything)
        m_XboxInfo = std::move(info);
}

void RawInputDevice::TryQueryBluetoothLEInfo()
{
    if (!m_DevNode)
        return;

    // {6e3bb679-4372-40c8-9eaa-4509df260cd8}
    static constexpr GUID GUID_BLUETOOTH_GATT_SERVICE_DEVICE_INTERFACE =
    { 0x6e3bb679, 0x4372, 0x40c8, { 0x9e, 0xaa, 0x45, 0x09, 0xdf, 0x26, 0x0c, 0xd8 } };

    std::string bleInterfacePath = SearchParentDeviceInterface(m_DevNode->instanceId, &GUID_BLUETOOTH_GATT_SERVICE_DEVICE_INTERFACE);
    if (bleInterfacePath.empty())
        return;

    auto info = BluetoothLEInfo{ bleInterfacePath };

    const DEVINST node = OpenDevNode(GetDeviceFromInterface(bleInterfacePath));

    static constexpr DEVPROPKEY DEVPKEY_Bluetooth_DeviceAddress = { { 0x2BD67D8B, 0x8BEB, 0x48D5, { 0x87, 0xE0, 0x6C, 0xDA, 0x34, 0x28, 0x04, 0x0A } }, 1 };
    static constexpr DEVPROPKEY DEVPKEY_Bluetooth_DeviceManufacturer = { { 0x2BD67D8B, 0x8BEB, 0x48D5, { 0x87, 0xE0, 0x6C, 0xDA, 0x34, 0x28, 0x04, 0x0A } }, 4 };
    static constexpr DEVPROPKEY DEVPKEY_Bluetooth_DeviceModelNumber = { { 0x2BD67D8B, 0x8BEB, 0x48D5, { 0x87, 0xE0, 0x6C, 0xDA, 0x34, 0x28, 0x04, 0x0A } }, 5 };
    static constexpr DEVPROPKEY DEVPKEY_Bluetooth_DeviceVID = { { 0x2BD67D8B, 0x8BEB, 0x48D5, { 0x87, 0xE0, 0x6C, 0xDA, 0x34, 0x28, 0x04, 0x0A } }, 7 };
    static constexpr DEVPROPKEY DEVPKEY_Bluetooth_DevicePID = { { 0x2BD67D8B, 0x8BEB, 0x48D5, { 0x87, 0xE0, 0x6C, 0xDA, 0x34, 0x28, 0x04, 0x0A } }, 8 };
    static constexpr DEVPROPKEY DEVPKEY_Bluetooth_DeviceProductVersion = { { 0x2BD67D8B, 0x8BEB, 0x48D5, { 0x87, 0xE0, 0x6C, 0xDA, 0x34, 0x28, 0x04, 0x0A } }, 9 };

    info.manufacturer = PropertyDataCast<std::string>(GetDevNodeProperty(node, &DEVPKEY_Bluetooth_DeviceManufacturer, DEVPROP_TYPE_STRING));
    info.modelNumber = PropertyDataCast<std::string>(GetDevNodeProperty(node, &DEVPKEY_Bluetooth_DeviceModelNumber, DEVPROP_TYPE_STRING));
    info.address = PropertyDataCast<std::string>(GetDevNodeProperty(node, &DEVPKEY_Bluetooth_DeviceAddress, DEVPROP_TYPE_STRING));
    info.vendorId = PropertyDataCast<uint16_t>(GetDevNodeProperty(node, &DEVPKEY_Bluetooth_DeviceVID, DEVPROP_TYPE_UINT16));
    info.productId = PropertyDataCast<uint16_t>(GetDevNodeProperty(node, &DEVPKEY_Bluetooth_DevicePID, DEVPROP_TYPE_UINT16));
    info.versionNumber = PropertyDataCast<uint16_t>(GetDevNodeProperty(node, &DEVPKEY_Bluetooth_DeviceProductVersion, DEVPROP_TYPE_UINT16));

    if (!info.interfacePath.empty())
        m_BleInfo = std::move(info);
}

void RawInputDevice::ResolveIdentity()
{
    if (!m_DevNode)
        return;

    GUID hid_guid;
    ::HidD_GetHidGuid(&hid_guid);

	std::string hidInterfacePath = SearchParentDeviceInterface(m_DevNode->instanceId, &hid_guid);
    if (!hidInterfacePath.empty())
    {
		m_HidInfo = HidDeviceInfo { hidInterfacePath };
    }

    DCHECK(m_HidInfo);

    ScopedHandle hidHandle = OpenDeviceInterface(m_HidInfo->hidInterfacePath, m_IsInterfaceReadOnly);
    if (!IsValidHandle(hidHandle.get()))
        return;

    std::wstring buffer;
    buffer.resize(128);

    if (::HidD_GetManufacturerString(hidHandle.get(), buffer.data(), static_cast<ULONG>(buffer.size())))
        m_Identity.manufacturer = utf8::narrow(buffer);

    if (::HidD_GetProductString(hidHandle.get(), buffer.data(), static_cast<ULONG>(buffer.size())))
        m_Identity.product = utf8::narrow(buffer);

    if (::HidD_GetSerialNumberString(hidHandle.get(), buffer.data(), static_cast<ULONG>(buffer.size())))
        m_Identity.serial = utf8::narrow(buffer);

    HIDD_ATTRIBUTES attrib { sizeof(HIDD_ATTRIBUTES) };
    if (::HidD_GetAttributes(hidHandle.get(), &attrib))
    {
        m_Identity.vendorId = attrib.VendorID;
        m_Identity.productId = attrib.ProductID;
        m_Identity.versionNumber = attrib.VersionNumber;
    }

    if (m_UsbInfo)
    {
        m_Identity.vendorId = m_UsbInfo->m_VendorId;
        m_Identity.productId = m_UsbInfo->m_ProductId;
        m_Identity.versionNumber = m_UsbInfo->m_VersionNumber;
        m_Identity.manufacturer = m_UsbInfo->m_Manufacturer;
        m_Identity.product = m_UsbInfo->m_Product;
        m_Identity.serial = m_UsbInfo->m_SerialNumber;
    }

    if (m_BleInfo)
    {
        if (!m_BleInfo->manufacturer.empty())
            m_Identity.manufacturer = m_BleInfo->manufacturer;
        if (!m_BleInfo->modelNumber.empty())
            m_Identity.product = m_BleInfo->modelNumber;
        if (!m_BleInfo->address.empty())
            m_Identity.serial = m_BleInfo->address;
        if (m_BleInfo->vendorId)
            m_Identity.vendorId = m_BleInfo->vendorId;
        if (m_BleInfo->productId)
            m_Identity.productId = m_BleInfo->productId;
        if (m_BleInfo->versionNumber)
            m_Identity.versionNumber = m_BleInfo->versionNumber;
    }

    if (m_XboxInfo && !m_XboxInfo->gipInterfacePath.empty())
    {
        m_Identity.serial = m_XboxInfo->gipSerial;
    }

    // fallback
    if (m_Identity.product.empty())
    {
        m_Identity.product = std::format("Unknown Device (VID:{:04X} PID:{:04X})",
            m_Identity.vendorId,
            m_Identity.productId);
    }
}

bool RawInputDevice::QueryRawDeviceInfo(HANDLE handle, RID_DEVICE_INFO* deviceInfo)
{
    UINT size = sizeof(RID_DEVICE_INFO);
    UINT result = ::GetRawInputDeviceInfoW(handle, RIDI_DEVICEINFO, deviceInfo, &size);

    if (result == static_cast<UINT>(-1))
    {
        DBGPRINT("GetRawInputDeviceInfo() failed for 0x%x handle", handle);
        return false;
    }
    DCHECK_EQ(size, result);

    return true;
}

std::string RawInputDevice::QueryRawDeviceInterfacePath(HANDLE handle)
{
    UINT size = 0;
    UINT result = ::GetRawInputDeviceInfoW(handle, RIDI_DEVICENAME, nullptr, &size);
    if (result == static_cast<UINT>(-1))
    {
        DBGPRINT("GetRawInputDeviceInfo() failed for 0x%x handle", handle);
        return {};
    }
    DCHECK_EQ(0u, result);
    std::wstring buffer(size, 0);
    result = ::GetRawInputDeviceInfoW(handle, RIDI_DEVICENAME, buffer.data(), &size);
    if (result == static_cast<UINT>(-1))
    {
        DBGPRINT("GetRawInputDeviceInfo() failed for 0x%x handle", handle);
        return {};
    }
    DCHECK_EQ(size, result);
	return utf8::narrow(buffer);
}

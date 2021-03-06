#include "pch.h"
#include "framework.h"

#include "RawInputDeviceMouse.h"

#include <hidsdi.h>

#include <ntddmou.h>

RawInputDeviceMouse::RawInputDeviceMouse(HANDLE handle)
    : RawInputDevice(handle)
{
    m_IsValid = QueryDeviceInfo();

    DBGPRINT("New Mouse device: '%s', Interface: `%s`", GetProductString().c_str(), GetInterfacePath().c_str());

    if (IsHidDevice())
        DBGPRINT("  ->Its HID Device[VID:%04X,PID:%04X]: Interface: `%s`", GetVendorId(), GetProductId(), m_HidInterfacePath.c_str());

    if (IsUsbDevice())
        DBGPRINT("  ->Its USB Device[VID:%04X,PID:%04X,VER:%04X]: Manufacturer: '%s', Product: '%s', Serial Number: `%s`, Interface: `%s`", m_UsbVendorId, m_UsbProductId, m_UsbVersionNumber, m_UsbDeviceManufacturer.c_str(), m_UsbDeviceProduct.c_str(), m_UsbDeviceSerialNumber.c_str(), m_UsbDeviceInterface.c_str());
}

RawInputDeviceMouse::~RawInputDeviceMouse()
{
    DBGPRINT("Removed Mouse device: '%s', Interface: `%s`", GetProductString().c_str(), GetInterfacePath().c_str());
}

void RawInputDeviceMouse::OnInput(const RAWINPUT* input)
{
    if (input == nullptr || input->header.dwType != RIM_TYPEMOUSE)
    {
        DBGPRINT("Wrong mouse input.");
        return;
    }

    const RAWMOUSE& rawMouse = input->data.mouse;

    if ((rawMouse.usFlags & MOUSE_MOVE_ABSOLUTE) == MOUSE_MOVE_ABSOLUTE)
    {
        //bool isVirtualDesktop = (rawMouse.usFlags & MOUSE_VIRTUAL_DESKTOP) == MOUSE_VIRTUAL_DESKTOP;

        //int width = GetSystemMetrics(isVirtualDesktop ? SM_CXVIRTUALSCREEN : SM_CXSCREEN);
        //int height = GetSystemMetrics(isVirtualDesktop ? SM_CYVIRTUALSCREEN : SM_CYSCREEN);

        //int absoluteX = static_cast<int>((rawMouse.lLastX / static_cast<float>(USHRT_MAX)) * width);
        //int absoluteY = static_cast<int>((rawMouse.lLastY / static_cast<float>(USHRT_MAX)) * height);

        //DBGPRINT("AbsoluteMove absoluteX=%d, absoluteY=%d\n", absoluteX, absoluteY);
    }
    else if (rawMouse.lLastX != 0 && rawMouse.lLastY != 0)
    {
        //int relativeX = rawMouse.lLastX;
        //int relativeY = rawMouse.lLastY;

        //DBGPRINT("RelativeMove relativeX=%d, relativeY=%d\n", relativeX, relativeY);
    }

    if ((rawMouse.usButtonFlags & RI_MOUSE_WHEEL) == RI_MOUSE_WHEEL ||
        (rawMouse.usButtonFlags & RI_MOUSE_HWHEEL) == RI_MOUSE_HWHEEL)
    {
        static const unsigned long defaultScrollLinesPerWheelDelta = 3;
        static const unsigned long defaultScrollCharsPerWheelDelta = 1;

        bool isHorizontalScroll = (rawMouse.usButtonFlags & RI_MOUSE_HWHEEL) == RI_MOUSE_HWHEEL;
        bool isScrollByPage = false;

        float wheelDelta = (float)(short)rawMouse.usButtonData;
        float numTicks = wheelDelta / WHEEL_DELTA;
        float scrollDelta = numTicks;

        if (isHorizontalScroll)
        {
            unsigned long scrollChars = defaultScrollCharsPerWheelDelta;
            SystemParametersInfo(SPI_GETWHEELSCROLLCHARS, 0, &scrollChars, 0);
            scrollDelta *= scrollChars;
        }
        else
        {
            unsigned long scrollLines = defaultScrollLinesPerWheelDelta;
            SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &scrollLines, 0);
            if (scrollLines == WHEEL_PAGESCROLL)
                isScrollByPage = true;
            else
                scrollDelta *= scrollLines;
        }

        //DBGPRINT("Wheel Scroll wheelDelta=%f, numTicks=%f, scrollDelta=%f, isHorizontalScroll=%d, isScrollByPage=%d\n", wheelDelta, numTicks, scrollDelta, isHorizontalScroll, isScrollByPage);
    }

    if ((rawMouse.usButtonFlags & RI_MOUSE_BUTTON_1_DOWN) != 0)
    {

    }
}

bool RawInputDeviceMouse::QueryDeviceInfo()
{
    if (!RawInputDevice::QueryDeviceInfo())
        return false;

    if (!m_MouseInfo.QueryInfo(m_Handle))
    {
        DBGPRINT("Cannot get Raw Input Mouse info from '%s'.", m_InterfacePath.c_str());
        return false;
    }

    /*if (IsValidHandle(m_RawInputInfo.m_InterfaceHandle.get()))
    {
        PHIDP_PREPARSED_DATA pp_data = NULL;
        auto res = HidD_GetPreparsedData(m_RawInputInfo.m_InterfaceHandle.get(), &pp_data);
    }*/

    return true;
}

bool RawInputDeviceMouse::MouseInfo::QueryInfo(HANDLE handle)
{
    RID_DEVICE_INFO device_info;

    if (!RawInputDevice::QueryRawDeviceInfo(handle, &device_info))
        return false;

    DCHECK_EQ(device_info.dwType, static_cast<DWORD>(RIM_TYPEMOUSE));

    const RID_DEVICE_INFO_MOUSE& mouseInfo = device_info.mouse;

    m_NumberOfButtons = static_cast<uint16_t>(mouseInfo.dwNumberOfButtons);
    m_SampleRate = static_cast<uint16_t>(mouseInfo.dwSampleRate);
    m_HasHorizontalWheel = mouseInfo.fHasHorizontalWheel;

    if ((mouseInfo.dwId & (WHEELMOUSE_I8042_HARDWARE | WHEELMOUSE_SERIAL_HARDWARE | WHEELMOUSE_HID_HARDWARE)) != 0)
        m_HasVerticalWheel = true;

    if ((mouseInfo.dwId & HORIZONTAL_WHEEL_PRESENT) != 0)
        m_HasHorizontalWheel = true;

    return true;
}

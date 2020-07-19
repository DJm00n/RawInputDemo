#include "framework.h"

#include "RawInputDeviceMouse.h"

RawInputDeviceMouse::RawInputDeviceMouse(HANDLE handle)
    : RawInputDevice(handle)
{
    m_IsValid = true;
}

RawInputDeviceMouse::~RawInputDeviceMouse()
{

}

void RawInputDeviceMouse::OnInput(const RAWINPUT* input)
{
    if (input == nullptr || input->header.dwType != RIM_TYPEMOUSE)
    {
        DBGPRINT(L"Wrong mouse input.");
        return;
    }

    const RAWMOUSE* pRawMouse = &input->data.mouse;

    if ((pRawMouse->usFlags & MOUSE_MOVE_ABSOLUTE) == MOUSE_MOVE_ABSOLUTE)
    {
        bool isVirtualDesktop = (pRawMouse->usFlags & MOUSE_VIRTUAL_DESKTOP) == MOUSE_VIRTUAL_DESKTOP;

        int width = GetSystemMetrics(isVirtualDesktop ? SM_CXVIRTUALSCREEN : SM_CXSCREEN);
        int height = GetSystemMetrics(isVirtualDesktop ? SM_CYVIRTUALSCREEN : SM_CYSCREEN);

        int absoluteX = int((pRawMouse->lLastX / 65535.0f) * width);
        int absoluteY = int((pRawMouse->lLastY / 65535.0f) * height);

        //DBGPRINT(L"AbsoluteMove absoluteX=%d, absoluteY=%d\n", absoluteX, absoluteY);
    }
    else if (pRawMouse->lLastX != 0 && pRawMouse->lLastY != 0)
    {
        int relativeX = pRawMouse->lLastX;
        int relativeY = pRawMouse->lLastY;

        //DBGPRINT(L"RelativeMove relativeX=%d, relativeY=%d\n", relativeX, relativeY);
    }

    if ((pRawMouse->usButtonFlags & RI_MOUSE_WHEEL) == RI_MOUSE_WHEEL ||
        (pRawMouse->usButtonFlags & RI_MOUSE_HWHEEL) == RI_MOUSE_HWHEEL)
    {
        static const unsigned long defaultScrollLinesPerWheelDelta = 3;
        static const unsigned long defaultScrollCharsPerWheelDelta = 1;

        bool isHorizontalScroll = (pRawMouse->usButtonFlags & RI_MOUSE_HWHEEL) == RI_MOUSE_HWHEEL;
        bool isScrollByPage = false;

        float wheelDelta = (float)(short)pRawMouse->usButtonData;
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

        DBGPRINT(L"Wheel Scroll wheelDelta=%f, numTicks=%f, scrollDelta=%f, isHorizontalScroll=%d, isScrollByPage=%d\n", wheelDelta, numTicks, scrollDelta, isHorizontalScroll, isScrollByPage);
    }

    if ((pRawMouse->usButtonFlags & RI_MOUSE_BUTTON_1_DOWN) != 0)
    {

    }
}

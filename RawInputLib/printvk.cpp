#include "pch.h"

#include "framework.h"


#include "printvk.h"

#include <ime.h>
#include <kbd.h>

std::string VkToString(uint16_t vk)
{
#define printvk(vk) case vk: return #vk
    switch (vk)
    {
        printvk(VK_LBUTTON);
        printvk(VK_RBUTTON);
        printvk(VK_CANCEL);
        printvk(VK_MBUTTON);
        printvk(VK_XBUTTON1);
        printvk(VK_XBUTTON2);
        printvk(VK_BACK);
        printvk(VK_TAB);
        printvk(VK_CLEAR);
        printvk(VK_RETURN);
        printvk(VK_SHIFT);
        printvk(VK_CONTROL);
        printvk(VK_MENU);
        printvk(VK_PAUSE);
        printvk(VK_CAPITAL);
        printvk(VK_KANA);
        printvk(VK_IME_ON);
        printvk(VK_JUNJA);
        printvk(VK_FINAL);
        printvk(VK_HANJA);
        printvk(VK_IME_OFF);
        printvk(VK_ESCAPE);
        printvk(VK_CONVERT);
        printvk(VK_NONCONVERT);
        printvk(VK_ACCEPT);
        printvk(VK_MODECHANGE);
        printvk(VK_SPACE);
        printvk(VK_PRIOR);
        printvk(VK_NEXT);
        printvk(VK_END);
        printvk(VK_HOME);
        printvk(VK_LEFT);
        printvk(VK_UP);
        printvk(VK_RIGHT);
        printvk(VK_DOWN);
        printvk(VK_SELECT);
        printvk(VK_PRINT);
        printvk(VK_EXECUTE);
        printvk(VK_SNAPSHOT);
        printvk(VK_INSERT);
        printvk(VK_DELETE);
        printvk(VK_HELP);
        printvk(VK_LWIN);
        printvk(VK_RWIN);
        printvk(VK_APPS);
        printvk(VK_SLEEP);
        printvk(VK_NUMPAD0);
        printvk(VK_NUMPAD1);
        printvk(VK_NUMPAD2);
        printvk(VK_NUMPAD3);
        printvk(VK_NUMPAD4);
        printvk(VK_NUMPAD5);
        printvk(VK_NUMPAD6);
        printvk(VK_NUMPAD7);
        printvk(VK_NUMPAD8);
        printvk(VK_NUMPAD9);
        printvk(VK_MULTIPLY);
        printvk(VK_ADD);
        printvk(VK_SEPARATOR);
        printvk(VK_SUBTRACT);
        printvk(VK_DECIMAL);
        printvk(VK_DIVIDE);
        printvk(VK_F1);
        printvk(VK_F2);
        printvk(VK_F3);
        printvk(VK_F4);
        printvk(VK_F5);
        printvk(VK_F6);
        printvk(VK_F7);
        printvk(VK_F8);
        printvk(VK_F9);
        printvk(VK_F10);
        printvk(VK_F11);
        printvk(VK_F12);
        printvk(VK_F13);
        printvk(VK_F14);
        printvk(VK_F15);
        printvk(VK_F16);
        printvk(VK_F17);
        printvk(VK_F18);
        printvk(VK_F19);
        printvk(VK_F20);
        printvk(VK_F21);
        printvk(VK_F22);
        printvk(VK_F23);
        printvk(VK_F24);
        printvk(VK_NAVIGATION_VIEW);
        printvk(VK_NAVIGATION_MENU);
        printvk(VK_NAVIGATION_UP);
        printvk(VK_NAVIGATION_DOWN);
        printvk(VK_NAVIGATION_LEFT);
        printvk(VK_NAVIGATION_RIGHT);
        printvk(VK_NAVIGATION_ACCEPT);
        printvk(VK_NAVIGATION_CANCEL);
        printvk(VK_NUMLOCK);
        printvk(VK_SCROLL);
        printvk(VK_OEM_NEC_EQUAL);
        printvk(VK_OEM_FJ_MASSHOU);
        printvk(VK_OEM_FJ_TOUROKU);
        printvk(VK_OEM_FJ_LOYA);
        printvk(VK_OEM_FJ_ROYA);
        printvk(VK_LSHIFT);
        printvk(VK_RSHIFT);
        printvk(VK_LCONTROL);
        printvk(VK_RCONTROL);
        printvk(VK_LMENU);
        printvk(VK_RMENU);
        printvk(VK_BROWSER_BACK);
        printvk(VK_BROWSER_FORWARD);
        printvk(VK_BROWSER_REFRESH);
        printvk(VK_BROWSER_STOP);
        printvk(VK_BROWSER_SEARCH);
        printvk(VK_BROWSER_FAVORITES);
        printvk(VK_BROWSER_HOME);
        printvk(VK_VOLUME_MUTE);
        printvk(VK_VOLUME_DOWN);
        printvk(VK_VOLUME_UP);
        printvk(VK_MEDIA_NEXT_TRACK);
        printvk(VK_MEDIA_PREV_TRACK);
        printvk(VK_MEDIA_STOP);
        printvk(VK_MEDIA_PLAY_PAUSE);
        printvk(VK_LAUNCH_MAIL);
        printvk(VK_LAUNCH_MEDIA_SELECT);
        printvk(VK_LAUNCH_APP1);
        printvk(VK_LAUNCH_APP2);
        printvk(VK_OEM_1);
        printvk(VK_OEM_PLUS);
        printvk(VK_OEM_COMMA);
        printvk(VK_OEM_MINUS);
        printvk(VK_OEM_PERIOD);
        printvk(VK_OEM_2);
        printvk(VK_OEM_3);
        printvk(VK_GAMEPAD_A);
        printvk(VK_GAMEPAD_B);
        printvk(VK_GAMEPAD_X);
        printvk(VK_GAMEPAD_Y);
        printvk(VK_GAMEPAD_RIGHT_SHOULDER);
        printvk(VK_GAMEPAD_LEFT_SHOULDER);
        printvk(VK_GAMEPAD_LEFT_TRIGGER);
        printvk(VK_GAMEPAD_RIGHT_TRIGGER);
        printvk(VK_GAMEPAD_DPAD_UP);
        printvk(VK_GAMEPAD_DPAD_DOWN);
        printvk(VK_GAMEPAD_DPAD_LEFT);
        printvk(VK_GAMEPAD_DPAD_RIGHT);
        printvk(VK_GAMEPAD_MENU);
        printvk(VK_GAMEPAD_VIEW);
        printvk(VK_GAMEPAD_LEFT_THUMBSTICK_BUTTON);
        printvk(VK_GAMEPAD_RIGHT_THUMBSTICK_BUTTON);
        printvk(VK_GAMEPAD_LEFT_THUMBSTICK_UP);
        printvk(VK_GAMEPAD_LEFT_THUMBSTICK_DOWN);
        printvk(VK_GAMEPAD_LEFT_THUMBSTICK_RIGHT);
        printvk(VK_GAMEPAD_LEFT_THUMBSTICK_LEFT);
        printvk(VK_GAMEPAD_RIGHT_THUMBSTICK_UP);
        printvk(VK_GAMEPAD_RIGHT_THUMBSTICK_DOWN);
        printvk(VK_GAMEPAD_RIGHT_THUMBSTICK_RIGHT);
        printvk(VK_GAMEPAD_RIGHT_THUMBSTICK_LEFT);
        printvk(VK_OEM_4);
        printvk(VK_OEM_5);
        printvk(VK_OEM_6);
        printvk(VK_OEM_7);
        printvk(VK_OEM_8);
        printvk(VK_OEM_AX);
        printvk(VK_OEM_102);
        printvk(VK_ICO_HELP);
        printvk(VK_ICO_00);
        printvk(VK_PROCESSKEY);
        printvk(VK_ICO_CLEAR);
        printvk(VK_PACKET);
        printvk(VK_OEM_CLEAR);
        printvk(VK_ABNT_C1);
        printvk(VK_ABNT_C2);
        printvk(VK_DBE_ALPHANUMERIC);
        printvk(VK_DBE_KATAKANA);
        printvk(VK_DBE_HIRAGANA);
        printvk(VK_DBE_SBCSCHAR);
        printvk(VK_DBE_DBCSCHAR);
        printvk(VK_DBE_ROMAN);
        printvk(VK_DBE_NOROMAN);
        printvk(VK_DBE_ENTERWORDREGISTERMODE);
        printvk(VK_DBE_ENTERIMECONFIGMODE);
        printvk(VK_DBE_FLUSHSTRING);
        printvk(VK_DBE_CODEINPUT);
        printvk(VK_DBE_NOCODEINPUT);
        printvk(VK_DBE_DETERMINESTRING);
        printvk(VK_DBE_ENTERDLGCONVERSIONMODE);
    }
#undef printvk

    std::array<char, 10> str;
    if ((vk >= (uint16_t)'A') && (vk <= (uint16_t)'Z') ||
        (vk >= (uint16_t)'0') && (vk <= (uint16_t)'9'))
    {
        std::snprintf(str.data(), str.size(), "VK_%c", vk);
    }
    else if (vk == 0)
    {
        return "VK_NONE";
    }
    else
    {
        std::snprintf(str.data(), str.size(), "VK_0x%x", vk);
    }

    return std::string(str.data(), str.size());
}

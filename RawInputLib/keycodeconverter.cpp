#include "pch.h"
#include "framework.h"

#include "keycodeconverter.h"

// This structure is used to define the keycode mapping table.
typedef struct {
    // USB keycode:
    //  Upper 16-bits: USB Usage Page.
    //  Lower 16-bits: USB Usage Id: Assigned ID within this usage page.
    uint32_t usb_keycode;

    // Contains one of the following:
    //  On Windows: Windows OEM scancode
    int native_keycode;

    const char* code;
} KeycodeMapEntry;


#define DOM_CODE(usb, evdev, xkb, win, mac, code, id) \
  { usb, win, code }

#define DOM_CODE_DECLARATION constexpr KeycodeMapEntry keyCodeMappings[] =
#include "dom_code_data.inc"
#undef DOM_CODE
#undef DOM_CODE_DECLARATION

// static
int KeycodeConverter::InvalidNativeKeycode() {
    return keyCodeMappings[0].native_keycode;
}

// static
uint32_t KeycodeConverter::InvalidUsbKeycode() {
    return keyCodeMappings[0].usb_keycode;
}

// static
int KeycodeConverter::UsbKeycodeToNativeKeycode(uint32_t usb_keycode)
{
    // Deal with some special-cases that don't fit the 1:1 mapping.
    if (usb_keycode == 0x070032)  // non-US hash.
        usb_keycode = 0x070031;     // US backslash.

    for (auto& mapping : keyCodeMappings) {
        if (mapping.usb_keycode == usb_keycode)
            return mapping.native_keycode;
    }
    return InvalidNativeKeycode();
}

// static
uint32_t KeycodeConverter::NativeKeycodeToUsbKeycode(int native_keycode)
{
    for (auto& mapping : keyCodeMappings) {
        if (mapping.native_keycode == native_keycode)
            return mapping.usb_keycode;
    }
    return InvalidUsbKeycode();
}

// static
uint32_t KeycodeConverter::CodeStringToUsbKeycode(const std::string& code)
{
    if (code.empty())
        return InvalidUsbKeycode();

    for (auto& mapping : keyCodeMappings) {
        if (mapping.code && code == mapping.code) {
            return mapping.usb_keycode;
        }
    }
    return InvalidUsbKeycode();
}

// static
int KeycodeConverter::CodeStringToNativeKeycode(const std::string& code)
{
    return UsbKeycodeToNativeKeycode(CodeStringToUsbKeycode(code));
}

// static
const char* KeycodeConverter::UsbKeycodeToCodeString(uint32_t usb_keycode)
{
    for (auto& mapping : keyCodeMappings) {
        if (mapping.usb_keycode == usb_keycode) {
            if (mapping.code)
                return mapping.code;
            break;
        }
    }
    return "";
}



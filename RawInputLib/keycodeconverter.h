#pragma once


// A class to convert between the current platform's native keycode (scancode) and usb hid usage codes
class KeycodeConverter
{
public:
    KeycodeConverter() = delete;
    KeycodeConverter(const KeycodeConverter&) = delete;
    KeycodeConverter& operator=(const KeycodeConverter&) = delete;

    // Return the value that identifies an invalid native keycode.
    static int InvalidNativeKeycode();

    // The following methods relate to USB keycodes.
    // Note that USB keycodes are not part of any web standard.
    // Please don't use USB keycodes in new code.

    // Return the value that identifies an invalid USB keycode.
    static uint32_t InvalidUsbKeycode();

    // Convert a USB keycode into an equivalent platform native keycode.
    static int UsbKeycodeToNativeKeycode(uint32_t usb_keycode);

    // Convert a platform native keycode into an equivalent USB keycode.
    static uint32_t NativeKeycodeToUsbKeycode(int native_keycode);

    // Convert a UI Event |code| string into a USB keycode value.
    static uint32_t CodeStringToUsbKeycode(const std::string& code);

    // Convert a UI Event |code| string into a native keycode.
    static int CodeStringToNativeKeycode(const std::string& code);

    // Convert a USB keycode into keycode string.
    static const char* UsbKeycodeToCodeString(uint32_t usb_keycode);
};


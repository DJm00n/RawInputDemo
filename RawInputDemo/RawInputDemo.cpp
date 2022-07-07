// RawInputDemo.cpp : Defines the entry point for the application.
//

#include "framework.h"

#include "RawInputDemo.h"

#include <RawInputDeviceManager.h>

#include <windowsx.h>
#include <algorithm>
#include <array>
#include <cwctype>
#include <codecvt>

RawInputDeviceManager rawDeviceManager;

#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_RAWINPUTDEMO, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_RAWINPUTDEMO));

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_RAWINPUTDEMO));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_RAWINPUTDEMO);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

BOOL WndProc_OnCreate(HWND hWnd, LPCREATESTRUCT /*lpCreateStruct*/)
{
    return TRUE;
}

void WndProc_OnCommand(HWND hWnd, int id, HWND hWndCtl, UINT codeNotify)
{
    // Parse the menu selections:
    switch (id)
    {
    case IDM_ABOUT:
        DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
        break;
    case IDM_EXIT:
        DestroyWindow(hWnd);
        break;
    }

    return FORWARD_WM_COMMAND(hWnd, id, hWndCtl, codeNotify, DefWindowProc);
}

void WndProc_OnDestroy(HWND /*hWnd*/)
{
    PostQuitMessage(0);
}

BOOL WndProc_OnInputLangChange(HWND hwnd, UINT codePage, HKL hkl)
{
    DBGPRINT("WM_INPUTLANGCHANGE: hkl=0x%08x", hkl);

    LCID locale = MAKELCID(LOWORD(hkl), SORT_DEFAULT);
    wchar_t hklLanguage[LOCALE_NAME_MAX_LENGTH] = { 0 };
    CHECK(::LCIDToLocaleName(locale, hklLanguage, LOCALE_NAME_MAX_LENGTH, LOCALE_ALLOW_NEUTRAL_NAMES));

    wchar_t keyboardLayoutId[KL_NAMELENGTH];
    CHECK(GetKLIDFromHKL(hkl, keyboardLayoutId));

    wchar_t keyboardLayoutId2[KL_NAMELENGTH];
    CHECK(::GetKeyboardLayoutNameW(keyboardLayoutId2));

    // Check that my GetKLIDFromHKL is working
    CHECK_EQ(std::stoul(keyboardLayoutId, nullptr, 16), std::stoul(keyboardLayoutId2, nullptr, 16));

    std::string layoutLang = GetLocaleInformation(hklLanguage, LOCALE_SLOCALIZEDDISPLAYNAME);
    std::string layoutDisplayName = GetKeyboardLayoutDisplayName(keyboardLayoutId);
    std::string layoutDescription = layoutLang + " - " + layoutDisplayName;

    std::wstring layoutProfileId = GetLayoutProfileId(hkl);
    LAYOUTORTIPPROFILE layoutProfile;
    GetLayoutProfile(layoutProfileId, &layoutProfile);
    std::string layoutDescription2 = GetLayoutProfileDescription(layoutProfileId);

    DBGPRINT("Switched to `%s`", layoutDescription.c_str());

    std::wstring defaultLayoutProfileId = GetDefaultLayoutProfileId();

    return TRUE;
}

void OnChar(char32_t ch)
{
    std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> utf32conv;
    std::string utf8ch = utf32conv.to_bytes(ch);
    DBGPRINT("WM_CHAR: %s\n", GetUnicodeCharacterNames(utf8ch).c_str());
}

void WndProc_OnChar(HWND hwnd, WCHAR c, int cRepeat)
{
    /*static char16_t high_surrogate = 0;

    if (c > 0xFFFF)
        return;

    if ((c == 0 && high_surrogate == 0))
        return;

    constexpr char32_t UNICODE_CODEPOINT_INVALID = 0xFFFD;

    // UTF-32 code point could contain two UTF-16 code units
    if (IS_HIGH_SURROGATE(c)) // High surrogate, must save
    {
        if (high_surrogate != 0)
            OnChar(UNICODE_CODEPOINT_INVALID);
        high_surrogate = c;
        return;
    }

    char32_t cp = c;
    if (high_surrogate != 0)
    {
        if (!IS_SURROGATE_PAIR(high_surrogate, c)) // Invalid low surrogate
        {
            OnChar(UNICODE_CODEPOINT_INVALID);
        }
        else
        {
            cp = ((high_surrogate - HIGH_SURROGATE_START) << 10) + (c - LOW_SURROGATE_START) + 0x10000;
        }

        high_surrogate = 0;
    }
    OnChar(cp);*/
}

inline std::string ToUnicodeWrapper(uint16_t vkCode, uint16_t scanCode, bool isShift = false)
{
    const uint32_t flags = 1 << 2; // Do not change keyboard state of this thread

    static uint8_t state[256] = { 0 };
    state[VK_SHIFT] = isShift << 7; // Modifiers set the high-order bit when pressed

    wchar_t utf16Chars[10] = { 0 };
    // This call can produce multiple UTF-16 code points
    // in case of ligatures or non-BMP Unicode chars that have hi and low surrogate
    // See examples: https://kbdlayout.info/features/ligatures
    int charCount = ::ToUnicode(vkCode, scanCode, state, utf16Chars, 10, flags);

    // negative value is returned on dead key press
    if (charCount < 0)
        charCount = -charCount;

    // do not return blank space and control characters
    if ((charCount == 1) && (std::iswblank(utf16Chars[0]) || std::iswcntrl(utf16Chars[0])))
        charCount = 0;

    return utf8::narrow(utf16Chars, charCount);
}

std::string GetKeyNameTextWrapper(uint16_t scanCode)
{
    // GetKeyNameText is not working for these keys
    // due to use of broken MapVirtualKey(scanCode, MAPVK_VK_TO_CHAR) under the hood
    // See https://stackoverflow.com/a/72464584/1795050
    const uint16_t vkCode = LOWORD(MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX));
    if ((vkCode >= (uint16_t)'A') && (vkCode <= (uint16_t)'Z'))
    {
        return ToUnicodeWrapper(vkCode, scanCode);
    }

    // GetKeyNameText also have bug with keys that are outside Unicode BMP.
    // It will return garbage for that keys
    // Try to call ToUnicode manually to fix these
    switch (scanCode)
    {
    case 0x02:
    case 0x03:
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07:
    case 0x08:
    case 0x09:
    case 0x0a:
    case 0x0b:
    case 0x0c:
    case 0x0d:

    case 0x10:

    case 0x1a:
    case 0x1b:
    case 0x27:
    case 0x28:
    case 0x29:
    case 0x2b:
    case 0x2c:
    case 0x33:
    case 0x34:
    case 0x35:
        return ToUnicodeWrapper(vkCode, scanCode);
    }

    // Some keys doesn't work GetKeyNameText API at all
    switch (vkCode)
    {
    case VK_BROWSER_BACK:
        return "Browser Back";
    case VK_BROWSER_FORWARD:
        return "Browser Forward";
    case VK_BROWSER_REFRESH:
        return "Browser Refresh";
    case VK_BROWSER_STOP:
        return "Browser Stop";
    case VK_BROWSER_SEARCH:
        return "Browser Search";
    case VK_BROWSER_FAVORITES:
        return "Browser Favorites";
    case VK_BROWSER_HOME:
        return "Browser Home";
    case VK_VOLUME_MUTE:
        return "Volume Mute";
    case VK_VOLUME_DOWN:
        return "Volume Down";
    case VK_VOLUME_UP:
        return "Volume Up";
    case VK_MEDIA_NEXT_TRACK:
        return "Next Track";
    case VK_MEDIA_PREV_TRACK:
        return "Previous Track";
    case VK_MEDIA_STOP:
        return "Media Stop";
    case VK_MEDIA_PLAY_PAUSE:
        return "Media Play/Pause";
    case VK_LAUNCH_MAIL:
        return "Launch Mail";
    case VK_LAUNCH_MEDIA_SELECT:
        return "Launch Media Selector";
    case VK_LAUNCH_APP1:
        return "Launch App 1";
    case VK_LAUNCH_APP2:
        return "Launch App 2";
    }

    wchar_t name[128] = { 0 };
    const LONG lParam = MAKELONG(0, ((scanCode & 0xff00) != 0 ? KF_EXTENDED : 0) | (scanCode & 0xff));
    int charCount = ::GetKeyNameTextW(lParam, name, 128);

    return utf8::narrow(name, charCount);
}

void WndProc_OnKeydown(HWND hwnd, UINT vk, BOOL fDown, int cRepeat, UINT flags)
{
    uint16_t scanCode = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX); // broken for at least NumPad keys
    uint16_t vk2 = MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX);

    WORD scanCode2 = LOBYTE(flags);
    if (LOBYTE(flags) != 0 && (flags & KF_EXTENDED) == KF_EXTENDED)
        scanCode2 = MAKEWORD(scanCode, 0xE0);

    uint16_t vk3 = MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX);
    uint16_t vk4 = MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK);

    std::string ch = ToUnicodeWrapper(vk, scanCode2, ::GetKeyState(VK_SHIFT));

    std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> utf32conv;
    std::u32string utf32ch = utf32conv.from_bytes(ch);

    std::string name = GetKeyNameTextWrapper(scanCode2);
    std::u32string utf32name = utf32conv.from_bytes(name);

    DBGPRINT("WM_KEYDOWN: vk=%x->sc=%x->vk2=%x sc2=%x->vk3=%x (vk4=%x) ch=%s, name=`%s`\n",
        vk,
        scanCode,
        vk2,
        scanCode2,
        vk3,vk4,
        GetUnicodeCharacterNames(ch).c_str(), name.c_str());
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
/* BOOL Cls_OnWinIniChange(HWND hwnd, UINT codePage, HKL hkl) */
#define HANDLE_WM_INPUTLANGCHANGE(hwnd, wParam, lParam, fn) \
    MAKELRESULT((BOOL)(fn)((hwnd), (UINT)(wParam), (HKL)(lParam)), 0L)

    switch (message)
    {
        HANDLE_MSG(hWnd, WM_CREATE, WndProc_OnCreate);
        HANDLE_MSG(hWnd, WM_COMMAND, WndProc_OnCommand);
        HANDLE_MSG(hWnd, WM_DESTROY, WndProc_OnDestroy);
        HANDLE_MSG(hWnd, WM_INPUTLANGCHANGE, WndProc_OnInputLangChange);
        HANDLE_MSG(hWnd, WM_CHAR, WndProc_OnChar);
        HANDLE_MSG(hWnd, WM_KEYDOWN, WndProc_OnKeydown);
    default:
        return(DefWindowProc(hWnd, message, wParam, lParam));
    }

#undef HANDLE_WM_INPUTLANGCHANGE
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

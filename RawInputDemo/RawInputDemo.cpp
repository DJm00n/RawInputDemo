// RawInputDemo.cpp : Defines the entry point for the application.
//

#include "framework.h"

#include "RawInputDemo.h"

#include <RawInputDeviceManager.h>
#include <printvk.h>

#include <windowsx.h>
#include <algorithm>
#include <array>
#include <cwctype>
#include <codecvt>
#include <map>

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
      CW_USEDEFAULT, 0, 300, 100, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

// scanCode => key name in current keyboard layout
std::map<uint16_t, std::wstring> keyNames;

void UpdateKeyNames()
{
    keyNames.clear();

    auto usagesToScanCodes = GetUsagesToScanCodes();

    // Do what Win32 USER subsystem is doing: swap Pause and NumLock scan codes
    usagesToScanCodes[0x00070048] = 0xe045;

    for (const auto& usageToScanCode : usagesToScanCodes)
    {
        const uint16_t scanCode = usageToScanCode.second;

        if (scanCode == 0)
        {
            continue;
        }

        std::string keyName = GetScanCodeName(scanCode);
        if (keyName.empty())
        {
            continue;
        }

        keyNames.insert_or_assign(scanCode, utf8::widen(keyName));
    }
}

BOOL WndProc_OnCreate(HWND hWnd, LPCREATESTRUCT /*lpCreateStruct*/)
{
    UpdateKeyNames();
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

BOOL WndProc_OnInputLangChange(HWND hwnd, USHORT gdiCodePage, HKL hkl)
{
    DBGPRINT("WM_INPUTLANGCHANGE: hkl=0x%08x", hkl);

    std::string layoutDescription = GetLayoutDescription(hkl);

    std::wstring layoutProfileId = GetLayoutProfileId(hkl);
    LAYOUTORTIPPROFILE layoutProfile;
    GetLayoutProfile(layoutProfileId, &layoutProfile);
    std::string layoutDescription2 = GetLayoutProfileDescription(layoutProfileId);

    std::string layoutDescription3 = GetLayoutDescriptionIcu(hkl);

    std::string layoutDescription4 = GetLayoutDescriptionWinRT(hkl);

    DBGPRINT("Switched to `%s` input language", layoutDescription3.c_str());

    std::wstring defaultLayoutProfileId = GetDefaultLayoutProfileId();

    UpdateKeyNames();

    /*std::vector<std::string> layoutNames = EnumInstalledKeyboardLayouts();
    for (auto& layoutName : layoutNames)
    {
        std::string layoutDisplayName = GetKeyboardLayoutDisplayName(utf8::widen(layoutName).c_str());
        DBGPRINT("| %s | 0x%s |", layoutDisplayName.c_str(), layoutName.c_str());
    }*/

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

void WndProc_OnKeydown(HWND hwnd, UINT vk, BOOL fDown, int cRepeat, UINT flags)
{
    uint16_t scanCode = LOBYTE(flags);
    if (scanCode != 0)
    {
        if ((flags & KF_EXTENDED) == KF_EXTENDED)
            scanCode = MAKEWORD(scanCode, 0xE0);
    }
    else
    {
        // Windows may not report scan codes for some buttons (like multimedia buttons).
        scanCode = LOWORD(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX));
    }

    switch (vk)
    {
    case VK_SHIFT:   // -> VK_LSHIFT or VK_RSHIFT
    case VK_CONTROL: // -> VK_LCONTROL or VK_RCONTROL
    case VK_MENU:    // -> VK_LMENU or VK_RMENU
        vk = LOWORD(MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX));
        break;
    }

    std::string ch = GetStringFromKeyPress(scanCode);
    std::string name = keyNames.count(scanCode) ? utf8::narrow(keyNames.at(scanCode)) : "";

    DBGPRINT("WM_KEYDOWN: vk=%s, sc=0x%04x, ch=`%s`, keyName=`%s`\n",
        VkToString(vk).c_str(),
        scanCode,
        GetUnicodeCharacterNames(ch).c_str(), name.c_str());
}

void WndProc_OnKeyup(HWND hwnd, UINT vk, BOOL fDown, int cRepeat, UINT flags)
{
    uint16_t scanCode = LOBYTE(flags);
    if (scanCode != 0)
    {
        if ((flags & KF_EXTENDED) == KF_EXTENDED)
            scanCode = MAKEWORD(scanCode, 0xE0);
    }
    else
    {
        // Windows may not report scan codes for some buttons (like multimedia buttons).
        scanCode = LOWORD(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX));
    }

    int a = 666;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
/* BOOL Cls_OnInputLangChange(HWND hwnd, USHORT gdiCodePage, HKL hkl) */
#define HANDLE_WM_INPUTLANGCHANGE(hwnd, wParam, lParam, fn) \
    MAKELRESULT((BOOL)(fn)((hwnd), (USHORT)(wParam), (HKL)(lParam)), 0L)

    switch (message)
    {
        HANDLE_MSG(hWnd, WM_CREATE, WndProc_OnCreate);
        HANDLE_MSG(hWnd, WM_COMMAND, WndProc_OnCommand);
        HANDLE_MSG(hWnd, WM_DESTROY, WndProc_OnDestroy);
        HANDLE_MSG(hWnd, WM_INPUTLANGCHANGE, WndProc_OnInputLangChange);
        HANDLE_MSG(hWnd, WM_CHAR, WndProc_OnChar);
        HANDLE_MSG(hWnd, WM_KEYDOWN, WndProc_OnKeydown);
        //HANDLE_MSG(hWnd, WM_KEYUP, WndProc_OnKeyup);
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

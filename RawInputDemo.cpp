// RawInputDemo.cpp : Defines the entry point for the application.
//

#include "framework.h"

#include "RawInputDemo.h"
#include "RawInputDeviceManager.h"

#include <windowsx.h>

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
    rawDeviceManager.Register(hWnd);
    rawDeviceManager.EnumerateDevices();

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
    rawDeviceManager.Unregister();
    PostQuitMessage(0);
}

/* BOOL Cls_OnInput(HWND hWnd, UINT code, HRAWINPUT handle) */
#define HANDLE_WM_INPUT(hWnd, wParam, lParam, fn) \
    ((fn)((hWnd), GET_RAWINPUT_CODE_WPARAM(wParam), (HRAWINPUT)lParam) ? 0L : (LRESULT)-1L)
#define FORWARD_WM_INPUT(hWnd, code, hInput, fn) \
    (BOOL)(DWORD)(fn)((hWnd), WM_INPUT, (WPARAM)code, (LPARAM)hInput)

BOOL WndProc_OnInput(HWND hWnd, UINT code, HRAWINPUT hInput)
{
    rawDeviceManager.OnInput(hWnd, code, hInput);

    if(code == RIM_INPUT)
        return FORWARD_WM_INPUT(hWnd, code, hInput, DefWindowProc);

    return TRUE;
}

/* BOOL Cls_OnInputDeviceChange(HWND hWnd, UINT code, HANDLE handle) */
#define HANDLE_WM_INPUT_DEVICE_CHANGE(hWnd, wParam, lParam, fn) \
    ((fn)((hWnd), (UINT)wParam, (HANDLE)lParam) ? 0L : (LRESULT)-1L)

BOOL WndProc_OnInputDeviceChange(HWND hWnd, UINT code, HANDLE handle)
{
    rawDeviceManager.OnInputDeviceChange(hWnd, code, handle);

    return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        HANDLE_MSG(hWnd, WM_CREATE, WndProc_OnCreate);
        HANDLE_MSG(hWnd, WM_COMMAND, WndProc_OnCommand);
        HANDLE_MSG(hWnd, WM_DESTROY, WndProc_OnDestroy);
        HANDLE_MSG(hWnd, WM_INPUT, WndProc_OnInput);
        HANDLE_MSG(hWnd, WM_INPUT_DEVICE_CHANGE, WndProc_OnInputDeviceChange);
    default:
        return(DefWindowProc(hWnd, message, wParam, lParam));
    }
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

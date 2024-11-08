#include "system.h"

#include <windows.h>

#include <stdlib.h>

typedef struct {
    HWND hwnd;
    HDC hdc;
    BITMAPINFO *bmi;
} Win32Data;

static LRESULT CALLBACK wndproc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    SysWindow *window = (SysWindow*) GetWindowLongPtr(hwnd, GWLP_USERDATA);
    Win32Data *data = NULL;
    if (window) {
        data = (Win32Data*) window->data;
    }

    switch (message)
    {
    case WM_PAINT:
        if (window && window->buffer && data) {
            StretchDIBits(data->hdc,
                          window->dst_ox, window->dst_oy, window->dst_width, window->dst_height,
                          0, 0, window->buffer_width, window->buffer_height,
                          window->buffer, data->bmi, DIB_RGB_COLORS, SRCCOPY);

            ValidateRect(hwnd, NULL);
        }
        break;

    case WM_DESTROY:
    case WM_CLOSE:
        if (window) {
            window->close = true;
        }
        break;

    case WM_SIZE:
        if (window) {
            window->dst_ox = 0;
            window->dst_oy = 0;
            window->dst_width = LOWORD(lparam);
            window->dst_height = HIWORD(lparam);
            window->width = window->dst_width;
            window->height = window->dst_height;
            BitBlt(data->hdc, 0, 0, window->width, window->height, 0, 0, 0, BLACKNESS);
        }
        break;

    default:
        return DefWindowProc(hwnd, message, wparam, lparam);
    }

    return 0;
}

static void destroy_window(SysWindow *window) {
    Win32Data *data = (Win32Data*) window->data;

    if (data->bmi) {
        free(data->bmi);
    }

    if (data->hwnd && data->hdc) {
        ReleaseDC(data->hwnd, data->hdc);
        DestroyWindow(data->hwnd);
    }
}

SysWindow* sys_open(const char* title, int width, int height) {
    SysWindow *window = calloc(1, sizeof(SysWindow));
    if (!window) { return NULL; }

    Win32Data *data = calloc(1, sizeof(Win32Data));
    if (!data) { free(window); return NULL; }

    window->data = data;
    window->buffer_width = width;
    window->buffer_height = height;

    int style = WS_OVERLAPPEDWINDOW;

    RECT rect = { 0, 0, width, height };
    AdjustWindowRect(&rect, style, 0);
    rect.right -= rect.left;
    rect.bottom -= rect.top;

    int x = (GetSystemMetrics(SM_CXSCREEN) - rect.right) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - rect.bottom + rect.top) / 2;

    WNDCLASS wc = { 0 };
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndproc;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = title;
    RegisterClass(&wc);

    window->width = rect.right;
    window->height = rect.bottom;

    data->hwnd = CreateWindowEx(0, title, title, style, x, y, window->width, window->height, 0, 0, 0, 0);
    if (!data->hwnd) {
        free(data);
        free(window);
        return NULL;
    }

    SetWindowLongPtr(data->hwnd, GWLP_USERDATA, (LONG_PTR) window);
    ShowWindow(data->hwnd, SW_NORMAL);


    data->bmi = (BITMAPINFO*) calloc(1, sizeof(BITMAPINFOHEADER) + sizeof(RGBQUAD) * 3);
    if (!data->bmi) {
        free(data);
        free(window);
        return NULL;
    }

    data->bmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    data->bmi->bmiHeader.biPlanes = 1;
    data->bmi->bmiHeader.biBitCount = 32;
    data->bmi->bmiHeader.biCompression = BI_BITFIELDS;
    data->bmi->bmiHeader.biWidth = window->buffer_width;
    data->bmi->bmiHeader.biHeight = -(LONG) window->buffer_height;
    data->bmi->bmiColors[0].rgbRed = 0xff;
    data->bmi->bmiColors[1].rgbGreen = 0xff;
    data->bmi->bmiColors[2].rgbBlue = 0xff;

    data->hdc = GetDC(data->hwnd);

    return window;
}

int sys_update(SysWindow* window, void *buffer, int width, int height) {
    if (!window) { return 1; }

    Win32Data *data = (Win32Data*) window->data;

    if (window->close) {
        destroy_window(window);
        return 1;
    }

    if (!buffer) {
        return 1;
    }

    window->buffer = buffer;
    window->buffer_width = width;
    window->buffer_height = height;

    data->bmi->bmiHeader.biWidth = width;
    data->bmi->bmiHeader.biHeight = -(LONG) height;
    InvalidateRect(data->hwnd, NULL, TRUE);
    SendMessage(data->hwnd, WM_PAINT, 0, 0);

    MSG msg;
    while (!window->close && PeekMessage(&msg, data->hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

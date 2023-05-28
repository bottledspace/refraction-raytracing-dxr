#include "RefractionDemo.hpp"
#include <Windows.h>
#include <WindowsX.h>

#define APPNAME TEXT("Refraction")


LRESULT CALLBACK WinProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    switch (Msg) {
        case WM_CLOSE:
            PostQuitMessage(0);
            break;
    }
    return DefWindowProc(hWnd, Msg, wParam, lParam);
}

ATOM InitializeInstance(HINSTANCE hInstance)
{
    WNDCLASSEX wc;
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = 0;
    wc.lpfnWndProc = WinProc;
    wc.cbWndExtra = 0;
    wc.cbClsExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszMenuName = nullptr;
    wc.lpszClassName = APPNAME;
    wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

    return RegisterClassEx(&wc);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lspzCmdLine, int nCmdShow)
{
    ATOM wc = InitializeInstance(hInstance);
    HWND hWnd = CreateWindow(MAKEINTATOM(wc), APPNAME, WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 640, 480,
                             nullptr, nullptr, hInstance, nullptr);

    RefractionDemo app;
    app.initialize(hWnd, 640, 480);

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    for (;;) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);

            if (msg.message == WM_QUIT)
                return (int)msg.wParam;
        }
        app.drawFrame();
    }
}
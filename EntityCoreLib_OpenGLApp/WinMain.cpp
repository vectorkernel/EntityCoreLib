
#pragma warning(disable : 28251)
#pragma warning(disable : 28159)

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define WIN32_EXTRA_LEAN

#include <glad/glad.h>
#undef APIENTRY
#include <windows.h>
#include <cstdio>
#include <iostream>

#include "Application.h"

#include "Platform/Win32Console.h"
#include "Platform/Win32MessagePump.h"
#include "Platform/Win32WglContext.h"
#include "Platform/FrameTimer.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, PSTR, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

#pragma comment(linker, "/subsystem:windows")
#pragma comment(lib, "opengl32.lib")

Application* gApplication = nullptr;
GLuint gVertexArrayObject = 0;
static WglContext gWgl;

// Per-frame render policy for this demo.
// Kept separate from the message pump and update loop so WinMain reads cleanly.
static void RenderOneFrame(HWND hwnd, int& clientWidth, int& clientHeight)
{
    RECT clientRect{};
    GetClientRect(hwnd, &clientRect);
    clientWidth = clientRect.right - clientRect.left;
    clientHeight = clientRect.bottom - clientRect.top;

    glViewport(0, 0, clientWidth, clientHeight);

    // For this basic test, treat it like a 2D line scene.
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);

    glPointSize(5.0f);
    glBindVertexArray(gVertexArrayObject);

    glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    const float aspect = (clientHeight != 0) ? (float)clientWidth / (float)clientHeight : 1.0f;
    gApplication->Render(aspect);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, PSTR /*szCmdLine*/, int /*iCmdShow*/)
{
    // Console for EntityCore logs + your own std::cout prints.
    Win32Console::AttachDebugConsole("EntityCoreLib - Logs");

    gApplication = new Application();

    WNDCLASSEX wndclass{};
    wndclass.cbSize = sizeof(WNDCLASSEX);
    wndclass.style = CS_HREDRAW | CS_VREDRAW;
    wndclass.lpfnWndProc = WndProc;
    wndclass.cbClsExtra = 0;
    wndclass.cbWndExtra = 0;
    wndclass.hInstance = hInstance;
    wndclass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wndclass.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndclass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wndclass.lpszMenuName = 0;
    wndclass.lpszClassName = L"Win32 OpenGL Window";
    RegisterClassEx(&wndclass);

    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    // Fixed client size (no resize).
    int clientWidth = 800;
    int clientHeight = 600;

    RECT windowRect{};
    SetRect(&windowRect,
        (screenWidth / 2) - (clientWidth / 2),
        (screenHeight / 2) - (clientHeight / 2),
        (screenWidth / 2) + (clientWidth / 2),
        (screenHeight / 2) + (clientHeight / 2));

    // Fixed-size: no WS_THICKFRAME and no WS_MAXIMIZEBOX.
    DWORD style = (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX);
    AdjustWindowRectEx(&windowRect, style, FALSE, 0);

    HWND hwnd = CreateWindowEx(
        0,
        wndclass.lpszClassName,
        L"EntityCoreLib OpenGL GUI Test",
        style,
        windowRect.left, windowRect.top,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        NULL, NULL,
        hInstance,
        nullptr);

    // OpenGL initialization (WGL context + GLAD + vsync).
    if (!gWgl.Create(hwnd, 4, 6, true))
        std::cout << "Failed to create OpenGL context\n";

    gWgl.LoadFunctions();
    gWgl.SetVSync(1);

    glGenVertexArrays(1, &gVertexArrayObject);
    glBindVertexArray(gVertexArrayObject);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // Initialize app AFTER GL is ready.
    gApplication->Initialize();

    FrameTimer timer;
    timer.Reset();

    MSG msg{};

    while (true)
    {
        if (!Win32MessagePump::PumpOnce(msg))
            break;

        const float deltaTime = timer.TickSeconds();

        if (gApplication)
            gApplication->Update(deltaTime);

        if (gApplication)
            RenderOneFrame(hwnd, clientWidth, clientHeight);

        if (gApplication)
        {
            gWgl.SwapBuffersNow();
            if (gWgl.swapInterval != 0)
                glFinish();
        }
    }

    // If WM_CLOSE/WM_DESTROY didn't run the shutdown path (e.g., forced quit), be safe.
    if (gApplication)
    {
        std::cout << "Shutting down application\n";
        gApplication->Shutdown();
        delete gApplication;
        gApplication = nullptr;
    }

    return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    switch (iMsg)
    {
    case WM_CLOSE:
        if (gApplication)
        {
            gApplication->Shutdown();
            delete gApplication;
            gApplication = nullptr;
            DestroyWindow(hwnd);
        }
        else
        {
            std::cout << "Already shut down!\n";
        }
        return 0;

    case WM_DESTROY:
        if (gVertexArrayObject != 0)
        {
            glBindVertexArray(0);
            glDeleteVertexArrays(1, &gVertexArrayObject);
            gVertexArrayObject = 0;

            gWgl.Destroy(hwnd);
            PostQuitMessage(0);
        }
        else
        {
            std::cout << "Got multiple destroy messages\n";
        }
        return 0;

    case WM_PAINT:
    case WM_ERASEBKGND:
        return 0;
    }

    return DefWindowProc(hwnd, iMsg, wParam, lParam);
}


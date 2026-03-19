#pragma warning(disable : 28251)
#pragma warning(disable : 28159)

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define WIN32_EXTRA_LEAN

#include <glad/glad.h>
#undef APIENTRY
#include <windows.h>
#include <windowsx.h>
#include <cstdio>
#include <iostream>

#include "Application.h"
#include "CmdWindow.h"
#include "CommandUi.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, PSTR, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

#pragma comment(linker, "/subsystem:windows")
#pragma comment(lib, "opengl32.lib")

#define WGL_CONTEXT_DEBUG_BIT_ARB         0x00000001
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB 0x00000002
#define WGL_CONTEXT_MAJOR_VERSION_ARB     0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB     0x2092
#define WGL_CONTEXT_FLAGS_ARB             0x2094
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB  0x00000001
#define WGL_CONTEXT_PROFILE_MASK_ARB      0x9126
typedef HGLRC(WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC) (HDC, HGLRC, const int*);

typedef const char* (WINAPI* PFNWGLGETEXTENSIONSSTRINGEXTPROC) (void);
typedef BOOL(WINAPI* PFNWGLSWAPINTERVALEXTPROC) (int);
typedef int  (WINAPI* PFNWGLGETSWAPINTERVALEXTPROC) (void);

static void InitConsole()
{
    AllocConsole();

    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
    freopen_s(&f, "CONIN$", "r", stdin);

    std::cout.clear();
    std::cerr.clear();
    std::cin.clear();

    SetConsoleTitleA("EntityCoreLib - Logs");
}

Application* gApplication = nullptr;
GLuint gVertexArrayObject = 0;
static CmdWindow  gCmdWnd;
static CommandUi* gCommandUi = nullptr;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, PSTR /*szCmdLine*/, int /*iCmdShow*/)
{
    // Console for EntityCore logs + your own std::cout prints.
    InitConsole();

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

    const int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    // Fixed client size (no resize).
    int clientWidth  = 800;
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

    HDC hdc = GetDC(hwnd);

    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 32;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pixelFormat = ChoosePixelFormat(hdc, &pfd);
    SetPixelFormat(hdc, pixelFormat, &pfd);

    // Temp context to load WGL extensions for creating core context.
    HGLRC tempRC = wglCreateContext(hdc);
    wglMakeCurrent(hdc, tempRC);

    auto wglCreateContextAttribsARB =
        (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");

    const int attribList[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
        WGL_CONTEXT_MINOR_VERSION_ARB, 6,

        // Debug is nice while building this up; forward-compatible is optional.
        WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_DEBUG_BIT_ARB,
        // WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,

        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0,
    };

    HGLRC hglrc = wglCreateContextAttribsARB ? wglCreateContextAttribsARB(hdc, 0, attribList) : nullptr;

    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(tempRC);

    if (!hglrc)
    {
        std::cout << "Failed to create OpenGL 3.3 core context. Falling back to legacy context.\n";
        hglrc = wglCreateContext(hdc);
    }

    wglMakeCurrent(hdc, hglrc);

    if (!gladLoadGL())
    {
        std::cout << "Could not initialize GLAD\n";
    }
    else
    {
        std::cout << "Vendor   : " << (const char*)glGetString(GL_VENDOR) << "\n";
        std::cout << "Renderer : " << (const char*)glGetString(GL_RENDERER) << "\n";
        std::cout << "GLSL     : " << (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION) << "\n";

        GLint profileMask = 0, flags = 0;
        glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profileMask);
        glGetIntegerv(GL_CONTEXT_FLAGS, &flags);

        std::cout << "Profile  : " << ((profileMask & 1 /*GL_CONTEXT_CORE_PROFILE_BIT*/) ? "Core" : "Compat") << "\n";
        std::cout << "Flags    : " << flags << "\n";
    }

    // VSync if supported
    int vsynch = 0;
    auto _wglGetExtensionsStringEXT =
        (PFNWGLGETEXTENSIONSSTRINGEXTPROC)wglGetProcAddress("wglGetExtensionsStringEXT");

    bool swapControlSupported = false;
    if (_wglGetExtensionsStringEXT)
        swapControlSupported = strstr(_wglGetExtensionsStringEXT(), "WGL_EXT_swap_control") != 0;

    if (swapControlSupported)
    {
        auto wglSwapIntervalEXT =
            (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
        auto wglGetSwapIntervalEXT =
            (PFNWGLGETSWAPINTERVALEXTPROC)wglGetProcAddress("wglGetSwapIntervalEXT");

        if (wglSwapIntervalEXT && wglGetSwapIntervalEXT && wglSwapIntervalEXT(1))
        {
            std::cout << "Enabled vsync\n";
            vsynch = wglGetSwapIntervalEXT();
        }
        else
        {
            std::cout << "Could not enable vsync\n";
        }
    }
    else
    {
        std::cout << "WGL_EXT_swap_control not supported\n";
    }

    glGenVertexArrays(1, &gVertexArrayObject);
    glBindVertexArray(gVertexArrayObject);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // Initialize app AFTER GL is ready.
    gApplication->Initialize();

    // -------------------------------------------------
    // Command manager window (modeless RichEdit)
    // -------------------------------------------------
    // Keep WinMain clean: bind CmdWindow to a small host/controller.
    gCommandUi = new CommandUi(gApplication);
    gCommandUi->BindWindow(&gCmdWnd);
    gCommandUi->Initialize();

    // Create as an owned tool window so it stays above the main viewport.
    gCmdWnd.CreateWithHost(hInstance, hwnd, gCommandUi, CW_USEDEFAULT, CW_USEDEFAULT, 740, 240);

    gCmdWnd.FocusInput();

    DWORD lastTick = GetTickCount();
    MSG msg{};

    while (true)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                break;

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        DWORD thisTick = GetTickCount();
        float deltaTime = float(thisTick - lastTick) * 0.001f;
        lastTick = thisTick;

        if (gApplication)
            gApplication->Update(deltaTime);

        // If the app is idle again (e.g. Esc pressed in the viewport), ensure we show the base prompt.
        gCmdWnd.ShowBasePromptIfIdle();

        if (gApplication)
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

            float aspect = (float)clientWidth / (float)clientHeight;
            gApplication->Render(aspect);
        }

        if (gApplication)
        {
            SwapBuffers(hdc);
            if (vsynch != 0)
                glFinish();
        }
    }

    if (gApplication)
    {
        std::cout << "Shutting down application\n";

        if (gCommandUi)
        {
            delete gCommandUi;
            gCommandUi = nullptr;
        }

        gCmdWnd.Destroy();

        delete gApplication;
        gApplication = nullptr;
    }

    return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    switch (iMsg)
    {
    case WM_KEYDOWN:
    {
        if (!gApplication) return 0;

        bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        gApplication->OnKeyDown(wParam, ctrlDown);

        // Keep your ortho test keys working too
        switch (wParam)
        {
        case '1': gApplication->m_pz.mode = PanZoomController::OrthoMode::Center;     break;
        case '2': gApplication->m_pz.mode = PanZoomController::OrthoMode::BottomLeft; break;
        case '3': gApplication->m_pz.mode = PanZoomController::OrthoMode::TopLeft;    break;
        default: break;
        }
        return 0;
    }

    case WM_CLOSE:
        if (gApplication)
        {
            gApplication->Shutdown();
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
            HDC hdc = GetDC(hwnd);
            HGLRC hglrc = wglGetCurrentContext();

            glBindVertexArray(0);
            glDeleteVertexArrays(1, &gVertexArrayObject);
            gVertexArrayObject = 0;

            wglMakeCurrent(NULL, NULL);
            if (hglrc) wglDeleteContext(hglrc);
            ReleaseDC(hwnd, hdc);

            PostQuitMessage(0);
        }
        else
        {
            std::cout << "Got multiple destroy messages\n";
        }
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }

    // --- RIGHT button = pan ---
    case WM_LBUTTONDOWN:
    {
        if (gApplication)
        {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            gApplication->OnLeftButtonDownClient(x, y);
        }
        return 0;
    }

    case WM_RBUTTONUP:
    {
        if (gApplication)
        {
            gApplication->m_pz.OnMouseUp();
            ReleaseCapture();
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (gApplication)
        {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            // Always update crosshair cursor position
            gApplication->OnMouseMoveClient(x, y);

            // Pan while RMB is held (or we own capture)
            if ((wParam & MK_RBUTTON) != 0 || GetCapture() == hwnd)
            {
                gApplication->m_pz.OnMouseMove(x, y);
            }
        }
        return 0;
    }

    // Optional: prevent the standard context menu on RMB release
    case WM_CONTEXTMENU:
        return 0;

    case WM_LBUTTONUP:
    {
        if (gApplication)
        {
            gApplication->m_pz.OnMouseUp();
            ReleaseCapture();
        }
        return 0;
    }



    case WM_MOUSEWHEEL:
    {
        if (gApplication)
        {
            // WM_MOUSEWHEEL provides screen coordinates in lParam
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);

            int wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            gApplication->m_pz.OnMouseWheel((int)pt.x, (int)pt.y, wheelDelta);
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return 0;
    }

    return DefWindowProc(hwnd, iMsg, wParam, lParam);
}

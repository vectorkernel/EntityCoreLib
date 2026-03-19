#pragma once

#ifdef _WIN32

#include <windows.h>

// Win32/WGL OpenGL context wrapper.
//
// Responsibilities:
//   - Sets a basic pixel format on the HWND's HDC.
//   - Creates a modern core-profile OpenGL context using
//     wglCreateContextAttribsARB (if available).
//   - Falls back to legacy context creation if core-profile creation fails.
//   - Loads OpenGL entry points (GLAD) once a context is current.
//   - Optionally enables vsync using WGL_EXT_swap_control.
//
// Intended use:
//   WglContext gl;
//   gl.Create(hwnd, 4, 6, true);
//   gl.LoadFunctions();
//   gl.SetVSync(1);

struct WglContext
{
    HDC   hdc = nullptr;
    HGLRC rc  = nullptr;
    int   swapInterval = 0; // 0 means "unknown/disabled".

    bool Create(HWND hwnd, int major, int minor, bool debugContext);
    bool MakeCurrent();
    bool LoadFunctions();
    bool SetVSync(int interval);
    void SwapBuffersNow() const;
    void Destroy(HWND hwnd);
};

#endif // _WIN32

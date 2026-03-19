#ifdef _WIN32

#include "Win32WglContext.h"

#include <glad/glad.h>
#undef APIENTRY

#include <cstring>
#include <iostream>

// WGL extension tokens
#define WGL_CONTEXT_DEBUG_BIT_ARB              0x00000001
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB 0x00000002
#define WGL_CONTEXT_MAJOR_VERSION_ARB          0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB          0x2092
#define WGL_CONTEXT_FLAGS_ARB                  0x2094
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB       0x00000001
#define WGL_CONTEXT_PROFILE_MASK_ARB           0x9126

typedef HGLRC(WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);
typedef const char* (WINAPI* PFNWGLGETEXTENSIONSSTRINGEXTPROC)(void);
typedef BOOL(WINAPI* PFNWGLSWAPINTERVALEXTPROC)(int);
typedef int  (WINAPI* PFNWGLGETSWAPINTERVALEXTPROC)(void);

static bool SetBasicPixelFormat(HDC hdc)
{
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 32;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    const int pixelFormat = ChoosePixelFormat(hdc, &pfd);
    if (pixelFormat == 0)
        return false;

    if (!SetPixelFormat(hdc, pixelFormat, &pfd))
        return false;

    return true;
}

bool WglContext::Create(HWND hwnd, int major, int minor, bool debugContext)
{
    hdc = GetDC(hwnd);
    if (!hdc)
        return false;

    if (!SetBasicPixelFormat(hdc))
        return false;

    // Temporary legacy context so we can fetch wglCreateContextAttribsARB.
    HGLRC tempRC = wglCreateContext(hdc);
    if (!tempRC)
        return false;

    if (!wglMakeCurrent(hdc, tempRC))
    {
        wglDeleteContext(tempRC);
        return false;
    }

    auto wglCreateContextAttribsARB =
        (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");

    HGLRC created = nullptr;
    if (wglCreateContextAttribsARB)
    {
        const int flags = debugContext ? WGL_CONTEXT_DEBUG_BIT_ARB : 0;

        const int attribList[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, major,
            WGL_CONTEXT_MINOR_VERSION_ARB, minor,
            WGL_CONTEXT_FLAGS_ARB, flags,
            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
            0,
        };

        created = wglCreateContextAttribsARB(hdc, 0, attribList);
    }

    // Tear down temporary context.
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(tempRC);

    if (!created)
    {
        std::cout << "Failed to create OpenGL core context. Falling back to legacy context.\n";
        created = wglCreateContext(hdc);
    }

    rc = created;
    if (!rc)
        return false;

    return MakeCurrent();
}

bool WglContext::MakeCurrent()
{
    if (!hdc || !rc)
        return false;
    return wglMakeCurrent(hdc, rc) == TRUE;
}

bool WglContext::LoadFunctions()
{
    if (!MakeCurrent())
        return false;

    if (!gladLoadGL())
    {
        std::cout << "Could not initialize GLAD\n";
        return false;
    }

    std::cout << "Vendor   : " << (const char*)glGetString(GL_VENDOR) << "\n";
    std::cout << "Renderer : " << (const char*)glGetString(GL_RENDERER) << "\n";
    std::cout << "GLSL     : " << (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION) << "\n";

    GLint profileMask = 0, flags = 0;
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profileMask);
    glGetIntegerv(GL_CONTEXT_FLAGS, &flags);

    std::cout << "Profile  : " << ((profileMask & 1 /*GL_CONTEXT_CORE_PROFILE_BIT*/) ? "Core" : "Compat") << "\n";
    std::cout << "Flags    : " << flags << "\n";

    return true;
}

bool WglContext::SetVSync(int interval)
{
    swapInterval = 0;

    auto wglGetExtensionsStringEXT =
        (PFNWGLGETEXTENSIONSSTRINGEXTPROC)wglGetProcAddress("wglGetExtensionsStringEXT");

    bool swapControlSupported = false;
    if (wglGetExtensionsStringEXT)
    {
        const char* ext = wglGetExtensionsStringEXT();
        if (ext)
            swapControlSupported = (std::strstr(ext, "WGL_EXT_swap_control") != nullptr);
    }

    if (!swapControlSupported)
    {
        std::cout << "WGL_EXT_swap_control not supported\n";
        return false;
    }

    auto wglSwapIntervalEXT =
        (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
    auto wglGetSwapIntervalEXT =
        (PFNWGLGETSWAPINTERVALEXTPROC)wglGetProcAddress("wglGetSwapIntervalEXT");

    if (wglSwapIntervalEXT && wglGetSwapIntervalEXT && wglSwapIntervalEXT(interval))
    {
        swapInterval = wglGetSwapIntervalEXT();
        std::cout << "Enabled vsync\n";
        return true;
    }

    std::cout << "Could not enable vsync\n";
    return false;
}

void WglContext::SwapBuffersNow() const
{
    if (hdc)
        ::SwapBuffers(hdc);
}

void WglContext::Destroy(HWND hwnd)
{
    if (rc)
    {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(rc);
        rc = nullptr;
    }

    if (hdc)
    {
        ReleaseDC(hwnd, hdc);
        hdc = nullptr;
    }

    swapInterval = 0;
}

#endif // _WIN32

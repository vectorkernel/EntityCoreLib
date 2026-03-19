#ifdef _WIN32

#include "Win32Console.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <iostream>

namespace Win32Console
{
    void AttachDebugConsole(const char* title)
    {
        AllocConsole();

        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        freopen_s(&f, "CONIN$", "r", stdin);

        std::cout.clear();
        std::cerr.clear();
        std::cin.clear();

        if (title && title[0])
            SetConsoleTitleA(title);
    }
}

#endif // _WIN32

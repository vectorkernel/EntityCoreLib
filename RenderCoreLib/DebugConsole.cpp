// DebugConsole.cpp
#define WIN32_LEAN_AND_MEAN
#include "pch.h"
#include <windows.h>
#include <cstdio>
#include <iostream>

#include "DebugConsole.h"

namespace DebugConsole
{
    void Attach()
    {
#if _DEBUG
        static bool s_attached = false;
        if (s_attached)
            return;
        s_attached = true;

        // If we were launched from a console, attach to it. Otherwise create one.
        if (!AttachConsole(ATTACH_PARENT_PROCESS))
        {
            AllocConsole();
        }

        // Route stdout/stderr/stdin
        FILE* fpOut = nullptr;
        FILE* fpErr = nullptr;
        FILE* fpIn = nullptr;

        freopen_s(&fpOut, "CONOUT$", "w", stdout);
        freopen_s(&fpErr, "CONOUT$", "w", stderr);
        freopen_s(&fpIn, "CONIN$", "r", stdin);

        std::ios::sync_with_stdio(true);

        SetConsoleTitleA("VDraw Debug Console");
        std::cout << "[DebugConsole] attached.\n";
#endif
    }
}

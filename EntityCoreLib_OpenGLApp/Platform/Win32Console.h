#pragma once

#ifdef _WIN32

// Win32 console helper for GUI-subsystem apps.
//
// Purpose:
//   - Allocates a console window for the process.
//   - Redirects stdin/stdout/stderr to the console so std::cout works.
//
// Rationale:
//   Keeping this out of WinMain.cpp prevents large blocks of boilerplate
//   from obscuring application startup logic.

namespace Win32Console
{
    // Allocates a console and attaches CRT stdio streams to it.
    // If title is non-null/non-empty, sets the console title.
    void AttachDebugConsole(const char* title);
}

#endif // _WIN32

// DebugConsole.h
#pragma once

namespace DebugConsole
{
    // Creates/attaches a Win32 console so std::cout / printf work.
    // Safe to call multiple times.
    void Attach();
}

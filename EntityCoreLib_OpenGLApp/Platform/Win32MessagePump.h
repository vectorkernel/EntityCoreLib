#pragma once

#ifdef _WIN32

#include <windows.h>

namespace Win32MessagePump
{
    // Pumps queued messages (non-blocking).
    // Returns false if WM_QUIT was received.
    bool PumpOnce(MSG& msg);
}

#endif // _WIN32

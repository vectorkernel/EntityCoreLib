#ifdef _WIN32

#include "Win32MessagePump.h"

namespace Win32MessagePump
{
    bool PumpOnce(MSG& msg)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                return false;

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        return true;
    }
}

#endif // _WIN32

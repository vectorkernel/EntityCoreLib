#pragma once

#ifdef _WIN32

#include <windows.h>

// Simple per-frame timer based on GetTickCount().
//
// Notes:
//   - Uses millisecond resolution; good enough for a demo app.
//   - If you later want higher precision, swap to QueryPerformanceCounter.
struct FrameTimer
{
    DWORD lastTick = 0;

    void Reset() { lastTick = GetTickCount(); }

    float TickSeconds()
    {
        const DWORD thisTick = GetTickCount();
        const float dt = float(thisTick - lastTick) * 0.001f;
        lastTick = thisTick;
        return dt;
    }
};

#endif // _WIN32

// Logger.h
#pragma once

#include <cstdint>
#include <cstdarg>
#include <string>

struct Entity;
struct ILogSink; // forward declaration (defined in LogSink.h)
namespace VKLog
{
    enum class Level : uint8_t
    {
        Error = 0,
        Warn = 1,
        Info = 2,
        Debug = 3,
        Trace = 4,
    };

    // Bitmask categories. Keep this compact; we can add more later.
    enum Category : uint32_t
    {
        Core = 1u << 0,
        Command = 1u << 1,
        EntityBook = 1u << 2,
        Selection = 1u << 3,
        Erase = 1u << 4,
        Pick = 1u << 5,
        Page = 1u << 6,
        Viewport = 1u << 7,
    };

    // Global config ---------------------------------------------------------
    void Init(); // safe to call multiple times

    // Optional UI sink (CmdWindow, etc.). Non-owning; caller manages lifetime.
    void SetSink(ILogSink* sink);
    ILogSink* GetSink();

    // High-frequency event gating ------------------------------------------
    // Mouse-move logging can easily flood output and hurt performance.
    // Keep it OFF by default; enable explicitly when needed.
    void SetMouseMoveLoggingEnabled(bool enabled);
    bool IsMouseMoveLoggingEnabled();

    // EntityBook / entity churn logging (very verbose; OFF by default).
    void SetEntityVerboseLoggingEnabled(bool enabled);
    bool IsEntityVerboseLoggingEnabled();

// Returns true if category is enabled AND level passes threshold.
    bool Enabled(Category cat, Level lvl);

    // Enable/disable categories.
    void Enable(Category cat, bool on);
    void EnableMask(uint32_t mask, bool on);
    uint32_t EnabledMask();

    // Per-category min-level threshold.
    void SetLevel(Category cat, Level lvl);
    Level GetLevel(Category cat);

    // Presets
    void ApplyPresetDefault();
    void ApplyPresetQuiet();
    void ApplyPresetVerbose();

    // Logging ---------------------------------------------------------------
    void VLogf(Category cat, Level lvl, const char* fmt, va_list args);
    void Logf(Category cat, Level lvl, const char* fmt, ...);

    // Convenience: same as Logf(), but additionally gated by the mouse-move flag.
    void LogMouseMovef(Category cat, Level lvl, const char* fmt, ...);

// Convenience: same as Logf()/LogEntity(), but additionally gated by the entity-verbose flag.
    void LogEntityVerbosef(Category cat, Level lvl, const char* fmt, ...);
    void LogEntityVerbose(Category cat, Level lvl, const char* prefix, const Entity& e);

    // Helpers for frequently-logged structures.
    void LogEntity(Category cat, Level lvl, const char* prefix, const Entity& e);

    // Command-window helper: prints current settings + syntax help.
    void PrintHelp(ILogSink* sink);
    void PrintState(ILogSink* sink);

    // Parsing helpers used by Application/main.
    // Returns true if the line was recognized as a LOG command (even if invalid).
    bool HandleLogCommandLine(const std::string& line, ILogSink* sink);
}

// Convenience macros (compile-time gated in non-debug builds if desired)
#define VK_LOG(cat, lvl, fmt, ...) ::VKLog::Logf((cat), (lvl), (fmt), ##__VA_ARGS__)

// Use for WM_MOUSEMOVE / cursor tracking logs.
// Example:
//   VK_LOG_MOUSEMOVE(VKLog::Viewport, VKLog::Level::Trace, "Mouse (%d,%d)", x, y);
#define VK_LOG_MOUSEMOVE(cat, lvl, fmt, ...) ::VKLog::LogMouseMovef((cat), (lvl), (fmt), ##__VA_ARGS__)
#define VK_LOG_ENT_VERBOSE(cat, lvl, fmt, ...) ::VKLog::LogEntityVerbosef((cat), (lvl), (fmt), ##__VA_ARGS__)

#pragma once
#include <cstdint>
#include <string>
#include <string_view>

struct Entity; // forward

namespace EntityCore
{
    enum class LogLevel : uint8_t
    {
        Trace,
        Info,
        Warn,
        Error
    };

    // Signature for host-provided log sink.
    // - category: short subsystem name (e.g. "EntityBook")
    // - event: short event name (e.g. "AddEntity")
    // - message: human-readable details (may be empty)
    using LogSink = void(*)(LogLevel level, const char* category, const char* event, std::string_view message);

    // Install a log sink (thread-safe). If unset (nullptr), logging is a no-op.
    void SetLogSink(LogSink sink) noexcept;
    LogSink GetLogSink() noexcept;

    // Convenience helper: format a small summary for an Entity.
    // NOTE: Only call when you actually plan to log (i.e. when GetLogSink() != nullptr).
    std::string DescribeEntity(const Entity& e);

    // Internal helper used by the library.
    inline void EmitLog(LogLevel level, const char* category, const char* event, std::string_view message)
    {
        if (auto* sink = GetLogSink())
            sink(level, category, event, message);
    }
} // namespace EntityCore

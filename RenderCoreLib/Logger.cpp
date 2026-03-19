#include "pch.h"
// Logger.cpp
#include "Logger.h"
#include "LogSink.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>
#include <algorithm>
#include <type_traits>

// Global, optional sink for routing log lines to UI (CmdWindow, etc.).
static std::atomic<ILogSink*> g_sink{ nullptr };

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#include "Entity.h"


// Returns bit index (0-31) from a single-bit mask.
// If mask has multiple bits or is zero, returns -1.
static int BitIndexFromMask(uint32_t mask)
{
    if (mask == 0)
        return -1;

    // Ensure only one bit is set
    if ((mask & (mask - 1)) != 0)
        return -1;

#if defined(_MSC_VER)
    unsigned long index;
    _BitScanForward(&index, mask);
    return static_cast<int>(index);
#else
    for (int i = 0; i < 32; ++i)
        if (mask & (1u << i))
            return i;
    return -1;
#endif
}

namespace
{
    // NOTE: Kept intentionally simple (console-first). If you later want file logging,
    // this module is the single choke point.
    std::once_flag g_initOnce;

    std::atomic<uint32_t> g_enabledMask{ 0 };
    // Per-category min level (indexed by bit position).
    // Default values set by presets.
    std::atomic<uint8_t> g_levelByBit[32];

    // High-frequency input gating (OFF by default)
    std::atomic<bool> g_mouseMoveLoggingEnabled{ false };
    std::atomic<bool> g_entityVerboseLoggingEnabled{ false }; // default OFF

    // NOTE: Avoid calling any VKLog::* API (which calls Init()) from inside Init()'s call_once
    // lambda. Doing so re-enters std::call_once and deadlocks. These helpers are safe to call
    // during Init() because they touch only the raw atomics.
    static inline void SetLevel_NoInit(uint32_t catBit, VKLog::Level lvl)
    {
        const int idx = BitIndexFromMask(catBit);
        if (idx < 0) return;
        g_levelByBit[idx].store((uint8_t)lvl, std::memory_order_relaxed);
    }

    static inline int BitIndexFromSingleBit(uint32_t catBit)
    {
        return BitIndexFromMask(catBit);
    }

    static const char* LevelToStr(VKLog::Level lvl)
    {
        switch (lvl)
        {
        case VKLog::Level::Error: return "ERR";
        case VKLog::Level::Warn:  return "WRN";
        case VKLog::Level::Info:  return "INF";
        case VKLog::Level::Debug: return "DBG";
        case VKLog::Level::Trace: return "TRC";
        default: return "UNK";
        }
    }

    static const char* CatToStr(uint32_t cat)
    {
        using namespace VKLog;
        switch ((VKLog::Category)cat)
        {
        case Core:       return "CORE";
        case Command:    return "CMD";
        case EntityBook: return "ENTS";
        case Selection:  return "SEL";
        case Erase:      return "ERASE";
        case Pick:       return "PICK";
        case Page:       return "PAGE";
        case Viewport:   return "VP";
        default:         return "CAT";
        }
    }

    static VKLog::Level ParseLevel(const std::string& s, bool* ok)
    {
        auto u = s;
        std::transform(u.begin(), u.end(), u.begin(), [](unsigned char c) { return (char)std::toupper(c); });
        if (u == "0" || u == "ERR" || u == "ERROR") { if (ok) *ok = true; return VKLog::Level::Error; }
        if (u == "1" || u == "WRN" || u == "WARN" || u == "WARNING") { if (ok) *ok = true; return VKLog::Level::Warn; }
        if (u == "2" || u == "INF" || u == "INFO") { if (ok) *ok = true; return VKLog::Level::Info; }
        if (u == "3" || u == "DBG" || u == "DEBUG") { if (ok) *ok = true; return VKLog::Level::Debug; }
        if (u == "4" || u == "TRC" || u == "TRACE") { if (ok) *ok = true; return VKLog::Level::Trace; }
        if (ok) *ok = false;
        return VKLog::Level::Info;
    }

    static uint32_t ParseCategoryMask(const std::string& s)
    {
        // Allows: ALL, CORE, CMD, ENTS, SEL, ERASE, PICK, PAGE, VP
        auto u = s;
        std::transform(u.begin(), u.end(), u.begin(), [](unsigned char c) { return (char)std::toupper(c); });
        if (u == "ALL" || u == "*")
            return 0xFFFFFFFFu;

        uint32_t m = 0;
        auto add = [&](uint32_t bit)
            {
                if (bit != 0) m |= bit;
            };
        if (u == "CORE") add(VKLog::Core);
        else if (u == "CMD" || u == "COMMAND") add(VKLog::Command);
        else if (u == "ENTS" || u == "ENTITY" || u == "ENTITYBOOK") add(VKLog::EntityBook);
        else if (u == "SEL" || u == "SELECTION") add(VKLog::Selection);
        else if (u == "ERASE") add(VKLog::Erase);
        else if (u == "PICK") add(VKLog::Pick);
        else if (u == "PAGE") add(VKLog::Page);
        else if (u == "VP" || u == "VIEWPORT") add(VKLog::Viewport);
        return m;
    }

    static std::vector<std::string> Tokenize(const std::string& line)
    {
        std::vector<std::string> out;
        std::string cur;
        for (char c : line)
        {
            if (std::isspace((unsigned char)c))
            {
                if (!cur.empty()) { out.push_back(cur); cur.clear(); }
                continue;
            }
            cur.push_back(c);
        }
        if (!cur.empty()) out.push_back(cur);
        return out;
    }

    static void SinkPrint(ILogSink* sink, const char* s)
    {
        if (!sink || !s) return;
        sink->AddLine(std::string_view(s));
    }
}

namespace VKLog
{
    void Init()
    {
        std::call_once(g_initOnce, []()
            {
                // Default preset is usable in debug; in release it will still print
                // only when explicitly enabled.
                ApplyPresetDefault();
            });
    }

    void SetSink(ILogSink* sink) { g_sink.store(sink, std::memory_order_release); }
    ILogSink* GetSink() { return g_sink.load(std::memory_order_acquire); }

    void SetMouseMoveLoggingEnabled(bool enabled)
    {
        Init();
        g_mouseMoveLoggingEnabled.store(enabled, std::memory_order_relaxed);
    }

    bool IsMouseMoveLoggingEnabled()
    {
        Init();
        return g_mouseMoveLoggingEnabled.load(std::memory_order_relaxed);
    }


    void SetEntityVerboseLoggingEnabled(bool enabled)
    {
        Init();
        g_entityVerboseLoggingEnabled.store(enabled, std::memory_order_relaxed);
    }

    bool IsEntityVerboseLoggingEnabled()
    {
        Init();
        return g_entityVerboseLoggingEnabled.load(std::memory_order_relaxed);
    }

    void LogEntityVerbosef(Category cat, Level lvl, const char* fmt, ...)
    {
        if (!IsEntityVerboseLoggingEnabled() || !fmt)
            return;

        va_list args;
        va_start(args, fmt);
        VLogf(cat, lvl, fmt, args);
        va_end(args);
    }

    void LogEntityVerbose(Category cat, Level lvl, const char* prefix, const Entity& e)
    {
        if (!IsEntityVerboseLoggingEnabled())
            return;

        LogEntity(cat, lvl, prefix, e);
    }

    bool Enabled(Category cat, Level lvl)
    {
        Init();
        const uint32_t bit = (uint32_t)cat;
        if ((g_enabledMask.load(std::memory_order_relaxed) & bit) == 0)
            return false;
        const uint32_t idx = BitIndexFromMask(bit);
        const uint8_t minLvl = g_levelByBit[idx].load(std::memory_order_relaxed);
        return (uint8_t)lvl >= minLvl;
    }

    void Enable(Category cat, bool on)
    {
        Init();
        EnableMask((uint32_t)cat, on);
    }

    void EnableMask(uint32_t mask, bool on)
    {
        Init();
        if (on)
            g_enabledMask.fetch_or(mask, std::memory_order_relaxed);
        else
            g_enabledMask.fetch_and(~mask, std::memory_order_relaxed);
    }

    uint32_t EnabledMask()
    {
        Init();
        return g_enabledMask.load(std::memory_order_relaxed);
    }

    void SetLevel(Category cat, Level lvl)
    {
        Init();
        const uint32_t idx = BitIndexFromMask((uint32_t)cat);
        g_levelByBit[idx].store((uint8_t)lvl, std::memory_order_relaxed);
    }

    Level GetLevel(Category cat)
    {
        Init();
        const uint32_t idx = BitIndexFromMask((uint32_t)cat);
        return (Level)g_levelByBit[idx].load(std::memory_order_relaxed);
    }

    void ApplyPresetDefault()
    {
        // Default: console-centric but not noisy.
        g_enabledMask.store(
            (uint32_t)(Core | Command | Erase | EntityBook),
            std::memory_order_relaxed);

        for (auto& a : g_levelByBit) a.store((uint8_t)Level::Warn, std::memory_order_relaxed);

        // IMPORTANT: Do not call SetLevel() here. ApplyPresetDefault() is called from Init()'s
        // call_once, and SetLevel() calls Init() -> deadlock.
        SetLevel_NoInit((uint32_t)Core, Level::Info);
        SetLevel_NoInit((uint32_t)Command, Level::Info);
        SetLevel_NoInit((uint32_t)Erase, Level::Info);
        SetLevel_NoInit((uint32_t)EntityBook, Level::Info); // entity details are TRACE; keep default sane

        // Off by default (can be enabled on demand)
        SetLevel_NoInit((uint32_t)Selection, Level::Debug);
        SetLevel_NoInit((uint32_t)Pick, Level::Debug);
        SetLevel_NoInit((uint32_t)Page, Level::Info);
        SetLevel_NoInit((uint32_t)Viewport, Level::Info);
    }

    void ApplyPresetQuiet()
    {
        g_enabledMask.store((uint32_t)Core, std::memory_order_relaxed);
        for (auto& a : g_levelByBit) a.store((uint8_t)Level::Error, std::memory_order_relaxed);
        SetLevel_NoInit((uint32_t)Core, Level::Warn);
    }

    void ApplyPresetVerbose()
    {
        g_enabledMask.store(0xFFFFFFFFu, std::memory_order_relaxed);
        for (auto& a : g_levelByBit) a.store((uint8_t)Level::Trace, std::memory_order_relaxed);
    }

    void VLogf(Category cat, Level lvl, const char* fmt, va_list args)
    {
        if (!Enabled(cat, lvl) || !fmt)
            return;

        // Format to a string so we can route to an optional sink as well as stdout.
        char prefix[64];
        std::snprintf(prefix, sizeof(prefix), "[%s][%s] ", LevelToStr(lvl), CatToStr((uint32_t)cat));

        // Copy args to compute size safely.
        va_list argsCopy;
        va_copy(argsCopy, args);
        const int needed = std::vsnprintf(nullptr, 0, fmt, argsCopy);
        va_end(argsCopy);

        std::string line;
        if (needed > 0)
        {
            line.resize((size_t)needed);
            std::vsnprintf(line.data(), line.size() + 1, fmt, args);
        }

        std::string out = std::string(prefix) + line;

        // Stdout (debugging / console)
        ::printf("%s\n", out.c_str());
        ::fflush(stdout);

        // Optional sink (e.g. command window)
        if (auto* sink = GetSink())
        {
            // CmdWindow expects CRLF; keep it consistent with the rest of the command UI.
            std::string withCrlf = out + "\r\n";
            sink->AddLine(withCrlf);
        }
    }


    void Logf(Category cat, Level lvl, const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        VLogf(cat, lvl, fmt, args);
        va_end(args);
    }

    void LogMouseMovef(Category cat, Level lvl, const char* fmt, ...)
    {
        // Mouse-move logs are opt-in to avoid flooding output.
        if (!IsMouseMoveLoggingEnabled() || !fmt)
            return;

        va_list args;
        va_start(args, fmt);
        VLogf(cat, lvl, fmt, args);
        va_end(args);
    }

    void LogEntity(Category cat, Level lvl, const char* prefix, const Entity& e)
    {
        if (!Enabled(cat, lvl))
            return;

        const char* typeStr = (e.type == EntityType::Line) ? "Line" : (e.type == EntityType::Text ? "Text" : "SolidRect");
        const char* tagStr = "?";
        switch (e.tag)
        {
        case EntityTag::Grid:      tagStr = "Grid"; break;
        case EntityTag::Scene:     tagStr = "Scene"; break;
        case EntityTag::User:      tagStr = "User"; break;
        case EntityTag::Cursor:    tagStr = "Cursor"; break;
        case EntityTag::Hud:       tagStr = "Hud"; break;
        case EntityTag::Paper:     tagStr = "Paper"; break;
        case EntityTag::PaperUser: tagStr = "PaperUser"; break;
        }

        if (e.type == EntityType::Line)
        {
            Logf(cat, lvl,
                "%s id=%zu type=%s tag=%s layer=%u drawOrder=%d screen=%d a=(%.3f,%.3f,%.3f) b=(%.3f,%.3f,%.3f) thick=%.3f",
                (prefix ? prefix : "Entity"),
                e.ID, typeStr, tagStr, (unsigned)e.layerId, e.drawOrder, e.screenSpace ? 1 : 0,
                e.line.start.x, e.line.start.y, e.line.start.z,
                e.line.end.x, e.line.end.y, e.line.end.z,
                e.line.thickness);
        }
        else
        {
            Logf(cat, lvl,
                "%s id=%zu type=%s tag=%s layer=%u drawOrder=%d screen=%d",
                (prefix ? prefix : "Entity"),
                e.ID, typeStr, tagStr, (unsigned)e.layerId, e.drawOrder, e.screenSpace ? 1 : 0);
        }
    }

    void PrintHelp(ILogSink* sink)
    {
        SinkPrint(sink,
            "LOG command:\r\n"
            "  LOG                         -> show current log state\r\n"
            "  LOG HELP                    -> show this help\r\n"
            "  LOG PRESET DEFAULT|QUIET|VERBOSE\r\n"
            "  LOG ON  <CAT>|ALL           -> enable category\r\n"
            "  LOG OFF <CAT>|ALL           -> disable category\r\n"
            "  LOG LEVEL <CAT> <LEVEL>     -> set min level for category\r\n"
            "\r\n"
            "Categories: CORE, CMD, ENTS, SEL, ERASE, PICK, PAGE, VP\r\n"
            "Levels: ERROR, WARN, INFO, DEBUG, TRACE (or 0..4)\r\n");
    }

    void PrintState(ILogSink* sink)
    {
        std::ostringstream oss;
        const uint32_t m = EnabledMask();
        oss << "[Log] enabledMask=0x" << std::hex << m << std::dec << "\r\n";
        oss << "  MOUSEMOVE: " << (IsMouseMoveLoggingEnabled() ? "ON" : "OFF") << "\r\n";
        auto line = [&](uint32_t bit, const char* name)
            {
                const bool on = (m & bit) != 0;
                const auto lvl = GetLevel((Category)bit);
                oss << "  " << name << ": " << (on ? "ON " : "OFF") << "  level=" << LevelToStr(lvl) << "\r\n";
            };
        line(Core, "CORE");
        line(Command, "CMD");
        line(EntityBook, "ENTS");
        line(Selection, "SEL");
        line(Erase, "ERASE");
        line(Pick, "PICK");
        line(Page, "PAGE");
        line(Viewport, "VP");

        SinkPrint(sink, oss.str().c_str());
    }

    bool HandleLogCommandLine(const std::string& line, ILogSink* sink)
    {
        Init();
        auto toks = Tokenize(line);
        if (toks.empty())
            return false;

        auto u0 = toks[0];
        std::transform(u0.begin(), u0.end(), u0.begin(), [](unsigned char c) { return (char)std::toupper(c); });
        if (u0 != "LOG")
            return false;

        if (toks.size() == 1)
        {
            PrintState(sink);
            return true;
        }

        auto u1 = toks[1];
        std::transform(u1.begin(), u1.end(), u1.begin(), [](unsigned char c) { return (char)std::toupper(c); });

        if (u1 == "HELP" || u1 == "?")
        {
            PrintHelp(sink);
            return true;
        }

        if (u1 == "PRESET")
        {
            if (toks.size() < 3)
            {
                SinkPrint(sink, "LOG PRESET requires DEFAULT|QUIET|VERBOSE\r\n");
                return true;
            }
            auto u2 = toks[2];
            std::transform(u2.begin(), u2.end(), u2.begin(), [](unsigned char c) { return (char)std::toupper(c); });
            if (u2 == "DEFAULT") ApplyPresetDefault();
            else if (u2 == "QUIET") ApplyPresetQuiet();
            else if (u2 == "VERBOSE") ApplyPresetVerbose();
            else SinkPrint(sink, "Unknown preset. Use DEFAULT|QUIET|VERBOSE\r\n");
            PrintState(sink);
            return true;
        }

        if (u1 == "ON" || u1 == "OFF")
        {
            if (toks.size() < 3)
            {
                SinkPrint(sink, "LOG ON/OFF requires a category (or ALL)\r\n");
                return true;
            }
            const uint32_t mask = ParseCategoryMask(toks[2]);
            if (mask == 0)
            {
                SinkPrint(sink, "Unknown category. Use CORE,CMD,ENTS,SEL,ERASE,PICK,PAGE,VP,ALL\r\n");
                return true;
            }
            EnableMask(mask, u1 == "ON");
            PrintState(sink);
            return true;
        }

        if (u1 == "LEVEL")
        {
            if (toks.size() < 4)
            {
                SinkPrint(sink, "LOG LEVEL <CAT> <LEVEL>\r\n");
                return true;
            }
            const uint32_t mask = ParseCategoryMask(toks[2]);
            if (mask == 0 || (mask & (mask - 1u)) != 0)
            {
                SinkPrint(sink, "LOG LEVEL expects a single category (not ALL).\r\n");
                return true;
            }
            bool ok = false;
            const Level lvl = ParseLevel(toks[3], &ok);
            if (!ok)
            {
                SinkPrint(sink, "Unknown level. Use ERROR|WARN|INFO|DEBUG|TRACE (or 0..4)\r\n");
                return true;
            }
            SetLevel((Category)mask, lvl);
            PrintState(sink);
            return true;
        }

        SinkPrint(sink, "Unknown LOG subcommand. Type LOG HELP\r\n");
        return true;
    }
}
// RenderCoreLib/TextEntityUtil.cpp
#include "pch.h"
#include "TextEntityUtil.h"
#include <cassert>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

// Replace with your logger
static void LogError(const char* msg) { /* LOG_ERROR(msg); */ }

namespace
{
    void FailLoud(const std::string& msg, RenderCore::TextInitPolicy policy)
    {
        LogError(msg.c_str());

#ifndef NDEBUG
        // Debug-time enforcement
        if (policy == RenderCore::TextInitPolicy::AssertAndMessageBox)
        {
#ifdef _WIN32
            MessageBoxA(nullptr, msg.c_str(), "CRITICAL: TextEntity misconfigured", MB_ICONERROR | MB_OK);
#endif
        }
        assert(false && "TextEntity misconfigured");
#endif

        if (policy == RenderCore::TextInitPolicy::Throw)
            throw std::logic_error(msg);

        // In non-throw policy, you can still assert in debug and just return in release.
    }
}

namespace RenderCore
{
    void InitTextEntityOrDie(TextEntity& t,
        const std::string& text,
        hershey_font* font,
        TextInitPolicy policy,
        const char* context)
    {
        t.text = text;      // adjust for your actual field name
        t.font = font;

        if (!t.font)
        {
            std::ostringstream oss;
            oss << "TextEntity.font was NOT set.\n\n"
                << "Context: " << (context ? context : "(none)") << "\n"
                << "Text: \"" << text << "\"\n\n"
                << "This will produce invisible text and waste debugging time.\n"
                << "Fix: set t.font before text generation/rendering.";
            FailLoud(oss.str(), policy);
        }

        // Add other invariants you care about:
        // - size > 0
        // - height > 0
        // - valid font metrics, etc.
    }
}
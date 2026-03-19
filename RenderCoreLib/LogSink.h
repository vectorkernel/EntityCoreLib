#pragma once
#include <string_view>

// Minimal log sink interface to decouple RenderCoreLib logging from any UI (CmdWindow, etc.).
// The sink is non-owning; caller must ensure lifetime exceeds usage.
struct ILogSink
{
    virtual ~ILogSink() = default;
    virtual void AddLine(std::string_view text) = 0;
};

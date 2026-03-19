#pragma once

#include <string>

#include "CmdWindow.h"

class Application;

// Minimal command manager glue:
// - Shows prompts
// - Implements LINE -> pick two points in the viewport
// - Esc cancels interactive command
class CommandUi
    : public CmdWindow::IHost
{
public:
    explicit CommandUi(Application* app) : m_app(app) {}

    // Bind the window we write output to.
    void BindWindow(CmdWindow* wnd) { m_wnd = wnd; }

    void Initialize();

    // Call this after a viewport mouse click is processed so we can print
    // rectangle selection corner prompts (W/C flow) at the right times.
    void SyncAfterViewportClick();

    // CmdWindow::IHost
    void ExecuteLine(const std::string& lineUtf8) override { OnExecuteLine(lineUtf8); }
    void Cancel() override { OnCancel(); }
    bool IsIdleForCommand() const override;
    std::wstring BasePrompt() const override { return L"> "; }

private:
    enum class State
    {
        Idle,
        Line_FirstPoint,
        Line_SecondPoint,
    };

    Application* m_app = nullptr;
    CmdWindow* m_wnd = nullptr;
    State m_state = State::Idle;
    // Implementation helpers (kept private so CmdWindow stays generic/portable).
    void OnExecuteLine(const std::string& lineUtf8);
    void OnCancel();

    static std::string ToUpperTrim(const std::string& s);
};

#pragma once

#include <string>

#include "CmdWindow.h"

class Application;

// Minimal command manager glue:
// - Shows prompts
// - Implements ERASE -> "Select entities:" flow with W/C and Enter to commit
// - Esc cancels interactive command
class CommandUi
    : public CmdWindow::IHost
{
public:
    explicit CommandUi(Application* app) : m_app(app) {}

    // Bind the window we write output to.
    void BindWindow(CmdWindow* wnd) { m_wnd = wnd; }

    void Initialize();

    // CmdWindow::IHost
    void ExecuteLine(const std::string& lineUtf8) override { OnExecuteLine(lineUtf8); }
    void Cancel() override { OnCancel(); }
    bool IsIdleForCommand() const override;
    std::wstring BasePrompt() const override { return L"> "; }

private:
    enum class State
    {
        Idle,
        Erase_SelectEntities,
    };

    Application* m_app = nullptr;
    CmdWindow* m_wnd = nullptr;
    State m_state = State::Idle;

    // Implementation helpers (kept private so CmdWindow stays generic/portable).
    void OnExecuteLine(const std::string& lineUtf8);
    void OnCancel();

    static std::string ToUpperTrim(const std::string& s);
};

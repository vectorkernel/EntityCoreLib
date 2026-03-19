#include "CommandUi.h"

#include "Application.h"

#include <algorithm>
#include <cctype>

static std::wstring WidenAscii(const std::string& s)
{
    std::wstring w;
    w.reserve(s.size());
    for (unsigned char c : s)
        w.push_back((wchar_t)c);
    return w;
}

std::string CommandUi::ToUpperTrim(const std::string& s)
{
    auto isspace_u = [](unsigned char c) { return std::isspace(c) != 0; };
    size_t b = 0;
    while (b < s.size() && isspace_u((unsigned char)s[b])) b++;
    size_t e = s.size();
    while (e > b && isspace_u((unsigned char)s[e - 1])) e--;

    std::string out = s.substr(b, e - b);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return (char)std::toupper(c); });
    return out;
}

void CommandUi::Initialize()
{
    if (!m_app || !m_wnd) return;
    m_state = State::Idle;
    m_wnd->SetBasePrompt(L"> ");

    // Initialize rectangle tracking from current app state.
    m_lastRectActive  = m_app->IsRectSelectionActive();
    m_lastRectWaiting = m_app->IsRectSelectionWaiting();
    m_lastRectMode    = m_app->CurrentRectMode();
}

bool CommandUi::IsIdleForCommand() const
{
    return m_app ? m_app->IsIdleForCommand() : true;
}

void CommandUi::OnCancel()
{
    if (!m_app || !m_wnd) return;

    // Esc cancels interactive command-in-progress.
    m_app->CancelCommand();
    m_state = State::Idle;
    // Ensure cancel message lands on its own line even if we were sitting at an inline prompt.
    m_wnd->AppendText(L"\r\n*Cancel*\r\n");
    m_wnd->ShowBasePromptIfIdle();
}

void CommandUi::OnExecuteLine(const std::string& lineUtf8)
{
    if (!m_app || !m_wnd) return;

    const std::string cmd = ToUpperTrim(lineUtf8);

    // Keep state in sync with the app (e.g. user hit Esc in main viewport).
    if (m_state != State::Idle && m_app->IsIdleForCommand())
        m_state = State::Idle;

    switch (m_state)
    {
    case State::Idle:
    {
        if (cmd == "" || cmd == ">")
        {
            // No-op Enter.
            return;
        }

        if (cmd == "ERASE" || cmd == "E")
        {
            m_app->StartEraseCommand();
            m_state = State::Erase_SelectEntities;
            // Prompt once (inline) on the new line created by CmdWindow::ExecuteCurrentLine().
            m_wnd->AppendText(L"Select entities: ");
            return;
        }

        // Unknown top-level command.
        m_wnd->AppendText(L"Unknown command: ");
        m_wnd->AppendText(WidenAscii(cmd));
        m_wnd->AppendText(L"\r\n");
        return;
    }

    case State::Erase_SelectEntities:
    {
        if (cmd == "")
        {
            // Finish selection -> commit erase.
            m_app->CommitErase();
            const int n = m_app->LastErasedCount();
            m_wnd->AppendText(L"\r\n");
            m_wnd->AppendText(std::to_wstring(n));
            m_wnd->AppendText(L" erased\r\n");
            m_state = State::Idle;
            return;
        }

        if (cmd == "W")
        {
            m_app->BeginWindowSelection(false);
            m_wnd->AppendText(L"(Window) First corner...\r\n");
            // Next prompt comes from viewport clicks (first/second corner).

            // Refresh tracking immediately after starting rect mode.
            m_lastRectActive  = m_app->IsRectSelectionActive();
            m_lastRectWaiting = m_app->IsRectSelectionWaiting();
            m_lastRectMode    = m_app->CurrentRectMode();
            return;
        }

        if (cmd == "C")
        {
            m_app->BeginWindowSelection(true);
            m_wnd->AppendText(L"(Crossing) First corner...\r\n");

            m_lastRectActive  = m_app->IsRectSelectionActive();
            m_lastRectWaiting = m_app->IsRectSelectionWaiting();
            m_lastRectMode    = m_app->CurrentRectMode();
            return;
        }

        // Ignore other text in selection mode.
        m_wnd->AppendText(L"\r\nSelect entities: ");
        return;
    }
    }
}

void CommandUi::SyncAfterViewportClick()
{
    if (!m_app || !m_wnd) return;
    if (m_state != State::Erase_SelectEntities) return;

    const bool rectWaiting = m_app->IsRectSelectionWaiting();
    const bool rectActive  = m_app->IsRectSelectionActive();
    const auto mode        = m_app->CurrentRectMode();

    // Transition: first corner picked (selection window becomes active).
    if (!m_lastRectActive && rectActive && rectWaiting && (mode != InteractionState::RectMode::None))
    {
        if (mode == InteractionState::RectMode::Window)
            m_wnd->AppendText(L"(Window) Second corner...\r\n");
        else if (mode == InteractionState::RectMode::Crossing)
            m_wnd->AppendText(L"(Crossing) Second corner...\r\n");
    }

    // Transition: rectangle selection finished/cancelled -> return to entity selection prompt.
    // Finished: rectActive goes false and tool clears rect mode/waiting.
    if ((m_lastRectWaiting || m_lastRectActive) && !rectWaiting && !rectActive && mode == InteractionState::RectMode::None)
    {
        m_wnd->AppendText(L"Select entities: ");
    }

    m_lastRectActive  = rectActive;
    m_lastRectWaiting = rectWaiting;
    m_lastRectMode    = mode;
}

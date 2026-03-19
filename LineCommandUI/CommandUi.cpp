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

        if (cmd == "LINE" || cmd == "L")
        {
            m_app->StartLineCommand();
            m_state = State::Line_FirstPoint;
            m_wnd->AppendText(L"Specify first point: ");
            return;
        }

        // Unknown top-level command.
        m_wnd->AppendText(L"Unknown command: ");
        m_wnd->AppendText(WidenAscii(cmd));
        m_wnd->AppendText(L"\r\n");
        return;
    }

    case State::Line_FirstPoint:
    case State::Line_SecondPoint:
    {
        // During LINE, input is via viewport clicks; ignore text here.
        m_wnd->AppendText(L"\r\n");
        return;
    }
    }
}


void CommandUi::SyncAfterViewportClick()
{
    if (!m_app || !m_wnd) return;

    // If user cancelled in viewport (Esc), return to idle.
    if (m_state != State::Idle && m_app->IsIdleForCommand())
    {
        m_state = State::Idle;
        m_wnd->ShowBasePromptIfIdle();
        return;
    }

    if (m_state == State::Line_FirstPoint && m_app->IsLineActive() && m_app->HasFirstPoint())
    {
        // First point was clicked; prompt for second.
        m_state = State::Line_SecondPoint;
        m_wnd->AppendText(L"\r\nSpecify next point: ");
        return;
    }

    if (m_state == State::Line_SecondPoint && m_app->IsIdleForCommand())
    {
        // Line completed (second click committed and command ended)
        m_state = State::Idle;
        m_wnd->AppendText(L"\r\n");
        m_wnd->ShowBasePromptIfIdle();
        return;
    }
}

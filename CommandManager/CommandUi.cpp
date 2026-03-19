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
    m_wnd->AppendText(L"*Cancel*\r\n");
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
            m_wnd->AppendText(L"Select entities:\r\n");
            // Show selection prompt immediately (we are no longer idle).
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
            // Keep selection prompt.
            m_wnd->AppendText(L"Select entities: ");
            return;
        }

        if (cmd == "C")
        {
            m_app->BeginWindowSelection(true);
            m_wnd->AppendText(L"(Crossing) First corner...\r\n");
            m_wnd->AppendText(L"Select entities: ");
            return;
        }

        // Ignore other text in selection mode.
        m_wnd->AppendText(L"\r\nSelect entities: ");
        return;
    }
    }
}

#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <string>
#include <functional>
#include <vector>

// Modeless command window hosting a RichEdit control (RICHEDIT50W from Msftedit.dll).
// Acts like a simple command-line console: scrollback is read-only, user input is at the end.
class CmdWindow
{
public:
    CmdWindow() = default;

    static constexpr const wchar_t* kWndClass = L"VKCmdWindow";

    // -------------------------------------------------
    // Optional host interface
    // -------------------------------------------------
    // To keep CmdWindow portable/reusable, bind it to a small host/controller
    // rather than wiring lambdas in each application's WinMain.
    struct IHost
    {
        virtual ~IHost() = default;
        virtual void ExecuteLine(const std::string& lineUtf8) = 0;
        virtual void Cancel() = 0;
        virtual bool IsIdleForCommand() const = 0;
        virtual std::wstring BasePrompt() const { return L"> "; }
    };

    bool Create(HINSTANCE hInst, HWND parent, int x = CW_USEDEFAULT, int y = CW_USEDEFAULT, int w = 700, int h = 220);
    void Destroy();

    // Convenience: create and bind to a host/controller in one call.
    bool CreateWithHost(HINSTANCE hInst, HWND parent, IHost* host,
                        int x = CW_USEDEFAULT, int y = CW_USEDEFAULT, int w = 700, int h = 220)
    {
        if (!Create(hInst, parent, x, y, w, h))
            return false;
        AttachHost(host);
        return true;
    }

    // Bind an IHost implementation (sets base prompt, idle query, execute/cancel callbacks).
    void AttachHost(IHost* host)
    {
        m_host = host;
        if (!m_host)
            return;

        SetBasePrompt(m_host->BasePrompt());
        SetIdleQuery([this]() -> bool { return m_host ? m_host->IsIdleForCommand() : true; });
        SetExecuteCallback([this](const std::string& line) {
            if (m_host) m_host->ExecuteLine(line);
        });
        SetCancelCallback([this]() {
            if (m_host) m_host->Cancel();
        });
    }

    void Show(bool show);
    bool IsVisible() const;

    // Invoked when user presses Enter on a line.
    void SetExecuteCallback(std::function<void(const std::string&)> cb);

    // Invoked when user presses Esc (used to cancel interactive commands).
    void SetCancelCallback(std::function<void()> cb);

    // Idle predicate so we only show the base prompt when app is ready.
    void SetIdleQuery(std::function<bool()> fn);

    void SetBasePrompt(const std::wstring& prompt);
    void ShowBasePromptIfIdle();

    void AppendText(const std::wstring& text);
    void AppendTextUtf8(const std::string& textUtf8);

    HWND Hwnd() const { return m_hwnd; }
    void FocusInput();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                             UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

private:
    HWND m_hwnd = nullptr;
    HWND m_edit = nullptr;
    HMODULE m_msftedit = nullptr;

    std::function<void(const std::string&)> m_onExecute;
    std::function<void()> m_onCancel;
    std::function<bool()> m_isIdleQuery;

    IHost* m_host = nullptr; // optional (non-owning)

    std::wstring m_basePrompt = L"> ";
    bool m_idlePromptShown = false;

    // Character index where user input begins (everything before is read-only history).
    LONG m_promptStart = 0;

    // Scroll behavior: only auto-scroll output if user is already at bottom.
    bool m_stickToBottom = true;

    // Command history (Up/Down recall)
    std::vector<std::string> m_history;
    int m_historyIndex = -1;
    std::string m_historyDraft;

    void OnCreate();
    void OnSize(int w, int h);
    void ExecuteCurrentLine();

    void BeginInputRegionAtEnd(bool forceScroll);
    void BeginNewPromptLine(const std::wstring& prompt);
    void ClampSelectionToInput();
    void UpdateStickToBottom();

    std::string GetCurrentInputUtf8() const;
    void ReplaceCurrentInputUtf8(const std::string& s);
    void CommitHistory(const std::string& s);

    LONG GetTextLenChars() const;
    void SetSelection(LONG a, LONG b) const;
    void GetSelection(LONG& a, LONG& b) const;
    void MoveCaretToEnd(bool forceScroll) const;

    static std::wstring GetLineTextW(HWND edit, int line);
    static std::string WideToUtf8(const std::wstring& ws);
    static std::wstring Utf8ToWide(const std::string& s);
    static std::string Trim(const std::string& s);
};

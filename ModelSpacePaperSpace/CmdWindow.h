#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <string>
#include <functional>
#include <vector>

// Standalone modeless command window hosting a RichEdit control (RICHEDIT50W from Msftedit.dll).
// Designed to be used in ANSI or UNICODE builds (we use the explicit *W APIs internally).
class CmdWindow
{
public:
    CmdWindow() = default;

    // Win32 window class name used for the standalone command window.
    static constexpr const wchar_t* kWndClass = L"VKCmdWindow";

    // Creates the modeless window. parent can be the main app HWND (used for ownership).
    bool Create(HINSTANCE hInst, HWND parent, int x = CW_USEDEFAULT, int y = CW_USEDEFAULT, int w = 700, int h = 220);

    void Destroy();

    void Show(bool show);
    bool IsVisible() const;

    // Set callback invoked when user presses Enter on a line.
    void SetExecuteCallback(std::function<void(const std::string&)> cb);

    // Provide an "idle" predicate so the command window knows when it is appropriate
    // to show the base command prompt.
    // Return true when the application is ready for a new top-level command.
    void SetIdleQuery(std::function<bool()> fn);

    // Show the standard base prompt ("Command: ") if the app is idle.
    void ShowBasePromptIfIdle();

    // Append text to the RichEdit output.
    void AppendText(const std::wstring& text);
    void AppendTextUtf8(const std::string& textUtf8);

    HWND Hwnd() const { return m_hwnd; }

    // Bring command window to front and focus the edit control.
    void FocusInput();


    // Window class and WndProc are public so Win32 registration helpers can reference them.
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                             UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

private:
    HWND m_hwnd = nullptr;
    HWND m_edit = nullptr;
    HMODULE m_msftedit = nullptr;

    std::function<void(const std::string&)> m_onExecute;
    std::function<bool()> m_isIdleQuery;

    // Character index where user input begins (everything before is read-only history).
    LONG m_promptStart = 0;

    // Command history (Up/Down recall)
    std::vector<std::string> m_history;
    int m_historyIndex = -1; // -1 when not browsing
    std::string m_historyDraft;

    void OnCreate();
    void OnSize(int w, int h);
    void ExecuteCurrentLine();

    // Prompt + input helpers
    void BeginInputRegionAtEnd();
    void BeginNewPromptLine(const std::wstring& prompt);
    void ClampSelectionToInput();
    std::string GetCurrentInputUtf8() const;
    void ReplaceCurrentInputUtf8(const std::string& s);
    void CommitHistory(const std::string& s);

    LONG GetTextLenChars() const;
    void SetSelection(LONG a, LONG b) const;
    void GetSelection(LONG& a, LONG& b) const;
    void MoveCaretToEnd() const;

    static std::wstring GetLineTextW(HWND edit, int line);
    static std::string WideToUtf8(const std::wstring& ws);
    static std::wstring Utf8ToWide(const std::string& s);
    static std::string Trim(const std::string& s);
};
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "CmdWindow.h"

#include <richedit.h>
#include <commctrl.h>
#include <algorithm>
#include <cctype>

#pragma comment(lib, "Comctl32.lib")

static bool EnsureClassRegistered(HINSTANCE hInst)
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = hInst;
    wc.lpszClassName = CmdWindow::kWndClass;
    wc.lpfnWndProc = CmdWindow::WndProc;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.style = CS_DBLCLKS;

    if (GetClassInfoExW(hInst, CmdWindow::kWndClass, &wc))
        return true;

    return RegisterClassExW(&wc) != 0;
}

bool CmdWindow::Create(HINSTANCE hInst, HWND parent, int x, int y, int w, int h)
{
    if (m_hwnd) return true;

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);

    // Load RichEdit 5.0
    if (!m_msftedit)
        m_msftedit = LoadLibraryA("Msftedit.dll");

    if (!EnsureClassRegistered(hInst))
        return false;

    m_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kWndClass,
        L"Command",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        x, y, w, h,
        parent,
        nullptr,
        hInst,
        this);

    return m_hwnd != nullptr;
}

void CmdWindow::Destroy()
{
    if (m_edit)
    {
        DestroyWindow(m_edit);
        m_edit = nullptr;
    }
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_msftedit)
    {
        FreeLibrary(m_msftedit);
        m_msftedit = nullptr;
    }
}

void CmdWindow::Show(bool show)
{
    if (!m_hwnd) return;
    ShowWindow(m_hwnd, show ? SW_SHOW : SW_HIDE);
}

bool CmdWindow::IsVisible() const
{
    return m_hwnd && IsWindowVisible(m_hwnd);
}

void CmdWindow::SetExecuteCallback(std::function<void(const std::string&)> cb)
{
    m_onExecute = std::move(cb);
}

void CmdWindow::SetIdleQuery(std::function<bool()> fn)
{
    m_isIdleQuery = std::move(fn);
}

void CmdWindow::ShowBasePromptIfIdle()
{
    if (!m_edit) return;
    if (!m_isIdleQuery) return;
    if (!m_isIdleQuery())
        return;

    BeginNewPromptLine(L"Command: ");
}

LONG CmdWindow::GetTextLenChars() const
{
    if (!m_edit) return 0;
    return (LONG)GetWindowTextLengthW(m_edit);
}

void CmdWindow::SetSelection(LONG a, LONG b) const
{
    if (!m_edit) return;
    SendMessageW(m_edit, EM_SETSEL, (WPARAM)a, (LPARAM)b);
}

void CmdWindow::GetSelection(LONG& a, LONG& b) const
{
    a = b = 0;
    if (!m_edit) return;
    SendMessageW(m_edit, EM_GETSEL, (WPARAM)&a, (LPARAM)&b);
}

void CmdWindow::MoveCaretToEnd() const
{
    if (!m_edit) return;
    SendMessageW(m_edit, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendMessageW(m_edit, EM_SCROLLCARET, 0, 0);
}

void CmdWindow::BeginInputRegionAtEnd()
{
    // Treat everything that already exists as read-only history.
    m_promptStart = GetTextLenChars();
    MoveCaretToEnd();
}

void CmdWindow::BeginNewPromptLine(const std::wstring& prompt)
{
    // Ensure the prompt is on its own line.
    const LONG len = GetTextLenChars();
    if (len > 0)
    {
        // If last char isn't a newline, add one.
        wchar_t tail[2] = { 0,0 };
        TEXTRANGEW tr{};
        tr.chrg.cpMin = std::max<LONG>(0, len - 1);
        tr.chrg.cpMax = len;
        tr.lpstrText = tail;
        SendMessageW(m_edit, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
        if (tail[0] != L'\n')
            AppendText(L"\r\n");
    }

    AppendText(prompt);
    BeginInputRegionAtEnd();

    // Reset history browse state whenever we show a fresh prompt.
    m_historyIndex = -1;
    m_historyDraft.clear();
}

void CmdWindow::ClampSelectionToInput()
{
    if (!m_edit) return;
    LONG a, b;
    GetSelection(a, b);
    const LONG end = GetTextLenChars();

    if (a < m_promptStart) a = m_promptStart;
    if (b < m_promptStart) b = m_promptStart;
    if (a > end) a = end;
    if (b > end) b = end;

    SetSelection(a, b);
    SendMessageW(m_edit, EM_SCROLLCARET, 0, 0);
}

std::string CmdWindow::GetCurrentInputUtf8() const
{
    if (!m_edit) return {};
    const LONG len = GetTextLenChars();
    if (m_promptStart < 0 || m_promptStart > len) return {};

    // Get all text, then slice. (This is fine for a small command window log.)
    std::wstring all;
    all.resize((size_t)len + 1);
    GetWindowTextW(m_edit, all.data(), (int)all.size());
    all.resize(wcslen(all.c_str()));

    if ((size_t)m_promptStart >= all.size())
        return {};
    return WideToUtf8(all.substr((size_t)m_promptStart));
}

void CmdWindow::ReplaceCurrentInputUtf8(const std::string& s)
{
    if (!m_edit) return;
    const LONG end = GetTextLenChars();
    SetSelection(m_promptStart, end);
    const std::wstring w = Utf8ToWide(s);
    SendMessageW(m_edit, EM_REPLACESEL, TRUE, (LPARAM)w.c_str());
    MoveCaretToEnd();
}

void CmdWindow::CommitHistory(const std::string& s)
{
    if (s.empty()) return;
    if (!m_history.empty() && m_history.back() == s) return;
    m_history.push_back(s);
}

void CmdWindow::AppendText(const std::wstring& text)
{
    if (!m_edit) return;

    // Append at end
    const int len = GetWindowTextLengthW(m_edit);
    SendMessageW(m_edit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(m_edit, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
    SendMessageW(m_edit, EM_SCROLLCARET, 0, 0);

    // Anything we print becomes history; keep the editable region at the end.
    BeginInputRegionAtEnd();
}

void CmdWindow::AppendTextUtf8(const std::string& textUtf8)
{
    AppendText(Utf8ToWide(textUtf8));
}

void CmdWindow::FocusInput()
{
    if (!m_hwnd) return;
    ShowWindow(m_hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(m_hwnd);
    SetActiveWindow(m_hwnd);
    if (m_edit) SetFocus(m_edit);
}

LRESULT CALLBACK CmdWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CmdWindow* self = nullptr;

    if (msg == WM_NCCREATE)
    {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<CmdWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->m_hwnd = hwnd;
    }
    else
    {
        self = reinterpret_cast<CmdWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    switch (msg)
    {
    case WM_CREATE:
        if (self) self->OnCreate();
        return 0;

    case WM_SIZE:
        if (self) self->OnSize(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_CLOSE)
            return 0; // ignore Alt+F4 / system close
        break;

    case WM_CLOSE:
        return 0; // ignore X button close
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void CmdWindow::OnCreate()
{
    // Create the RichEdit child control.
    m_edit = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"RICHEDIT50W",
        L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
        0, 0, 100, 100,
        m_hwnd,
        (HMENU)1001,
        (HINSTANCE)GetWindowLongPtrW(m_hwnd, GWLP_HINSTANCE),
        nullptr);

    // Subclass to implement console-like behavior (Enter, history, read-only scrollback)
    SetWindowSubclass(m_edit, EditSubclassProc, 1, (DWORD_PTR)this);

    // Disable Close (X / Alt+F4). Minimize still works.
    if (HMENU sys = GetSystemMenu(m_hwnd, FALSE))
    {
        EnableMenuItem(sys, SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);
        DeleteMenu(sys, SC_CLOSE, MF_BYCOMMAND);
        DrawMenuBar(m_hwnd);
    }

    // Make it look command-ish
    SendMessageW(m_edit, EM_SETBKGNDCOLOR, 0, (LPARAM)RGB(0, 0, 0));
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE;
    cf.crTextColor = RGB(230, 230, 230);
    wcscpy_s(cf.szFaceName, L"Consolas");
    cf.yHeight = 200; // 10pt (twips*?)
    SendMessageW(m_edit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);

    BeginNewPromptLine(L"Command: ");
}

void CmdWindow::OnSize(int w, int h)
{
    if (!m_edit) return;
    MoveWindow(m_edit, 0, 0, w, h, TRUE);
}

LRESULT CALLBACK CmdWindow::EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                            UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    CmdWindow* self = reinterpret_cast<CmdWindow*>(dwRefData);
    if (!self) return DefSubclassProc(hwnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_SETFOCUS:
        self->ClampSelectionToInput();
        break;

    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MOUSEMOVE:
    {
        // Let RichEdit handle selection, then clamp so the caret cannot enter history.
        LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
        self->ClampSelectionToInput();
        return r;
    }

    case WM_PASTE:
    {
        // Force paste into input region.
        LONG a, b; self->GetSelection(a, b);
        if (a < self->m_promptStart)
            self->SetSelection(self->GetTextLenChars(), self->GetTextLenChars());

        LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
        self->ClampSelectionToInput();
        return r;
    }

    case WM_KEYDOWN:
    {
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

        switch (wParam)
        {
        case VK_RETURN:
            self->ExecuteCurrentLine();
            return 0;

        case VK_HOME:
            // Home moves to start of input, not start of document.
            self->SetSelection(self->m_promptStart, self->m_promptStart);
            return 0;

        case VK_UP:
        {
            // History recall (Up)
            const std::string cur = self->GetCurrentInputUtf8();
            if (self->m_historyIndex == -1)
                self->m_historyDraft = cur;

            if (!self->m_history.empty())
            {
                if (self->m_historyIndex < 0)
                    self->m_historyIndex = (int)self->m_history.size() - 1;
                else if (self->m_historyIndex > 0)
                    self->m_historyIndex--;

                self->ReplaceCurrentInputUtf8(self->m_history[(size_t)self->m_historyIndex]);
            }
            return 0;
        }

        case VK_DOWN:
        {
            // History recall (Down)
            if (self->m_historyIndex == -1)
                return 0;

            if (self->m_historyIndex < (int)self->m_history.size() - 1)
            {
                self->m_historyIndex++;
                self->ReplaceCurrentInputUtf8(self->m_history[(size_t)self->m_historyIndex]);
            }
            else
            {
                self->m_historyIndex = -1;
                self->ReplaceCurrentInputUtf8(self->m_historyDraft);
                self->m_historyDraft.clear();
            }
            return 0;
        }

        case 'A':
            if (ctrl)
            {
                // Ctrl+A selects only current input.
                self->SetSelection(self->m_promptStart, self->GetTextLenChars());
                return 0;
            }
            break;

        case VK_BACK:
        case VK_DELETE:
        {
            LONG a, b; self->GetSelection(a, b);
            if (a < self->m_promptStart)
            {
                self->ClampSelectionToInput();
                // If caret was at the fence, block destructive edit.
                self->GetSelection(a, b);
                if (a == b && a == self->m_promptStart)
                    return 0;
            }
            break;
        }
        }

        LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
        self->ClampSelectionToInput();
        return r;
    }

    case WM_CHAR:
    {
        // If caret drifts into history, force it back before typing.
        LONG a, b; self->GetSelection(a, b);
        if (a < self->m_promptStart)
            self->SetSelection(self->GetTextLenChars(), self->GetTextLenChars());

        LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
        self->ClampSelectionToInput();
        return r;
    }
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void CmdWindow::ExecuteCurrentLine()
{
    if (!m_edit) return;

    // Grab only the editable portion (text after the prompt fence).
    std::string lineUtf8 = Trim(GetCurrentInputUtf8());

    // Append newline to commit the current input line into history/log.
    AppendText(L"\r\n");

    // Save to history (do not store empty Enter).
    CommitHistory(lineUtf8);

    // NOTE: We invoke the callback even for an empty line.
    // Interactive commands (e.g. ERASE) use an empty Enter to mean "finish".
    if (m_onExecute)
        m_onExecute(lineUtf8);

    // Only show the base prompt when the app is ready.
    ShowBasePromptIfIdle();
}

std::wstring CmdWindow::GetLineTextW(HWND edit, int line)
{
    const int lineIndex = (int)SendMessageW(edit, EM_LINEINDEX, (WPARAM)line, 0);
    if (lineIndex < 0) return L"";

    const int lineLen = (int)SendMessageW(edit, EM_LINELENGTH, (WPARAM)lineIndex, 0);
    if (lineLen <= 0) return L"";

    // EM_GETLINE requires first WORD be buffer size in TCHARs
    std::wstring buf;
    buf.resize((size_t)lineLen + 2);
    *((WORD*)buf.data()) = (WORD)(lineLen + 1);
    const int copied = (int)SendMessageW(edit, EM_GETLINE, (WPARAM)line, (LPARAM)buf.data());
    if (copied <= 0) return L"";
    buf.resize((size_t)copied);
    return buf;
}

std::string CmdWindow::WideToUtf8(const std::wstring& ws)
{
    if (ws.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string out;
    out.resize((size_t)needed);
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring CmdWindow::Utf8ToWide(const std::string& s)
{
    if (s.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out;
    out.resize((size_t)needed);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), needed);
    return out;
}

std::string CmdWindow::Trim(const std::string& s)
{
    auto isspace_u = [](unsigned char c) { return std::isspace(c) != 0; };
    size_t b = 0;
    while (b < s.size() && isspace_u((unsigned char)s[b])) b++;
    size_t e = s.size();
    while (e > b && isspace_u((unsigned char)s[e - 1])) e--;
    return s.substr(b, e - b);
}

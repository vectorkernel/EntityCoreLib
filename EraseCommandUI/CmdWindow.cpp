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

void CmdWindow::SetCancelCallback(std::function<void()> cb)
{
    m_onCancel = std::move(cb);
}

void CmdWindow::SetIdleQuery(std::function<bool()> fn)
{
    m_isIdleQuery = std::move(fn);
}

void CmdWindow::SetBasePrompt(const std::wstring& prompt)
{
    m_basePrompt = prompt;
}

void CmdWindow::ShowBasePromptIfIdle()
{
    if (!m_edit) return;
    if (!m_isIdleQuery) return;
    if (!m_isIdleQuery())
    {
        // Leaving idle -> allow prompt to be shown again later.
        m_idlePromptShown = false;
        return;
    }
    if (m_idlePromptShown)
        return;
    BeginNewPromptLine(m_basePrompt);
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

void CmdWindow::MoveCaretToEnd(bool forceScroll) const
{
    if (!m_edit) return;
    SendMessageW(m_edit, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    if (forceScroll)
        SendMessageW(m_edit, EM_SCROLLCARET, 0, 0);
}

void CmdWindow::BeginInputRegionAtEnd(bool forceScroll)
{
    m_promptStart = GetTextLenChars();
    MoveCaretToEnd(forceScroll);
}

void CmdWindow::BeginNewPromptLine(const std::wstring& prompt)
{
    const LONG len = GetTextLenChars();
    if (len > 0)
    {
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
    // Prompts imply user intent to interact; jump to bottom.
    m_stickToBottom = true;
    BeginInputRegionAtEnd(true);

    m_idlePromptShown = (prompt == m_basePrompt);

    m_historyIndex = -1;
    m_historyDraft.clear();
}

void CmdWindow::UpdateStickToBottom()
{
    if (!m_edit) return;

    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    if (!GetScrollInfo(m_edit, SB_VERT, &si))
        return;

    // Consider at-bottom if the thumb is at the end.
    const int maxPos = std::max(0, (int)si.nMax - (int)si.nPage);
    m_stickToBottom = (si.nPos >= maxPos);
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

    // User interaction means we want the caret visible; snap to bottom.
    m_stickToBottom = true;
    SendMessageW(m_edit, EM_SCROLLCARET, 0, 0);
}

std::string CmdWindow::GetCurrentInputUtf8() const
{
    if (!m_edit) return {};
    const LONG len = GetTextLenChars();
    if (m_promptStart < 0 || m_promptStart > len) return {};

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
    m_stickToBottom = true;
    MoveCaretToEnd(true);
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

    // Any output means we are no longer sitting at the idle prompt.
    m_idlePromptShown = false;

    UpdateStickToBottom();

    const int len = GetWindowTextLengthW(m_edit);
    SendMessageW(m_edit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(m_edit, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());

    // Anything we print becomes history; keep the editable region at the end.
    BeginInputRegionAtEnd(m_stickToBottom);

    if (m_stickToBottom)
        SendMessageW(m_edit, EM_SCROLLCARET, 0, 0);
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
    m_stickToBottom = true;
    MoveCaretToEnd(true);
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

    SetWindowSubclass(m_edit, EditSubclassProc, 1, (DWORD_PTR)this);

    // Disable Close (X / Alt+F4). Minimize still works.
    if (HMENU sys = GetSystemMenu(m_hwnd, FALSE))
    {
        EnableMenuItem(sys, SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);
        DeleteMenu(sys, SC_CLOSE, MF_BYCOMMAND);
        DrawMenuBar(m_hwnd);
    }

    // Command-ish visuals
    SendMessageW(m_edit, EM_SETBKGNDCOLOR, 0, (LPARAM)RGB(0, 0, 0));
    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE;
    cf.crTextColor = RGB(230, 230, 230);
    wcscpy_s(cf.szFaceName, L"Consolas");
    cf.yHeight = 200; // ~10pt
    SendMessageW(m_edit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);

    BeginNewPromptLine(m_basePrompt);
}

void CmdWindow::OnSize(int w, int h)
{
    if (!m_edit) return;
    MoveWindow(m_edit, 0, 0, w, h, TRUE);
}

LRESULT CALLBACK CmdWindow::EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                            UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData)
{
    CmdWindow* self = reinterpret_cast<CmdWindow*>(dwRefData);
    if (!self) return DefSubclassProc(hwnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_SETFOCUS:
        self->ClampSelectionToInput();
        break;

    case WM_VSCROLL:
    case WM_MOUSEWHEEL:
    {
        LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
        self->UpdateStickToBottom();
        return r;
    }

    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MOUSEMOVE:
    {
        // Allow selecting/copying from scrollback with the mouse.
        // Editing is still constrained to the input region by WM_CHAR / edit keys.
        LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
        self->UpdateStickToBottom();
        return r;
    }

    case WM_PASTE:
    {
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
        case VK_ESCAPE:
            if (self->m_onCancel)
                self->m_onCancel();
            return 0;

        case VK_RETURN:
            self->ExecuteCurrentLine();
            return 0;

        case VK_HOME:
            self->SetSelection(self->m_promptStart, self->m_promptStart);
            return 0;

        case VK_UP:
        {
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
                self->SetSelection(self->m_promptStart, self->GetTextLenChars());
                return 0;
            }
            break;

        case 'C':
        case 'X':
            // Allow Ctrl+C / Ctrl+X to operate on any selection (including scrollback).
            // We'll keep edits constrained elsewhere.
            if (ctrl)
                return DefSubclassProc(hwnd, msg, wParam, lParam);
            break;

        case VK_BACK:
        case VK_DELETE:
        {
            LONG a, b; self->GetSelection(a, b);
            if (a < self->m_promptStart)
            {
                self->ClampSelectionToInput();
                self->GetSelection(a, b);
                if (a == b && a == self->m_promptStart)
                    return 0;
            }
            break;
        }
        }

        LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);

        // Only clamp selection for keys that can move the caret into history and/or edit the buffer.
        // If the user is selecting scrollback (for copy), don't fight them.
        switch (wParam)
        {
        case VK_LEFT:
        case VK_RIGHT:
        case VK_END:
        case VK_BACK:
        case VK_DELETE:
            self->ClampSelectionToInput();
            break;
        default:
            break;
        }

        return r;
    }

    case WM_CHAR:
    {
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

    std::string lineUtf8 = Trim(GetCurrentInputUtf8());

    // Commit input line into history/log.
    AppendText(L"\r\n");

    CommitHistory(lineUtf8);

    // After executing, we're no longer at an idle prompt until the app decides it is.
    m_idlePromptShown = false;

    if (m_onExecute)
        m_onExecute(lineUtf8);

    ShowBasePromptIfIdle();
}

std::wstring CmdWindow::GetLineTextW(HWND edit, int line)
{
    const int lineIndex = (int)SendMessageW(edit, EM_LINEINDEX, (WPARAM)line, 0);
    if (lineIndex < 0) return L"";

    const int lineLen = (int)SendMessageW(edit, EM_LINELENGTH, (WPARAM)lineIndex, 0);
    if (lineLen <= 0) return L"";

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

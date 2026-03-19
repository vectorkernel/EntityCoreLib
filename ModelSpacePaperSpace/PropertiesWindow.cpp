#include "PropertiesWindow.h"

#include "Application.h"
#include "LayerTable.h"

#include <vector>
#include <string>
#include <algorithm>
#include <cstring>

#include <commdlg.h>
#include <commctrl.h>


// Explicit ANSI ListView helpers (avoid UNICODE macro mapping issues).
static int LV_InsertColumnA(HWND hwndLV, int index, const LVCOLUMNA* col) {
    return (int)SendMessageA(hwndLV, LVM_INSERTCOLUMNA, (WPARAM)index, (LPARAM)col);
}
static int LV_GetItemTextA(HWND hwndLV, int iItem, int iSubItem, LPSTR text, int cchTextMax) {
    LVITEMA it{};
    it.iSubItem = iSubItem;
    it.pszText = text;
    it.cchTextMax = cchTextMax;
    return (int)SendMessageA(hwndLV, LVM_GETITEMTEXTA, (WPARAM)iItem, (LPARAM)&it);
}


#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Comdlg32.lib")

static COLORREF ToCOLORREF(const glm::vec4& c)
{
    auto clamp01 = [](float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); };
    const int r = (int)(clamp01(c.r) * 255.0f + 0.5f);
    const int g = (int)(clamp01(c.g) * 255.0f + 0.5f);
    const int b = (int)(clamp01(c.b) * 255.0f + 0.5f);
    return RGB(r, g, b);
}

static glm::vec4 FromCOLORREF(COLORREF cr)
{
    return glm::vec4(
        (float)GetRValue(cr) / 255.0f,
        (float)GetGValue(cr) / 255.0f,
        (float)GetBValue(cr) / 255.0f,
        1.0f);
}


static void EnsureCommonControls()
{
    static bool s_init = false;
    if (s_init) return;

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icc);

    s_init = true;
}

bool PropertiesWindow::Create(HINSTANCE hInst, HWND parent, int x, int y, int w, int h)
{
    EnsureCommonControls();

    m_hInst = hInst;

    WNDCLASSA wc{};
    wc.lpfnWndProc = &PropertiesWindow::WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kWndClass;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassA(&wc);

    m_hwnd = CreateWindowExA(
        WS_EX_TOOLWINDOW,
        kWndClass,
        "Properties",
        WS_OVERLAPPEDWINDOW,
        x, y, w, h,
        parent,
        nullptr,
        hInst,
        this);

    if (!m_hwnd)
        return false;

    Show(false);
    return true;
}

void PropertiesWindow::Destroy()
{
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        m_list = nullptr;
        m_combo = nullptr;
    }
}

void PropertiesWindow::Show(bool show)
{
    if (!m_hwnd) return;
    ShowWindow(m_hwnd, show ? SW_SHOW : SW_HIDE);
}

void PropertiesWindow::Toggle()
{
    Show(!IsVisible());
}

bool PropertiesWindow::IsVisible() const
{
    return m_hwnd && IsWindowVisible(m_hwnd) != FALSE;
}

void PropertiesWindow::SetProperties(const std::vector<std::pair<std::string, std::string>>& rows)
{
    if (!m_list) return;

    ListView_DeleteAllItems(m_list);

    int i = 0;
    for (const auto& kv : rows)
    {
        LVITEMA it{};
        it.mask = LVIF_TEXT;
        it.iItem = i;
        it.iSubItem = 0;
        it.pszText = const_cast<char*>(kv.first.c_str());
        SendMessageA(m_list, LVM_INSERTITEMA, 0, reinterpret_cast<LPARAM>(&it));

        LVITEMA itv{};
        itv.iSubItem = 1;
        itv.pszText = const_cast<char*>(kv.second.c_str());
        SendMessageA(m_list, LVM_SETITEMTEXTA, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(&itv));
        ++i;
    }
}


void PropertiesWindow::SetTypeFilterItems(const std::vector<std::pair<std::string, int>>& items)
{
    if (!m_combo) return;

    // Preserve previous selection label if possible.
    std::string prevLabel;
    {
        const int cur = (int)SendMessageA(m_combo, CB_GETCURSEL, 0, 0);
        if (cur >= 0)
        {
            char buf[256]{};
            SendMessageA(m_combo, CB_GETLBTEXT, cur, (LPARAM)buf);
            prevLabel = buf;
        }
    }

    SendMessageA(m_combo, CB_RESETCONTENT, 0, 0);
    m_filterTags.clear();

    for (const auto& it : items)
    {
        SendMessageA(m_combo, CB_ADDSTRING, 0, (LPARAM)it.first.c_str());
        m_filterTags.push_back(it.second);
    }

    // Restore selection if label exists; otherwise clamp.
    int newSel = 0;
    if (!prevLabel.empty())
    {
        const int found = (int)SendMessageA(m_combo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)prevLabel.c_str());
        if (found >= 0) newSel = found;
    }
    if (newSel >= (int)m_filterTags.size()) newSel = (int)m_filterTags.size() - 1;
    if (newSel < 0) newSel = 0;

    m_filterSel = newSel;
    SendMessageA(m_combo, CB_SETCURSEL, (WPARAM)m_filterSel, 0);
}

int PropertiesWindow::GetTypeFilterTag() const
{
    if (m_filterSel < 0 || m_filterSel >= (int)m_filterTags.size())
        return -1;
    return m_filterTags[m_filterSel];
}

LRESULT CALLBACK PropertiesWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PropertiesWindow* self = nullptr;

    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        self = reinterpret_cast<PropertiesWindow*>(cs->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<PropertiesWindow*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    }

    if (self)
        return self->HandleMsg(hwnd, msg, wParam, lParam);

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

LRESULT PropertiesWindow::HandleMsg(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        CreateChildControls(hwnd);
        return 0;

    case WM_SIZE:
        ResizeChildControls(LOWORD(lParam), HIWORD(lParam));
        return 0;


    case WM_COMMAND:
    {
        if ((HWND)lParam == m_combo && HIWORD(wParam) == CBN_SELCHANGE)
        {
            m_filterSel = (int)SendMessageA(m_combo, CB_GETCURSEL, 0, 0);
            if (m_filterSel < 0) m_filterSel = 0;
            return 0;
        }
        break;
    }


    case WM_NOTIFY:
    {
        auto* nm = reinterpret_cast<NMHDR*>(lParam);
        if (nm->hwndFrom == m_list && nm->code == NM_DBLCLK && m_app)
        {
            auto* act = reinterpret_cast<NMITEMACTIVATE*>(lParam);
            const int row = act->iItem;
            if (row >= 0)
            {
                char propName[256]{};
                LV_GetItemTextA(m_list, row, 0, propName, 255);

                // Only a couple of editable properties for now.
                if (strcmp(propName, "Layer") == 0)
                {
                    // Build a popup menu of layers.
                    HMENU hMenu = CreatePopupMenu();
                    const auto& layers = m_app->GetLayerTable().GetLayers();
                    const uint32_t curSel = m_app->GetSelectionLayerIdCommon();

                    const UINT kBase = 10000;
                    UINT cmdId = kBase;

                    for (const auto& L : layers)
                    {
                        UINT flags = MF_STRING;
                        if (L.id == curSel) flags |= MF_CHECKED;
                        AppendMenuA(hMenu, flags, cmdId, L.name.c_str());
                        ++cmdId;
                    }

                    POINT pt{};
                    GetCursorPos(&pt);
                    UINT picked = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, m_hwnd, nullptr);
                    if (picked >= kBase && picked < kBase + (UINT)layers.size())
                    {
                        const uint32_t lid = layers[picked - kBase].id;
                        m_app->SetSelectionLayer(lid);
                    }
                    DestroyMenu(hMenu);
                    return 0;
                }
                else if (strcmp(propName, "Color") == 0)
                {
                    HMENU hMenu = CreatePopupMenu();
                    AppendMenuA(hMenu, MF_STRING, 20001, "ByLayer");
                    AppendMenuA(hMenu, MF_STRING, 20002, "Custom...");
                    POINT pt{}; GetCursorPos(&pt);
                    UINT picked = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, m_hwnd, nullptr);
                    DestroyMenu(hMenu);

                    if (picked == 20001)
                    {
                        m_app->SetSelectionColorByLayer();
                        return 0;
                    }
                    if (picked == 20002)
                    {
                        bool byLayer = true;
                        glm::vec4 col(1,1,1,1);
                        m_app->GetSelectionColorCommon(byLayer, col);

                        CHOOSECOLORA cc{};
                        COLORREF cust[16]{};
                        cc.lStructSize = sizeof(cc);
                        cc.hwndOwner = m_hwnd;
                        cc.rgbResult = ToCOLORREF(col);
                        cc.lpCustColors = cust;
                        cc.Flags = CC_RGBINIT | CC_FULLOPEN;
                        if (ChooseColorA(&cc))
                            m_app->SetSelectionColorCustom(FromCOLORREF(cc.rgbResult));
                        return 0;
                    }
                }
            }
        }
        break;
    }
    case WM_CLOSE:
        Show(false); // modeless: hide instead of destroy
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void PropertiesWindow::CreateChildControls(HWND hwnd)
{
    // Top filter combo (Mixed / Lines / Text / ...)
    m_combo = CreateWindowExA(
        0,
        "COMBOBOX",
        "",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        0, 0, 1, 200,
        hwnd,
        (HMENU)1001,
        m_hInst,
        nullptr);

    // Properties grid
    m_list = CreateWindowExA(
        0,
        WC_LISTVIEWA,
        "",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 1, 1,
        hwnd,
        nullptr,
        m_hInst,
        nullptr);

    ListView_SetExtendedListViewStyle(m_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNA c0{};
    c0.mask = LVCF_TEXT | LVCF_WIDTH;
    c0.pszText = const_cast<char*>("Property");
    c0.cx = 160;
    LV_InsertColumnA(m_list, 0, &c0);

    LVCOLUMNA c1{};
    c1.mask = LVCF_TEXT | LVCF_WIDTH;
    c1.pszText = const_cast<char*>("Value");
    c1.cx = 170;
    LV_InsertColumnA(m_list, 1, &c1);

    // Default filter: Mixed/All
    m_filterTags = { -1 };
    SendMessageA(m_combo, CB_ADDSTRING, 0, (LPARAM)"Mixed");
    SendMessageA(m_combo, CB_SETCURSEL, 0, 0);
    m_filterSel = 0;
}

void PropertiesWindow::ResizeChildControls(int w, int h)
{
    const int pad = 6;
    const int comboH = 24;

    if (m_combo)
        MoveWindow(m_combo, pad, pad, std::max(1, w - 2 * pad), comboH, TRUE);

    if (m_list)
        MoveWindow(m_list, pad, pad + comboH + pad, std::max(1, w - 2 * pad), std::max(1, h - (pad + comboH + pad + pad)), TRUE);
}
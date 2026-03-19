#include "LayersWindow.h"
#include "Application.h"
#include "LayerTable.h"

#include <commdlg.h>
#include <commctrl.h>

#include <vector>
#include <sstream>
#include <algorithm>
#include <cstdio>


// Explicit ANSI ListView helpers (avoid UNICODE macro mapping issues).
static BOOL LV_DeleteAllItems(HWND hwndLV) {
    return (BOOL)SendMessageA(hwndLV, LVM_DELETEALLITEMS, 0, 0);
}

static int LV_InsertColumnA(HWND hwndLV, int index, const LVCOLUMNA* col) {
    return (int)SendMessageA(hwndLV, LVM_INSERTCOLUMNA, (WPARAM)index, (LPARAM)col);
}
static int LV_InsertItemA(HWND hwndLV, const LVITEMA* item) {
    return (int)SendMessageA(hwndLV, LVM_INSERTITEMA, 0, (LPARAM)item);
}
static BOOL LV_SetItemTextA(HWND hwndLV, int iItem, int iSubItem, LPSTR text) {
    LVITEMA it{};
    it.iSubItem = iSubItem;
    it.pszText = text;
    return (BOOL)SendMessageA(hwndLV, LVM_SETITEMTEXTA, (WPARAM)iItem, (LPARAM)&it);
}
static BOOL LV_GetItemA(HWND hwndLV, LVITEMA* item) {
    return (BOOL)SendMessageA(hwndLV, LVM_GETITEMA, 0, (LPARAM)item);
}
static BOOL LV_DeleteItem(HWND hwndLV, int iItem) {
    return (BOOL)SendMessage(hwndLV, LVM_DELETEITEM, (WPARAM)iItem, 0);
}
static HWND LV_EditLabelA(HWND hwndLV, int iItem) {
    return (HWND)SendMessageA(hwndLV, LVM_EDITLABELA, (WPARAM)iItem, 0);
}


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

bool LayersWindow::Create(HINSTANCE hInst, HWND parent, Application* app, int x, int y, int w, int h)
{
    m_hInst = hInst;
    m_app = app;

    WNDCLASSA wc{};
    wc.lpfnWndProc = LayersWindow::WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kWndClass;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassA(&wc);

    m_hwnd = CreateWindowExA(
        WS_EX_TOOLWINDOW,
        kWndClass,
        "Layers",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
        x, y, w, h,
        parent,
        nullptr,
        hInst,
        this);

    if (!m_hwnd) return false;

    CreateChildControls(m_hwnd);
    Refresh();
    return true;
}

void LayersWindow::Destroy()
{
    if (m_hwnd) DestroyWindow(m_hwnd);
    m_hwnd = nullptr;
    m_list = nullptr;
}

void LayersWindow::Show(bool show)
{
    if (!m_hwnd) return;
    ShowWindow(m_hwnd, show ? SW_SHOW : SW_HIDE);
    if (show) SetForegroundWindow(m_hwnd);
}

void LayersWindow::Toggle()
{
    Show(!IsVisible());
}

bool LayersWindow::IsVisible() const
{
    return m_hwnd && IsWindowVisible(m_hwnd);
}

void LayersWindow::Refresh()
{
    if (!m_hwnd || !m_app) return;
    PopulateList();
}

LRESULT CALLBACK LayersWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LayersWindow* self = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        self = reinterpret_cast<LayersWindow*>(cs->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->m_hwnd = hwnd;
    }
    else
    {
        self = reinterpret_cast<LayersWindow*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    }

    if (self) return self->HandleMsg(hwnd, msg, wParam, lParam);
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

LRESULT LayersWindow::HandleMsg(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SIZE:
        ResizeChildControls(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_COMMAND:
        if ((HWND)lParam == m_btnAdd) { OnAddLayer(); return 0; }
        if ((HWND)lParam == m_btnDelete) { OnDeleteLayer(); return 0; }
        if ((HWND)lParam == m_btnOn) { OnToggleOn(); return 0; }
        if ((HWND)lParam == m_btnFreeze) { OnToggleFrozen(); return 0; }
        if ((HWND)lParam == m_btnCurrent) { OnSetCurrent(); return 0; }
        if ((HWND)lParam == m_btnColor) { OnPickColor(); return 0; }
        return 0;

    case WM_NOTIFY:
    {
        auto* nm = reinterpret_cast<NMHDR*>(lParam);
        if (nm->hwndFrom == m_list)
        {
            if (nm->code == LVN_ENDLABELEDITW)
            {
                auto* di = reinterpret_cast<NMLVDISPINFOW*>(lParam);
                if (di->item.pszText)
                {
                    const int row = di->item.iItem;
                    uint32_t lid = GetLayerIdFromRow(row);
                    // convert to utf8
                    int n = WideCharToMultiByte(CP_UTF8, 0, di->item.pszText, -1, nullptr, 0, nullptr, nullptr);
                    std::string utf8; utf8.resize(std::max(0, n - 1));
                    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, di->item.pszText, -1, utf8.data(), n, nullptr, nullptr);
                    m_app->GetLayerTable().RenameLayer(lid, utf8);
                    m_app->ApplyLayerDefaultsToEntities();
                    Refresh();
                    return TRUE;
                }
                return FALSE;
            }
            else if (nm->code == NM_DBLCLK)
            {
                // Double click Name column -> edit label
                int row = GetSelectedRow();
                if (row >= 0) LV_EditLabelA(m_list, row);
                return 0;
            }
        }
        break;
    }

    case WM_CLOSE:
        Show(false);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void LayersWindow::CreateChildControls(HWND hwnd)
{
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    m_list = CreateWindowExA(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEWA,
        "",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_EDITLABELS,
        0, 0, 100, 100,
        hwnd,
        (HMENU)1001,
        m_hInst,
        nullptr);

    ListView_SetExtendedListViewStyle(m_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNA col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    col.pszText = (LPSTR)"Name"; col.cx = 170; col.iSubItem = 0; LV_InsertColumnA(m_list, 0, &col);
    col.pszText = (LPSTR)"On"; col.cx = 50; col.iSubItem = 1; LV_InsertColumnA(m_list, 1, &col);
    col.pszText = (LPSTR)"Frozen"; col.cx = 60; col.iSubItem = 2; LV_InsertColumnA(m_list, 2, &col);
    col.pszText = (LPSTR)"Color"; col.cx = 80; col.iSubItem = 3; LV_InsertColumnA(m_list, 3, &col);
    col.pszText = (LPSTR)"Linetype"; col.cx = 100; col.iSubItem = 4; LV_InsertColumnA(m_list, 4, &col);
    col.pszText = (LPSTR)"Id"; col.cx = 60; col.iSubItem = 5; LV_InsertColumnA(m_list, 5, &col);

    m_btnAdd = CreateWindowA("BUTTON", "Add", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 60, 24, hwnd, (HMENU)2001, m_hInst, nullptr);
    m_btnDelete = CreateWindowA("BUTTON", "Delete", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 60, 24, hwnd, (HMENU)2002, m_hInst, nullptr);
    m_btnOn = CreateWindowA("BUTTON", "On/Off", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 70, 24, hwnd, (HMENU)2003, m_hInst, nullptr);
    m_btnFreeze = CreateWindowA("BUTTON", "Freeze", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 70, 24, hwnd, (HMENU)2004, m_hInst, nullptr);
    m_btnColor = CreateWindowA("BUTTON", "Color...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 70, 24, hwnd, (HMENU)2005, m_hInst, nullptr);
    m_btnCurrent = CreateWindowA("BUTTON", "Set Current", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 90, 24, hwnd, (HMENU)2006, m_hInst, nullptr);
}

void LayersWindow::ResizeChildControls(int w, int h)
{
    const int pad = 8;
    const int btnH = 26;
    const int btnY = h - pad - btnH;

    const int listH = std::max(10, btnY - pad);
    MoveWindow(m_list, pad, pad, std::max(10, w - 2 * pad), listH, TRUE);

    int x = pad;
    MoveWindow(m_btnAdd, x, btnY, 60, btnH, TRUE); x += 66;
    MoveWindow(m_btnDelete, x, btnY, 60, btnH, TRUE); x += 66;
    MoveWindow(m_btnOn, x, btnY, 70, btnH, TRUE); x += 76;
    MoveWindow(m_btnFreeze, x, btnY, 70, btnH, TRUE); x += 76;
    MoveWindow(m_btnColor, x, btnY, 70, btnH, TRUE); x += 76;
    MoveWindow(m_btnCurrent, x, btnY, 90, btnH, TRUE);
}

void LayersWindow::PopulateList()
{
    LV_DeleteAllItems(m_list);
    if (!m_app) return;

    const auto& layers = m_app->GetLayerTable().GetLayers();
    const uint32_t cur = m_app->GetLayerTable().CurrentLayerId();

    int row = 0;
    for (const auto& L : layers)
    {
        LVITEMA it{};
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = row;
        it.iSubItem = 0;

        std::string label = L.name;
        if (L.id == cur) label += " (current)";
        it.pszText = (LPSTR)label.c_str();
        it.lParam = (LPARAM)L.id;
        LV_InsertItemA(m_list, &it);

        LV_SetItemTextA(m_list, row, 1, (LPSTR)(L.on ? "Yes" : "No"));
        LV_SetItemTextA(m_list, row, 2, (LPSTR)(L.frozen ? "Yes" : "No"));

        char colorTxt[32];
        COLORREF cr = ToCOLORREF(L.defaultColor);
        sprintf_s(colorTxt, "#%02X%02X%02X", (unsigned)GetRValue(cr), (unsigned)GetGValue(cr), (unsigned)GetBValue(cr));
        LV_SetItemTextA(m_list, row, 3, colorTxt);

        LV_SetItemTextA(m_list, row, 4, (LPSTR)L.defaultLinetype.c_str());

        char idTxt[32];
        sprintf_s(idTxt, "%u", (unsigned)L.id);
        LV_SetItemTextA(m_list, row, 5, idTxt);

        ++row;
    }
}

int LayersWindow::GetSelectedRow() const
{
    return ListView_GetNextItem(m_list, -1, LVNI_SELECTED);
}

uint32_t LayersWindow::GetLayerIdFromRow(int row) const
{
    LVITEMA it{};
    it.mask = LVIF_PARAM;
    it.iItem = row;
    it.iSubItem = 0;
    if (LV_GetItemA(m_list, &it))
        return (uint32_t)it.lParam;
    return LayerTable::kInvalidLayerId;
}

void LayersWindow::OnAddLayer()
{
    uint32_t id = m_app->GetLayerTable().AddLayer();
    m_app->GetLayerTable().SetCurrentLayer(id);
    Refresh();
}

void LayersWindow::OnDeleteLayer()
{
    int row = GetSelectedRow();
    if (row < 0) return;
    uint32_t id = GetLayerIdFromRow(row);
    if (id == m_app->GetLayerTable().CurrentLayerId())
    {
        MessageBoxW(m_hwnd, L"Cannot delete the current layer. Set a different current layer first.", L"Layers", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!m_app->GetLayerTable().DeleteLayer(id))
        MessageBoxW(m_hwnd, L"Layer delete failed (last layer or invalid).", L"Layers", MB_OK | MB_ICONINFORMATION);

    m_app->ApplyLayerDefaultsToEntities();
    Refresh();
}

void LayersWindow::OnToggleOn()
{
    int row = GetSelectedRow();
    if (row < 0) return;
    uint32_t id = GetLayerIdFromRow(row);
    auto* L = m_app->GetLayerTable().Find(id);
    if (!L) return;
    m_app->GetLayerTable().SetLayerOn(id, !L->on);

    m_app->ApplyLayerDefaultsToEntities();
    Refresh();
}

void LayersWindow::OnToggleFrozen()
{
    int row = GetSelectedRow();
    if (row < 0) return;
    uint32_t id = GetLayerIdFromRow(row);
    auto* L = m_app->GetLayerTable().Find(id);
    if (!L) return;
    m_app->GetLayerTable().SetLayerFrozen(id, !L->frozen);

    m_app->ApplyLayerDefaultsToEntities();
    Refresh();
}

void LayersWindow::OnSetCurrent()
{
    int row = GetSelectedRow();
    if (row < 0) return;
    uint32_t id = GetLayerIdFromRow(row);
    m_app->GetLayerTable().SetCurrentLayer(id);
    Refresh();
}

void LayersWindow::OnPickColor()
{
    int row = GetSelectedRow();
    if (row < 0) return;
    uint32_t id = GetLayerIdFromRow(row);

    LayerTable::Layer* L = m_app->GetLayerTable().Find(id);
    if (!L) return;

    CHOOSECOLORA cc{};
    COLORREF cust[16]{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = m_hwnd;
    cc.rgbResult = ToCOLORREF(L->defaultColor);
    cc.lpCustColors = cust;
    cc.Flags = CC_RGBINIT | CC_FULLOPEN;

    if (ChooseColorA(&cc))
    {
        m_app->GetLayerTable().SetLayerColor(id, FromCOLORREF(cc.rgbResult));
        m_app->ApplyLayerDefaultsToEntities();
        Refresh();
    }
}
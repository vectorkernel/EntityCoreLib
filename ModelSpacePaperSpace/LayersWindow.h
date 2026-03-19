#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <commctrl.h>
#include <string>

class Application;

// Floating Layers dialog (ModelSpace/PaperSpace share the same global layer table).
class LayersWindow
{
public:
    LayersWindow() = default;
    static constexpr const char* kWndClass = "VKLayersWindow";

    bool Create(HINSTANCE hInst, HWND parent, Application* app, int x = CW_USEDEFAULT, int y = CW_USEDEFAULT, int w = 520, int h = 420);
    void Destroy();

    void Show(bool show);
    void Toggle();
    bool IsVisible() const;

    void Refresh();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMsg(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateChildControls(HWND hwnd);
    void ResizeChildControls(int w, int h);

    void PopulateList();
    int  GetSelectedRow() const;
    uint32_t GetLayerIdFromRow(int row) const;

    void OnAddLayer();
    void OnDeleteLayer();
    void OnToggleOn();
    void OnToggleFrozen();
    void OnSetCurrent();
    void OnPickColor();

private:
    HINSTANCE m_hInst = nullptr;
    HWND m_hwnd = nullptr;
    HWND m_list = nullptr;

    HWND m_btnAdd = nullptr;
    HWND m_btnDelete = nullptr;
    HWND m_btnOn = nullptr;
    HWND m_btnFreeze = nullptr;
    HWND m_btnCurrent = nullptr;
    HWND m_btnColor = nullptr;

    Application* m_app = nullptr;
};

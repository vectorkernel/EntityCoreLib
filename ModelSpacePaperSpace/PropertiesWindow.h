#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <string>
#include <vector>

class Application;

// Simple floating Properties window.
// Current implementation uses a Win32 ListView in report mode (2 columns: Property / Value).
// Editing + combo-box style editors can be layered on later.
class PropertiesWindow
{
public:
    PropertiesWindow() = default;

    static constexpr const char* kWndClass = "VKPropertiesWindow";

    bool Create(HINSTANCE hInst, HWND parent, int x = CW_USEDEFAULT, int y = CW_USEDEFAULT, int w = 360, int h = 420);
    void Destroy();

    void Show(bool show);
    void Toggle();
    bool IsVisible() const;

    HWND Hwnd() const { return m_hwnd; }

    // Replace all rows with the given property pairs.
    void SetProperties(const std::vector<std::pair<std::string, std::string>>& rows);

    // Selection type filter combo (top of window). Items are {label, tag} where tag=-1 means 'Mixed/All'.
    void SetTypeFilterItems(const std::vector<std::pair<std::string, int>>& items);
    int  GetTypeFilterTag() const;

    // Host provides selection + layer editing actions.
    void SetHost(Application* app) { m_app = app; }


private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMsg(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateChildControls(HWND hwnd);
    void ResizeChildControls(int w, int h);

private:
    HINSTANCE m_hInst = nullptr;
    HWND m_hwnd = nullptr;
    HWND m_list = nullptr;
    HWND m_combo = nullptr;

    std::vector<int> m_filterTags;
    int m_filterSel = 0;
    Application* m_app = nullptr;
};

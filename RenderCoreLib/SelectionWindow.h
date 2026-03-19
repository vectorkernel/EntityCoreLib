#pragma once

#include <vector>
#include <glm/glm.hpp>

#include "LineEntity.h"

namespace RenderCore
{
    // Drag-selection window helper.
    // Tracks mouse-down + drag in Win32 client coords (origin top-left, y down)
    // and can build a screen-space Y-up overlay rectangle (origin bottom-left).
    class SelectionWindow
    {
    public:
        void Reset();

        void BeginClient(int x, int y);
        void UpdateClient(int x, int y);
        void End();

        bool IsActive() const { return m_active; }
        bool HasRect() const { return m_hasRect; }

        // Window selection: left->right drag
        // Crossing selection: right->left drag
        bool IsCrossing() const { return m_crossing; }

        // Normalized client rect (x0<=x1, y0<=y1)
        void GetClientRect(int& x0, int& y0, int& x1, int& y1) const;

        // Build an outline rectangle in overlay space (Y-up pixels).
        // Clears and appends 4 LineEntity edges.
        void BuildOverlayOutline(int viewportW, int viewportH,
                                const glm::vec4& color,
                                float widthPx,
                                std::vector<LineEntity>& outLines) const;

    private:
        bool m_active = false;
        bool m_hasRect = false;
        bool m_crossing = false;

        int m_x0 = 0;
        int m_y0 = 0;
        int m_x1 = 0;
        int m_y1 = 0;
    };
}

#include "pch.h"
#include "SelectionWindow.h"

#include <algorithm>

namespace RenderCore
{
    void SelectionWindow::Reset()
    {
        m_active = false;
        m_hasRect = false;
        m_crossing = false;
        m_x0 = m_y0 = m_x1 = m_y1 = 0;
    }

    void SelectionWindow::BeginClient(int x, int y)
    {
        m_active = true;
        m_hasRect = false;
        m_x0 = m_x1 = x;
        m_y0 = m_y1 = y;
        m_crossing = false;
    }

    void SelectionWindow::UpdateClient(int x, int y)
    {
        if (!m_active) return;
        m_x1 = x;
        m_y1 = y;
        m_hasRect = (m_x0 != m_x1) || (m_y0 != m_y1);
        m_crossing = (m_x1 < m_x0);
    }

    void SelectionWindow::End()
    {
        m_active = false;
        // keep m_hasRect + coordinates so app can use the final rect
    }

    void SelectionWindow::GetClientRect(int& x0, int& y0, int& x1, int& y1) const
    {
        x0 = std::min(m_x0, m_x1);
        x1 = std::max(m_x0, m_x1);
        y0 = std::min(m_y0, m_y1);
        y1 = std::max(m_y0, m_y1);
    }

    void SelectionWindow::BuildOverlayOutline(int viewportW, int viewportH,
                                             const glm::vec4& color,
                                             float widthPx,
                                             std::vector<LineEntity>& outLines) const
    {
        outLines.clear();
        if (!m_hasRect) return;

        viewportW = std::max(1, viewportW);
        viewportH = std::max(1, viewportH);

        int cx0, cy0, cx1, cy1;
        GetClientRect(cx0, cy0, cx1, cy1);

        // Convert client (y-down) -> overlay (y-up)
        const float x0 = (float)std::clamp(cx0, 0, viewportW);
        const float x1 = (float)std::clamp(cx1, 0, viewportW);

        const float oy0 = (float)std::clamp(viewportH - 1 - cy1, 0, viewportH);
        const float oy1 = (float)std::clamp(viewportH - 1 - cy0, 0, viewportH);

        const glm::vec3 b00(x0, oy0, 0.0f);
        const glm::vec3 b10(x1, oy0, 0.0f);
        const glm::vec3 b11(x1, oy1, 0.0f);
        const glm::vec3 b01(x0, oy1, 0.0f);

        auto push = [&](glm::vec3 a, glm::vec3 b)
        {
            LineEntity e;
            e.p0 = a;
            e.p1 = b;
            e.color = color;
            e.width = widthPx;
            outLines.push_back(e);
        };

        push(b00, b10);
        push(b10, b11);
        push(b11, b01);
        push(b01, b00);
    }
}

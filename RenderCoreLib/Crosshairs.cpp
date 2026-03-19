#include "pch.h"
#include "Crosshairs.h"

#include <algorithm>

namespace RenderCore
{
    void Crosshairs::SetViewport(int w, int h)
    {
        m_vpW = std::max(1, w);
        m_vpH = std::max(1, h);

        // Clamp mouse to viewport so math stays sane.
        m_mouseX = std::clamp(m_mouseX, 0, m_vpW - 1);
        m_mouseY = std::clamp(m_mouseY, 0, m_vpH - 1);
    }

    void Crosshairs::SetMouseClient(int x, int y)
    {
        m_mouseX = std::clamp(x, 0, m_vpW - 1);
        m_mouseY = std::clamp(y, 0, m_vpH - 1);
    }

    void Crosshairs::ToggleMode()
    {
        m_mode = (m_mode == CrosshairsMode::GripsSelection) ? CrosshairsMode::PointPick
                                                            : CrosshairsMode::GripsSelection;
    }

    void Crosshairs::NextBackground()
    {
        const int n = static_cast<int>(CrosshairsBackground::COUNT);
        int i = static_cast<int>(m_bg);
        i = (i + 1) % std::max(1, n);
        m_bg = static_cast<CrosshairsBackground>(i);
    }

    CrosshairsColors Crosshairs::GetColors() const
    {
        CrosshairsColors c;

        // Intentionally simple presets for the demo.
        switch (m_bg)
        {
        case CrosshairsBackground::Dark:
            c.clearColor = glm::vec4(0.08f, 0.09f, 0.11f, 1.0f);
            c.crosshairs = glm::vec4(1, 1, 1, 1);
            c.hudText    = glm::vec4(1, 1, 0, 1);
            break;
        case CrosshairsBackground::MidGray:
            c.clearColor = glm::vec4(0.35f, 0.35f, 0.37f, 1.0f);
            c.crosshairs = glm::vec4(1, 1, 1, 1);
            c.hudText    = glm::vec4(1, 1, 0, 1);
            break;
        case CrosshairsBackground::Light:
            c.clearColor = glm::vec4(0.92f, 0.92f, 0.93f, 1.0f);
            c.crosshairs = glm::vec4(0, 0, 0, 1);
            c.hudText    = glm::vec4(0, 0, 0, 1);
            break;
        case CrosshairsBackground::Blueprint:
            c.clearColor = glm::vec4(0.06f, 0.10f, 0.24f, 1.0f);
            c.crosshairs = glm::vec4(0.80f, 0.95f, 1.0f, 1.0f);
            c.hudText    = glm::vec4(0.80f, 0.95f, 1.0f, 1.0f);
            break;
        case CrosshairsBackground::Paper:
            c.clearColor = glm::vec4(1.0f, 0.99f, 0.96f, 1.0f);
            c.crosshairs = glm::vec4(0.05f, 0.05f, 0.05f, 1.0f);
            c.hudText    = glm::vec4(0.05f, 0.05f, 0.05f, 1.0f);
            break;
        default:
            break;
        }

        return c;
    }

    void Crosshairs::BuildLines(std::vector<LineEntity>& outLines) const
    {
        outLines.clear();

        const CrosshairsColors colors = GetColors();
        const glm::vec4 col = colors.crosshairs;

        // Convert Win32 client Y-down -> overlay Y-up.
        const float cx = static_cast<float>(m_mouseX);
        const float cy = static_cast<float>(m_vpH - 1 - m_mouseY);

        const bool drawCrossLines =
            (m_mode == CrosshairsMode::GripsSelection) ||
            (m_mode == CrosshairsMode::PointPick);

        const bool drawPickBox =
            (m_mode == CrosshairsMode::GripsSelection) ||
            (m_mode == CrosshairsMode::PickerOnly);

        // Crosshair lines: two (horizontal + vertical)
        if (drawCrossLines)
        {
            LineEntity h;
            h.p0 = glm::vec3(0.0f, cy, 0.0f);
            h.p1 = glm::vec3(static_cast<float>(m_vpW), cy, 0.0f);
            h.color = col;
            h.width = 1.0f;
            outLines.push_back(h);

            LineEntity v;
            v.p0 = glm::vec3(cx, 0.0f, 0.0f);
            v.p1 = glm::vec3(cx, static_cast<float>(m_vpH), 0.0f);
            v.color = col;
            v.width = 1.0f;
            outLines.push_back(v);
        }

        // Center picker box.
        if (drawPickBox)
        {
            const float half = 0.5f * static_cast<float>(m_pickBoxPx);

            const glm::vec3 b00(cx - half, cy - half, 0.0f);
            const glm::vec3 b10(cx + half, cy - half, 0.0f);
            const glm::vec3 b11(cx + half, cy + half, 0.0f);
            const glm::vec3 b01(cx - half, cy + half, 0.0f);

            LineEntity e0; e0.p0 = b00; e0.p1 = b10; e0.color = col; e0.width = 1.0f; outLines.push_back(e0);
            LineEntity e1; e1.p0 = b10; e1.p1 = b11; e1.color = col; e1.width = 1.0f; outLines.push_back(e1);
            LineEntity e2; e2.p0 = b11; e2.p1 = b01; e2.color = col; e2.width = 1.0f; outLines.push_back(e2);
            LineEntity e3; e3.p0 = b01; e3.p1 = b00; e3.color = col; e3.width = 1.0f; outLines.push_back(e3);
        }
    }

    const char* Crosshairs::ModeName(CrosshairsMode m)
    {
        switch (m)
        {
        case CrosshairsMode::GripsSelection: return "GripsSelection (center box)";
        case CrosshairsMode::PointPick:      return "PointPick (lines only)";
        default:                             return "(unknown)";
        }
    }

    const char* Crosshairs::BackgroundName(CrosshairsBackground b)
    {
        switch (b)
        {
        case CrosshairsBackground::Dark:      return "Dark";
        case CrosshairsBackground::MidGray:   return "MidGray";
        case CrosshairsBackground::Light:     return "Light";
        case CrosshairsBackground::Blueprint: return "Blueprint";
        case CrosshairsBackground::Paper:     return "Paper";
        default:                              return "(unknown)";
        }
    }
}

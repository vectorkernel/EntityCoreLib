#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "LineEntity.h"

namespace RenderCore
{
    // Cursor crosshair styling.
    // - GripsSelection: crosshair lines + small center square ("grips" picking)
    // - PointPick     : crosshair lines only (used for point entry)
    enum class CrosshairsMode
    {
        GripsSelection,
        PointPick,
        PickerOnly       // (box only)  <-- add this
    };

    // Demo-oriented background presets. The demo app can use this value
    // to set glClearColor() (and optionally to choose a contrasting crosshair color).
    enum class CrosshairsBackground
    {
        Dark,
        MidGray,
        Light,
        Blueprint,
        Paper,
        COUNT
    };

    struct CrosshairsColors
    {
        glm::vec4 clearColor{ 0.08f, 0.09f, 0.11f, 1.0f };
        glm::vec4 crosshairs{ 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec4 hudText{ 1.0f, 1.0f, 0.0f, 1.0f };
    };

    // Small, reusable crosshairs helper.
    // Output is screen-space (pixel) line segments in Y-up coordinates:
    //   origin = bottom-left, X right, Y up.
    class Crosshairs
    {
    public:
        void SetViewport(int w, int h);

        // Mouse in Win32 client coordinates (origin top-left, Y down)
        void SetMouseClient(int x, int y);

        void SetMode(CrosshairsMode m) { m_mode = m; }
        CrosshairsMode GetMode() const { return m_mode; }
        void ToggleMode();

        void SetBackground(CrosshairsBackground b) { m_bg = b; }
        CrosshairsBackground GetBackground() const { return m_bg; }
        void NextBackground();

        // Selection/grip pick box size (client pixels)
        void SetPickBoxSizePx(int px) { m_pickBoxPx = (px < 1) ? 1 : px; }
        int  GetPickBoxSizePx() const { return m_pickBoxPx; }

        // Returns preset colors for the current background.
        CrosshairsColors GetColors() const;

        // Build line segments for the crosshair overlay.
        // Clears and appends to outLines.
        void BuildLines(std::vector<LineEntity>& outLines) const;

        // Convenience strings for HUD.
        static const char* ModeName(CrosshairsMode m);
        static const char* BackgroundName(CrosshairsBackground b);

    private:
        int m_vpW = 1;
        int m_vpH = 1;

        // Win32 client coords
        int m_mouseX = 0;
        int m_mouseY = 0;

        CrosshairsMode m_mode = CrosshairsMode::GripsSelection;
        CrosshairsBackground m_bg = CrosshairsBackground::Dark;

        int m_pickBoxPx = 12;
    };
}

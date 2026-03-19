#pragma once

#include <glm/glm.hpp>

class PanZoomController;

namespace RenderCore
{
    // Small helper that turns a pixel-sized pick square into both:
    //  - client-space rect (Win32 coords)
    //  - world-space AABB + radius (for hit testing)
    struct PickBox
    {
        // Win32 client coords (origin top-left, y down), inclusive-ish bounds.
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;

        // World-space AABB
        glm::vec2 worldMin{ 0.0f, 0.0f };
        glm::vec2 worldMax{ 0.0f, 0.0f };
        glm::vec2 worldCenter{ 0.0f, 0.0f };

        // Approx world radius that corresponds to halfSizePx in X.
        float worldRadius = 0.0f;
    };

    // halfSizePx is half the side length in pixels (same concept as Crosshairs pick box).
    PickBox MakePickBox(const PanZoomController& pz,
                        int mouseX, int mouseY,
                        int halfSizePx,
                        int viewportW, int viewportH);
}

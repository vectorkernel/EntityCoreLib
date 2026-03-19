#include "pch.h"
#include "Picking.h"

#include "PanZoomController.h"

#include <algorithm>
#include <cmath>

namespace RenderCore
{
    PickBox MakePickBox(const PanZoomController& pz,
                        int mouseX, int mouseY,
                        int halfSizePx,
                        int viewportW, int viewportH)
    {
        PickBox pb;

        viewportW = std::max(1, viewportW);
        viewportH = std::max(1, viewportH);

        halfSizePx = std::max(1, halfSizePx);

        // Clamp mouse into viewport.
        mouseX = std::clamp(mouseX, 0, viewportW - 1);
        mouseY = std::clamp(mouseY, 0, viewportH - 1);

        pb.x0 = std::clamp(mouseX - halfSizePx, 0, viewportW - 1);
        pb.y0 = std::clamp(mouseY - halfSizePx, 0, viewportH - 1);
        pb.x1 = std::clamp(mouseX + halfSizePx, 0, viewportW - 1);
        pb.y1 = std::clamp(mouseY + halfSizePx, 0, viewportH - 1);

        // Convert corners to world.
        const glm::vec2 w00 = pz.ScreenToWorld(pb.x0, pb.y1); // bottom-left in client rect
        const glm::vec2 w11 = pz.ScreenToWorld(pb.x1, pb.y0); // top-right in client rect

        pb.worldMin = glm::vec2(std::min(w00.x, w11.x), std::min(w00.y, w11.y));
        pb.worldMax = glm::vec2(std::max(w00.x, w11.x), std::max(w00.y, w11.y));

        // Center
        const glm::vec2 wc = pz.ScreenToWorld(mouseX, mouseY);
        pb.worldCenter = wc;

        // Radius: measure world distance for a +halfSizePx step in X.
        const glm::vec2 wx = pz.ScreenToWorld(std::clamp(mouseX + halfSizePx, 0, viewportW - 1), mouseY);
        pb.worldRadius = std::abs(wx.x - wc.x);
        if (pb.worldRadius <= 0.0f)
        {
            // Fallback: use AABB extent.
            pb.worldRadius = 0.5f * std::max(pb.worldMax.x - pb.worldMin.x, pb.worldMax.y - pb.worldMin.y);
        }

        return pb;
    }
}

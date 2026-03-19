#include "pch.h"
#include "GripsOverlay.h"

#include "EntityBook.h"
#include "Entity.h"
#include "PanZoomController.h"

#include <algorithm>

namespace RenderCore
{
    static void AddSquareLines(float cx, float cy, float half,
                               const glm::vec4& color,
                               std::vector<OverlayVertex>& out)
    {
        const float x0 = cx - half;
        const float x1 = cx + half;
        const float y0 = cy - half;
        const float y1 = cy + half;

        // top
        out.push_back({ glm::vec3(x0, y1, 0.0f), color });
        out.push_back({ glm::vec3(x1, y1, 0.0f), color });
        // right
        out.push_back({ glm::vec3(x1, y1, 0.0f), color });
        out.push_back({ glm::vec3(x1, y0, 0.0f), color });
        // bottom
        out.push_back({ glm::vec3(x1, y0, 0.0f), color });
        out.push_back({ glm::vec3(x0, y0, 0.0f), color });
        // left
        out.push_back({ glm::vec3(x0, y0, 0.0f), color });
        out.push_back({ glm::vec3(x0, y1, 0.0f), color });
    }

    void BuildGripsOverlayLines(const EntityBook& book,
                               const std::unordered_set<std::size_t>& gripsIds,
                               const PanZoomController& pz,
                               int viewportW, int viewportH,
                               int halfSizePx,
                               const glm::vec4& gripColor,
                               std::vector<OverlayVertex>& outLines)
    {
        outLines.clear();

        if (viewportW <= 0 || viewportH <= 0) return;
        if (gripsIds.empty()) return;

        halfSizePx = std::max(1, halfSizePx);

        // Each selected LINE contributes two squares => 16 line segments => 32 vertices.
        outLines.reserve(gripsIds.size() * 32);

        for (const auto& e : book.entities)
        {
            if (e.type != EntityType::Line) continue;
            if (e.screenSpace) continue;
            if (!e.visible) continue;

            // only draw grips for ids in the grips selection set
            if (gripsIds.find(e.ID) == gripsIds.end())
                continue;

            // World -> client pixels (origin top-left, y down)
            const glm::vec2 s0 = pz.WorldToScreen(e.line.p0.x, e.line.p0.y);
            const glm::vec2 s1 = pz.WorldToScreen(e.line.p1.x, e.line.p1.y);

            // Convert to overlay coords (origin bottom-left, y up)
            const float ox0 = s0.x;
            const float oy0 = (float)viewportH - s0.y;

            const float ox1 = s1.x;
            const float oy1 = (float)viewportH - s1.y;

            AddSquareLines(ox0, oy0, (float)halfSizePx, gripColor, outLines);
            AddSquareLines(ox1, oy1, (float)halfSizePx, gripColor, outLines);
        }
    }
}

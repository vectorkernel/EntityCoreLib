#pragma once

#include <vector>
#include <unordered_set>
#include <cstddef>

#include <glm/glm.hpp>

class EntityBook;
class PanZoomController;

namespace RenderCore
{
    // Simple overlay vertex for GL_LINES.
    struct OverlayVertex
    {
        glm::vec3 pos;
        glm::vec4 color;
    };

    // Builds a screen-space grips overlay for the provided grips selection set.
    // - Output vertices are in *pixel coordinates* (origin bottom-left), suitable for drawing with an ortho MVP:
    //      glm::ortho(0, w, 0, h)
    // - Grips are constant-size squares in pixels (halfSizePx from center to edge).
    void BuildGripsOverlayLines(const EntityBook& book,
                               const std::unordered_set<std::size_t>& gripsIds,
                               const PanZoomController& pz,
                               int viewportW, int viewportH,
                               int halfSizePx,
                               const glm::vec4& gripColor,
                               std::vector<OverlayVertex>& outLines);
}

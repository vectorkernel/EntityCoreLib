#pragma once

#include <cstddef>
#include <unordered_set>

#include <glm/glm.hpp>

class EntityBook;
struct InteractionState;

namespace AppCore
{
    // Segment vs axis-aligned box (world units). Used by hover highlighting and rectangle selection.
    bool SegmentIntersectsAabb2D(const glm::vec2& a, const glm::vec2& b,
                                const glm::vec2& mn, const glm::vec2& mx);

    // Rebuilds InteractionState.hoveredIds from a world-space AABB.
    // Returns true if the hovered set changed.
    bool RebuildHoverSet(EntityBook& book, InteractionState& s,
                         const glm::vec2& worldMin, const glm::vec2& worldMax);
}

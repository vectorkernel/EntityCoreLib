#pragma once

#include <unordered_set>

#include <glm/glm.hpp>

#include "Entity.h"

namespace RenderCore
{
    // Centralized policy for how hover/selection affects line color.
    // Keeps per-app VBO upload code dumb (it just asks for the effective color).
    inline glm::vec4 EffectiveLineColor(const Entity& e,
                                       const std::unordered_set<std::size_t>* hoveredIds,
                                       const glm::vec4& highlight)
    {
        glm::vec4 c = e.line.color;

        if (!e.visible)
            return c;

        // Only user world-geometry is highlightable.
        if (!e.screenSpace && e.tag == EntityTag::User)
        {
            const bool hovered = hoveredIds && (hoveredIds->find(e.ID) != hoveredIds->end());
            if (e.selected || hovered)
                c = highlight;
        }

        return c;
    }
}

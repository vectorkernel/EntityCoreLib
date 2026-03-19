#include "pch.h"

#include "HoverOps.h"

#include <algorithm>
#include <cmath>

#include "EntityBook.h"
#include "InteractionState.h"
#include "Entity.h"

namespace AppCore
{
    bool SegmentIntersectsAabb2D(const glm::vec2& a, const glm::vec2& b,
                                const glm::vec2& mn, const glm::vec2& mx)
    {
        // Quick reject: segment bbox vs aabb
        glm::vec2 smin(std::min(a.x, b.x), std::min(a.y, b.y));
        glm::vec2 smax(std::max(a.x, b.x), std::max(a.y, b.y));
        if (smax.x < mn.x || smin.x > mx.x || smax.y < mn.y || smin.y > mx.y)
            return false;

        // Liang–Barsky style clip in 2D
        glm::vec2 d = b - a;
        float t0 = 0.0f, t1 = 1.0f;

        auto clip = [&](float p, float q) -> bool
        {
            if (std::abs(p) < 1e-8f) return q >= 0.0f;
            float r = q / p;
            if (p < 0.0f)
            {
                if (r > t1) return false;
                if (r > t0) t0 = r;
            }
            else
            {
                if (r < t0) return false;
                if (r < t1) t1 = r;
            }
            return true;
        };

        if (!clip(-d.x, a.x - mn.x)) return false;
        if (!clip( d.x, mx.x - a.x)) return false;
        if (!clip(-d.y, a.y - mn.y)) return false;
        if (!clip( d.y, mx.y - a.y)) return false;

        return true;
    }

    bool RebuildHoverSet(EntityBook& book, InteractionState& s,
                         const glm::vec2& worldMin, const glm::vec2& worldMax)
    {
        std::unordered_set<std::size_t> next;
        next.reserve(64);

        for (const auto& e : book.entities)
        {
            if (!e.visible) continue;
            if (!e.pickable) continue;
            if (e.type != EntityType::Line) continue;
            if (e.screenSpace) continue;
            if (e.tag != EntityTag::User) continue;

            glm::vec2 a(e.line.p0.x, e.line.p0.y);
            glm::vec2 b(e.line.p1.x, e.line.p1.y);
            if (SegmentIntersectsAabb2D(a, b, worldMin, worldMax))
                next.insert(e.ID);
        }

        if (next == s.hoveredIds)
            return false;

        s.hoveredIds.swap(next);
        return true;
    }
}

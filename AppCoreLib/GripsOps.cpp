#include "pch.h"

#include "GripsOps.h"
#include "InteractionState.h"
#include "EntityBook.h"
#include "Entity.h"
#include "HoverOps.h" // SegmentIntersectsAabb2D

#include <unordered_set>

namespace AppCore
{
    void ClearGrips(InteractionState& s)
    {
        if (s.gripsIds.empty())
            return;

        s.gripsIds.clear();
        s.requestRedraw = true;
    }

    bool ApplyGripsFromAabb(EntityBook& book, InteractionState& s,
                            const glm::vec2& worldMin, const glm::vec2& worldMax,
                            GripsApplyMode mode)
    {
        std::unordered_set<std::size_t> hits;
        hits.reserve(64);

        for (const auto& e : book.entities)
        {
            if (!e.visible) continue;
            if (!e.pickable) continue;
            if (e.type != EntityType::Line) continue;
            if (e.screenSpace) continue;
            if (e.tag != EntityTag::User) continue;

            glm::vec2 a(e.line.p0.x, e.line.p0.y);
            glm::vec2 b(e.line.p1.x, e.line.p1.y);
            if (AppCore::SegmentIntersectsAabb2D(a, b, worldMin, worldMax))
                hits.insert(e.ID);
        }

        if (hits.empty() && mode != GripsApplyMode::Replace)
            return false;

        auto before = s.gripsIds;

        switch (mode)
        {
        case GripsApplyMode::Replace:
            s.gripsIds = std::move(hits);
            break;
        case GripsApplyMode::Add:
            for (auto id : hits) s.gripsIds.insert(id);
            break;
        case GripsApplyMode::Toggle:
            for (auto id : hits)
            {
                if (s.gripsIds.find(id) != s.gripsIds.end()) s.gripsIds.erase(id);
                else s.gripsIds.insert(id);
            }
            break;
        }

        const bool changed = (before != s.gripsIds);
        if (changed)
            s.requestRedraw = true;
        return changed;
    }
}

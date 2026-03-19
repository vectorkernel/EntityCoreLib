#include "pch.h"

#include "GridUtil.h"

#include <algorithm>
#include <cmath>

#include "EntityBook.h"
#include "Entity.h"
#include "PanZoomController.h"

namespace RenderCore
{
    static void ComputeWorldBounds(const PanZoomController& pz, int w, int h,
                                   glm::vec2& outMin, glm::vec2& outMax)
    {
        glm::vec2 w00 = pz.ScreenToWorld(0, 0);
        glm::vec2 w10 = pz.ScreenToWorld(w, 0);
        glm::vec2 w01 = pz.ScreenToWorld(0, h);
        glm::vec2 w11 = pz.ScreenToWorld(w, h);

        outMin.x = std::min(std::min(w00.x, w10.x), std::min(w01.x, w11.x));
        outMin.y = std::min(std::min(w00.y, w10.y), std::min(w01.y, w11.y));
        outMax.x = std::max(std::max(w00.x, w10.x), std::max(w01.x, w11.x));
        outMax.y = std::max(std::max(w00.y, w10.y), std::max(w01.y, w11.y));
    }

    bool UpdateBackgroundGrid(EntityBook& book,
                              const PanZoomController& pz,
                              int viewportW, int viewportH,
                              std::size_t& nextId,
                              GridCache& cache,
                              float spacingWorld,
                              int majorEvery)
    {
        if (viewportW <= 0 || viewportH <= 0)
            return false;
        if (spacingWorld <= 1e-6f)
            return false;

        glm::vec2 mn{}, mx{};
        ComputeWorldBounds(pz, viewportW, viewportH, mn, mx);

        // Build a small pad so panning doesn't expose gaps.
        const float pad = spacingWorld * 2.0f;
        mn -= glm::vec2(pad, pad);
        mx += glm::vec2(pad, pad);

        const int ix0 = (int)std::floor(mn.x / spacingWorld);
        const int ix1 = (int)std::ceil (mx.x / spacingWorld);
        const int iy0 = (int)std::floor(mn.y / spacingWorld);
        const int iy1 = (int)std::ceil (mx.y / spacingWorld);

        const bool same = cache.valid &&
                          cache.ix0 == ix0 && cache.ix1 == ix1 &&
                          cache.iy0 == iy0 && cache.iy1 == iy1 &&
                          std::abs(cache.spacing - spacingWorld) < 1e-6f &&
                          cache.majorEvery == majorEvery;

        if (same)
            return false;

        cache.valid = true;
        cache.ix0 = ix0; cache.ix1 = ix1;
        cache.iy0 = iy0; cache.iy1 = iy1;
        cache.spacing = spacingWorld;
        cache.majorEvery = majorEvery;

        // Remove prior grid entities.
        book.RemoveIf([](const Entity& e) { return e.tag == EntityTag::Grid; });

        auto addLine = [&](glm::vec3 a, glm::vec3 b, const glm::vec4& color)
        {
            Entity e;
            e.ID = nextId++;
            e.type = EntityType::Line;
            e.tag = EntityTag::Grid;
            e.drawOrder = 0;
            e.screenSpace = false;
            e.pickable = false;
            e.visible = true;
            e.selected = false;
            e.line.p0 = a;
            e.line.p1 = b;
            e.line.color = color;
            e.line.width = 1.0f;
            book.AddEntity(e);
        };

        auto isMajor = [&](int i) -> bool
        {
            if (majorEvery <= 1) return true;
            const int m = std::abs(i) % majorEvery;
            return m == 0;
        };

        const glm::vec4 minor(0.20f, 0.22f, 0.26f, 1.0f);
        const glm::vec4 major(0.30f, 0.32f, 0.36f, 1.0f);

        const float y0 = (float)iy0 * spacingWorld;
        const float y1 = (float)iy1 * spacingWorld;
        const float x0 = (float)ix0 * spacingWorld;
        const float x1 = (float)ix1 * spacingWorld;

        for (int ix = ix0; ix <= ix1; ++ix)
        {
            const float x = (float)ix * spacingWorld;
            addLine({ x, y0, 0.0f }, { x, y1, 0.0f }, isMajor(ix) ? major : minor);
        }

        for (int iy = iy0; iy <= iy1; ++iy)
        {
            const float y = (float)iy * spacingWorld;
            addLine({ x0, y, 0.0f }, { x1, y, 0.0f }, isMajor(iy) ? major : minor);
        }

        // Axis lines (more visible)
        addLine({ x0, 0.0f, 0.0f }, { x1, 0.0f, 0.0f }, { 0.70f, 0.20f, 0.20f, 1.0f });
        addLine({ 0.0f, y0, 0.0f }, { 0.0f, y1, 0.0f }, { 0.20f, 0.70f, 0.20f, 1.0f });

        book.SortByDrawOrder();
        return true;
    }
}

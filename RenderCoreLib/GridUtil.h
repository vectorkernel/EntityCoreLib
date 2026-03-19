#pragma once

#include <cstddef>

#include <glm/glm.hpp>

class EntityBook;
class PanZoomController;

namespace RenderCore
{
    struct GridCache
    {
        // Cache derived from camera + spacing so we only rebuild when the view crosses a grid cell.
        int ix0 = 0, ix1 = 0;
        int iy0 = 0, iy1 = 0;
        float spacing = 0.0f;
        int majorEvery = 5;
        bool valid = false;
    };

    // Maintains a view-aligned infinite background grid (EntityTag::Grid, pickable=false).
    // Returns true if any grid entities were rebuilt.
    bool UpdateBackgroundGrid(EntityBook& book,
                              const PanZoomController& pz,
                              int viewportW, int viewportH,
                              std::size_t& nextId,
                              GridCache& cache,
                              float spacingWorld = 50.0f,
                              int majorEvery = 5);
}

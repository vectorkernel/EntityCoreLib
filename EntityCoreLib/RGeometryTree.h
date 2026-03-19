#pragma once
#include <vector>
#include <cstddef>
#include <optional>

#include <boost/geometry.hpp>
#include <boost/geometry/index/rtree.hpp>

#include "BoundingBox.h"

namespace bgi = boost::geometry::index;

class RGeometryTree
{
public:
    void Clear();
    void Build(const std::vector<std::pair<BoundingBox, std::size_t>>& items);

    // Query with an AABB (picker square in world space). Returns the first hit (best-effort).
    std::optional<std::size_t> QueryFirstIntersect(const BoundingBox& box) const;

private:
    using Value = std::pair<BoundingBox, std::size_t>;
    bgi::rtree<Value, bgi::quadratic<16>> m_tree;
};


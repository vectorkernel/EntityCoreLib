#include "pch.h"

#include "RGeometryTree.h"

void RGeometryTree::Clear()
{
    m_tree.clear();
}

void RGeometryTree::Build(const std::vector<std::pair<BoundingBox, std::size_t>>& items)
{
    m_tree = bgi::rtree<Value, bgi::quadratic<16>>(items.begin(), items.end());
}

std::optional<std::size_t> RGeometryTree::QueryFirstIntersect(const BoundingBox& box) const
{
    std::vector<Value> out;
    m_tree.query(bgi::intersects(box), std::back_inserter(out));
    if (out.empty())
        return std::nullopt;
    return out.front().second;
}


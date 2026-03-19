#pragma once
#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point.hpp>
#include <boost/geometry/geometries/box.hpp>
#include <boost/geometry/index/rtree.hpp>

namespace bg = boost::geometry;

struct BoundingBox {
    float minX, minY, minZ;
    float maxX, maxY, maxZ;

    BoundingBox() : minX(0), minY(0), minZ(0), maxX(0), maxY(0), maxZ(0) {}
    BoundingBox(float minX, float minY, float minZ, float maxX, float maxY, float maxZ)
        : minX(minX), minY(minY), minZ(minZ), maxX(maxX), maxY(maxY), maxZ(maxZ) {
    }
};

namespace boost::geometry::traits {
    template<> struct tag<BoundingBox> { using type = box_tag; };
    template<> struct point_type<BoundingBox> { using type = model::point<float, 3, cs::cartesian>; };
    template<> struct indexed_access<BoundingBox, min_corner, 0> {
        static float get(BoundingBox const& b) { return b.minX; }
        static void set(BoundingBox& b, float value) { b.minX = value; }
    };
    template<> struct indexed_access<BoundingBox, min_corner, 1> {
        static float get(BoundingBox const& b) { return b.minY; }
        static void set(BoundingBox& b, float value) { b.minY = value; }
    };
    template<> struct indexed_access<BoundingBox, min_corner, 2> {
        static float get(BoundingBox const& b) { return b.minZ; }
        static void set(BoundingBox& b, float value) { b.minZ = value; }
    };
    template<> struct indexed_access<BoundingBox, max_corner, 0> {
        static float get(BoundingBox const& b) { return b.maxX; }
        static void set(BoundingBox& b, float value) { b.maxX = value; }
    };
    template<> struct indexed_access<BoundingBox, max_corner, 1> {
        static float get(BoundingBox const& b) { return b.maxY; }
        static void set(BoundingBox& b, float value) { b.maxY = value; }
    };
    template<> struct indexed_access<BoundingBox, max_corner, 2> {
        static float get(BoundingBox const& b) { return b.maxZ; }
        static void set(BoundingBox& b, float value) { b.maxZ = value; }
    };
}


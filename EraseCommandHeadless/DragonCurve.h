// DragonCurve.h
#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

#ifndef DRAGON_LEG_UNITS
// One leg length in WORLD units (same coordinate space as your grid/scene).
// Tweak this to scale the curve without changing iteration count.
#define DRAGON_LEG_UNITS 6.0f
#endif

struct DragonSegment
{
    glm::vec3 a;
    glm::vec3 b;
};

class DragonCurve
{
public:
    // Build a single dragon curve polyline as line segments in WORLD space.
    // iterations controls complexity: segments = 2^iterations.
    // origin is the starting point.
    std::vector<DragonSegment> Build(int iterations, const glm::vec3& origin) const;

private:
    // Generates the standard dragon "turn" sequence:
    // next = prev + [1] + invert(reverse(prev))
    // where 1 means "turn right", 0 means "turn left".
    std::vector<uint8_t> GenerateTurns(int iterations) const;
};

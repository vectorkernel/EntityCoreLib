// DragonCurve.cpp
#include "DragonCurve.h"

static inline uint8_t InvertBit(uint8_t b) { return b ? 0u : 1u; }

std::vector<uint8_t> DragonCurve::GenerateTurns(int iterations) const
{
    // iterations <= 0 => empty
    if (iterations <= 0)
        return {};

    // For a dragon curve, the turn sequence length is (2^iterations - 1).
    // Start seed: [1]
    std::vector<uint8_t> turns;
    turns.reserve((1u << iterations) - 1u);
    turns.push_back(1u);

    for (int i = 1; i < iterations; ++i)
    {
        std::vector<uint8_t> next;
        next.reserve((turns.size() * 2) + 1);

        // prev
        next.insert(next.end(), turns.begin(), turns.end());

        // + [1]
        next.push_back(1u);

        // + invert(reverse(prev))
        for (auto it = turns.rbegin(); it != turns.rend(); ++it)
            next.push_back(InvertBit(*it));

        turns.swap(next);
    }

    return turns;
}

std::vector<DragonSegment> DragonCurve::Build(int iterations, const glm::vec3& origin) const
{
    std::vector<DragonSegment> segs;

    // The polyline has 2^iterations segments.
    if (iterations <= 0)
        return segs;

    const uint32_t segmentCount = (1u << iterations);
    segs.reserve(segmentCount);

    const auto turns = GenerateTurns(iterations);

    // Coordinate system: WORLD space with Y growing downward in this app.
    // We'll use directions:
    // 0=E(+x), 1=S(+y), 2=W(-x), 3=N(-y)
    int dir = 0;

    glm::vec3 p = origin;

    auto stepForward = [&](int d) -> glm::vec3
    {
        switch (d & 3)
        {
        default:
        case 0: return glm::vec3(p.x + DRAGON_LEG_UNITS, p.y, 0.0f); // E
        case 1: return glm::vec3(p.x, p.y + DRAGON_LEG_UNITS, 0.0f); // S
        case 2: return glm::vec3(p.x - DRAGON_LEG_UNITS, p.y, 0.0f); // W
        case 3: return glm::vec3(p.x, p.y - DRAGON_LEG_UNITS, 0.0f); // N
        }
    };

    // First segment: go forward once (no turn yet)
    {
        glm::vec3 q = stepForward(dir);
        segs.push_back({ p, q });
        p = q;
    }

    // Then for each turn bit: turn, then step forward
    for (uint8_t t : turns)
    {
        // 1 = right turn, 0 = left turn
        if (t) dir = (dir + 1) & 3;
        else   dir = (dir + 3) & 3;

        glm::vec3 q = stepForward(dir);
        segs.push_back({ p, q });
        p = q;
    }

    return segs;
}

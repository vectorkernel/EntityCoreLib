// LineEntity.h
#pragma once
#include <glm/glm.hpp>

// A single line segment to be drawn by the line pass.
//
// NOTE: Some call sites expect members named start/end/thickness,
// while others use p0/p1/width. We provide both names as aliases.
struct LineEntity
{
    union { glm::vec3 p0; glm::vec3 start; };
    union { glm::vec3 p1; glm::vec3 end; };

    glm::vec4 color{ 1.0f };

    union { float width; float thickness; };

    LineEntity()
        : p0(0.0f)
        , p1(0.0f)
        , color(1.0f)
        , width(1.0f)
    {
    }
};


// SolidRectEntity.h
#pragma once
#include <glm/glm.hpp>

// Filled axis-aligned rectangle. The rectangle is defined by min/max corners
// in the entity's coordinate space (world units or screen pixels depending on Entity::screenSpace).
struct SolidRectEntity
{
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
    glm::vec4 color{0.0f, 0.0f, 0.0f, 1.0f};
};

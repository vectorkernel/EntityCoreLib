#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include "EntityType.h"
#include "LineEntity.h"
#include "TextEntity.h"
#include "SolidRectEntity.h"


struct Entity
{
    std::size_t ID = 0;

    EntityType type = EntityType::Line;
    EntityTag  tag = EntityTag::Scene;

    bool screenSpace = false;
	bool pickable = true;
	bool visible = true;
    bool selected = false;

    int drawOrder = 0;


    uint32_t layerId = 0;
    bool colorByLayer = true;
    bool linetypeByLayer = true;
    std::string linetypeOverride = "Continuous";

    LineEntity line{};
    TextEntity text{};
    SolidRectEntity solidRect{};
};
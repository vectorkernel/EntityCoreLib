// SceneJsonIO.h
#pragma once

#include <string>
#include <vector>
#include "Entity.h"

// Simple JSON save/load helpers for the drawing entity list.
// Uses JsonCpp (vcpkg: jsoncpp).
namespace SceneJsonIO
{
    // Writes entities to a JSON file. Returns true on success.
    // By default, callers should pass only "scene" entities (tag == EntityTag::Scene).
    bool SaveEntitiesToFile(const std::string& filename, const std::vector<Entity>& entities, int formatVersion = 1);
}

// SceneJsonIO.cpp
#include "pch.h"
#include "SceneJsonIO.h"

#include <fstream>
#include <json/json.h>

#include <glm/glm.hpp>

static Json::Value Vec3ToJson(const glm::vec3& v)
{
    Json::Value a(Json::arrayValue);
    a.append(v.x);
    a.append(v.y);
    a.append(v.z);
    return a;
}

static Json::Value Vec4ToJson(const glm::vec4& v)
{
    Json::Value a(Json::arrayValue);
    a.append(v.x);
    a.append(v.y);
    a.append(v.z);
    a.append(v.w);
    return a;
}

static const char* EntityTypeToString(EntityType t)
{
    switch (t)
    {
    case EntityType::Line:      return "Line";
    case EntityType::Text:      return "Text";
    case EntityType::SolidRect: return "SolidRect";
    default:                    return "Unknown";
    }
}

static const char* EntityTagToString(EntityTag t)
{
    switch (t)
    {
    case EntityTag::Grid:   return "Grid";
    case EntityTag::Scene:  return "Scene";
    case EntityTag::Cursor: return "Cursor";
    case EntityTag::Hud:    return "Hud";
    default:                return "Unknown";
    }
}

static const char* TextHAlignToString(TextHAlign a)
{
    switch (a)
    {
    case TextHAlign::Left:   return "Left";
    case TextHAlign::Center: return "Center";
    case TextHAlign::Right:  return "Right";
    default:                 return "Left";
    }
}

bool SceneJsonIO::SaveEntitiesToFile(const std::string& filename, const std::vector<Entity>& entities, int formatVersion)
{
    Json::Value root(Json::objectValue);
    root["format"] = "VectorKernel.Entities";
    root["version"] = formatVersion;

    Json::Value arr(Json::arrayValue);

    for (const auto& e : entities)
    {
        Json::Value je(Json::objectValue);

        // Common fields
        je["id"] = (Json::UInt64)e.ID;
        je["type"] = EntityTypeToString(e.type);
        je["tag"] = EntityTagToString(e.tag);
        je["screenSpace"] = e.screenSpace;
        je["drawOrder"] = e.drawOrder;

        je["layerId"] = e.layerId;
        je["colorByLayer"] = e.colorByLayer;
        je["linetypeByLayer"] = e.linetypeByLayer;
        je["linetypeOverride"] = e.linetypeOverride;

        // Payload by type
        switch (e.type)
        {
        case EntityType::Line:
            je["line"]["p0"] = Vec3ToJson(e.line.p0);
            je["line"]["p1"] = Vec3ToJson(e.line.p1);
            je["line"]["color"] = Vec4ToJson(e.line.color);
            je["line"]["width"] = e.line.width;
            break;

        case EntityType::Text:
            je["text"]["text"] = e.text.text;
            je["text"]["position"] = Vec3ToJson(e.text.position);
            je["text"]["boxWidth"] = e.text.boxWidth;
            je["text"]["boxHeight"] = e.text.boxHeight;
            je["text"]["wordWrapEnabled"] = e.text.wordWrapEnabled;
            je["text"]["hAlign"] = TextHAlignToString(e.text.hAlign);
            je["text"]["scale"] = e.text.scale;
            je["text"]["strokeWidth"] = e.text.strokeWidth;
            je["text"]["color"] = Vec4ToJson(e.text.color);
            je["text"]["size"] = e.text.size;

            je["text"]["backgroundEnabled"] = e.text.backgroundEnabled;
            je["text"]["backgroundColor"] = Vec4ToJson(e.text.backgroundColor);
            je["text"]["backgroundPadding"] = e.text.backgroundPadding;
            break;

        case EntityType::SolidRect:
            je["solidRect"]["min"] = Vec3ToJson(e.solidRect.min);
            je["solidRect"]["max"] = Vec3ToJson(e.solidRect.max);
            je["solidRect"]["color"] = Vec4ToJson(e.solidRect.color);
            break;
        }

        arr.append(je);
    }

    root["entities"] = arr;

    Json::StreamWriterBuilder b;
    b["indentation"] = "  "; // pretty

    std::ofstream out(filename, std::ios::out | std::ios::trunc);
    if (!out.is_open())
        return false;

    std::unique_ptr<Json::StreamWriter> w(b.newStreamWriter());
    w->write(root, &out);
    out << "\n";
    return true;
}

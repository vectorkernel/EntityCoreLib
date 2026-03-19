// TextEntity.h
#pragma once
#include <string>
#include <glm/glm.hpp>

struct hershey_font;

enum class TextHAlign
{
    Left,
    Center,
    Right
};

// Declarative text entity. HersheyTextBuilder converts this to line entities.
struct TextEntity
{
    // Text content
    std::string text;

    // World-space anchor (your builder uses "position")
    glm::vec3 position{ 0.0f };

    // Word-wrap box size (your builder uses these exact names)
    float boxWidth = 0.0f;
    float boxHeight = 0.0f;

    bool wordWrapEnabled = true;

    // Font pointer (builder expects hershey_font*)
    hershey_font* font = nullptr;

    // Horizontal alignment (both names are supported by different call sites)
    union { TextHAlign hAlign; TextHAlign align; };

    // Scale and stroke controls used by builder
    float scale = 1.0f;
    float strokeWidth = 1.0f;

    // Color applied to emitted line entities
    glm::vec4 color{ 1.0f };

    // Optional extra knob (safe default; keep for compatibility)
    float size = 1.0f;


    // Optional background "wipeout" rectangle behind the text.
    // Units match the entity coordinate space (world units or screen pixels).
    bool backgroundEnabled = false;
    glm::vec4 backgroundColor{ 0.0f, 0.0f, 0.0f, 1.0f };
    float backgroundPadding = 6.0f;

    // Optional outline around the background rectangle (drawn with lines).
    bool backgroundOutlineEnabled = false;
    glm::vec4 backgroundOutlineColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    float backgroundOutlineThickness = 1.0f;

    TextEntity()
        : text()
        , position(0.0f)
        , boxWidth(0.0f)
        , boxHeight(0.0f)
        , wordWrapEnabled(true)
        , font(nullptr)
        , hAlign(TextHAlign::Left)
        , scale(1.0f)
        , strokeWidth(1.0f)
        , color(1.0f)
        , size(1.0f)
    {
    }
};


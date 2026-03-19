#pragma once
#include <string>
#include <vector>
#include "TextEntity.h"
#include "LineEntity.h"

// Converts a TextEntity into a list of LineEntity segments (GL_LINES).
// Output is appended to outLines.
namespace HersheyTextBuilder
{
    // Measure a single-line string width in world units (no wrapping).
    float MeasureTextWidth(const std::string& s, hershey_font* font, float scale);

    // Split + wrap into lines (respects '\n' and TextEntity.wordWrapEnabled/boxWidth).
    std::vector<std::string> BuildTextLines(const TextEntity& text);

    // Expand the text into line segments.
    void BuildLines(const TextEntity& text, std::vector<LineEntity>& outLines);
}


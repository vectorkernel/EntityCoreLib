#pragma once

#include <glm/glm.hpp>

// Centralized, easily-tweakable render/UI colors.
//
// These are intended as simple globals for now (starter kit), so that the
// background and cursor/crosshair colors can be adjusted in one place.
struct RenderStyle
{
    // Background clear colors (RGB in 0..1, alpha ignored).
    glm::vec4 modelBackground   = glm::vec4(0.07f, 0.07f, 0.08f, 1.0f);
    glm::vec4 paperBackground   = glm::vec4(0.35f, 0.35f, 0.35f, 1.0f);
    glm::vec4 viewportBackground = glm::vec4(0.07f, 0.07f, 0.08f, 1.0f);

    // Cursor/crosshair colors.
    glm::vec4 modelCrosshairs    = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    glm::vec4 paperCrosshairs    = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    glm::vec4 viewportCrosshairs = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
};

// Global style instance.
extern RenderStyle g_renderStyle;

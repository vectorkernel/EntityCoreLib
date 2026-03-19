#pragma once
#pragma once

struct PageSettings
{
    // Defaults: US Letter in inches
    float widthIn = 8.5f;
    float heightIn = 11.0f;

    // Printable margins (inches)
    float marginLeftIn = 0.25f;
    float marginRightIn = 0.25f;
    float marginTopIn = 0.25f;
    float marginBottomIn = 0.25f;

    // Preview/output resolution intent (pixels per inch). X/Y can differ.
    float dpiX = 300.0f;
    float dpiY = 300.0f;
};

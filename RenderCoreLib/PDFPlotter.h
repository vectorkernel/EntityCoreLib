// PDFPlotter.h
#pragma once

#include <string>
#include <vector>

#include "Entity.h"
#include "PageSettings.h"

// Minimal, dependency-free PDF writer for VectorKernel.
//
// - Produces a single-page PDF.
// - Exports vector line segments (and Hershey text expanded into line segments).
// - Coordinates are assumed to be in paper-space inches with origin at top-left.
// - Output PDF uses points (1/72 inch) with origin at bottom-left.
//
// This is intentionally a small foundation that we can grow over time
// (layers, viewports, multiple pages, etc.).
class PDFPlotter
{
public:
    // Writes a single-page PDF.
    // Returns true on success.
    static bool Write(const std::string& outPath,
        const std::vector<Entity>& entities,
        const PageSettings& page);
};

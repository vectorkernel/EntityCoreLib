#pragma once

#include "EntityBook.h"
#include "LayerTable.h"
#include "LinePass.h"
#include "RenderContext.h"

#include <vector>
#include <cstdint>
#include <string>

class StatefulVectorRenderer
{
public:
    void Init();

    void SetEntityBook(const EntityBook* book);
    void SetLayerTable(const LayerTable* layers) { layerTable = layers; dirty = true; }
    void SetTagMask(uint32_t mask) { tagMask = mask; dirty = true; }
    void MarkDirty();

    // Draw cached/static batches
    void Redraw(const RenderContext& ctx);
    // Draw selection highlight overlay (always-on-top)
    void DrawSelectionOverlay(const RenderContext& ctx);

private:
    void RebuildBatchesIfDirty();

private:
    const EntityBook* entityBook = nullptr;
    const LayerTable* layerTable = nullptr;
    bool dirty = true;
    uint32_t tagMask = 0xFFFFFFFFu;

    // Cache invalidation: rebuild only when EntityBook revision changes.
    uint64_t lastSeenRevision = 0;
    std::string lastSeenReason;

    // We batch everything into lines for now (lines + text -> line segments)
    std::vector<LineEntity> cachedWorldLines;
    std::vector<LineEntity> cachedSelectedLines;

    LinePass worldPass;
    LinePass selectedPass;
};

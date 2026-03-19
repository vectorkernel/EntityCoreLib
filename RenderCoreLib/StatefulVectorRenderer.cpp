// StatefulVectorRenderer.cpp
#include "pch.h"
#include "StatefulVectorRenderer.h"

#include "HersheyTextBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include "Logger.h"

void StatefulVectorRenderer::Init()
{
    worldPass.Init();
    selectedPass.Init();
}

void StatefulVectorRenderer::SetEntityBook(const EntityBook* book)
{
    entityBook = book;
    dirty = true;
    lastSeenRevision = 0;
    lastSeenReason.clear();
}

void StatefulVectorRenderer::MarkDirty()
{
    dirty = true;
    // Force rebuild next time even if revision is unchanged.
    lastSeenRevision = 0;
    lastSeenReason.clear();
}

static inline bool StartsWith(const std::string& s, const char* prefix)
{
    if (!prefix) return false;
    const size_t n = std::strlen(prefix);
    return s.size() >= n && std::memcmp(s.data(), prefix, n) == 0;
}

void StatefulVectorRenderer::RebuildBatchesIfDirty()
{
    if (!dirty || !entityBook)
        return;

    cachedWorldLines.clear();
    cachedSelectedLines.clear();

    const auto& entities = entityBook->GetEntities();
    const std::string reason = entityBook->GetLastTouchReason() ? entityBook->GetLastTouchReason() : "";

    // Only print pipeline-level logs on selection mutations (no hover spam by default).
    const bool selectionEvent =
        (reason == "ApplySelection") ||
        (reason == "ClearSelection") ||
        (reason == "AddToSelection");

    const bool wantTrace = VKLog::Enabled(VKLog::Selection, VKLog::Level::Trace);
    const bool wantInfo  = VKLog::Enabled(VKLog::Selection, VKLog::Level::Info);

    const glm::vec4 kSelColor(1.0f, 1.0f, 0.0f, 1.0f);
    const float     kSelWidthPx = 3.0f;

    size_t worldSegments = 0;
    size_t overlaySegments = 0;

    // Counts of what the renderer actually sees selected (after filters).
    size_t selEntitiesSeen = 0;
    size_t selLine = 0, selText = 0, selRect = 0;

    auto emitRectOutline = [&](const SolidRectEntity& r)
    {
        // Draw a rectangle outline as 4 line segments. Keep Z stable.
        const float z = r.min.z;
        const glm::vec3 p00(r.min.x, r.min.y, z);
        const glm::vec3 p10(r.max.x, r.min.y, z);
        const glm::vec3 p11(r.max.x, r.max.y, z);
        const glm::vec3 p01(r.min.x, r.max.y, z);

        LineEntity a; a.p0 = p00; a.p1 = p10; a.color = kSelColor; a.width = kSelWidthPx;
        LineEntity b; b.p0 = p10; b.p1 = p11; b.color = kSelColor; b.width = kSelWidthPx;
        LineEntity c; c.p0 = p11; c.p1 = p01; c.color = kSelColor; c.width = kSelWidthPx;
        LineEntity d; d.p0 = p01; d.p1 = p00; d.color = kSelColor; d.width = kSelWidthPx;

        cachedSelectedLines.push_back(a);
        cachedSelectedLines.push_back(b);
        cachedSelectedLines.push_back(c);
        cachedSelectedLines.push_back(d);
        overlaySegments += 4;
    };

    for (const Entity& e : entities)
    {
        // HUD / overlay entities are drawn via RenderLoopRenderer (immediate mode),
        // not cached here.
        if (e.screenSpace)
            continue;

        const uint32_t tagBit = 1u << (uint32_t)e.tag;
        if ((tagMask & tagBit) == 0)
            continue;

        if ((e.tag == EntityTag::Scene || e.tag == EntityTag::Paper || e.tag == EntityTag::PaperUser) &&
            layerTable && !layerTable->IsLayerVisible(e.layerId))
            continue;

        if (e.selected)
        {
            ++selEntitiesSeen;
            switch (e.type)
            {
            case EntityType::Line:      ++selLine; break;
            case EntityType::Text:      ++selText; break;
            case EntityType::SolidRect: ++selRect; break;
            default: break;
            }
        }

        if (e.type == EntityType::Line)
        {
            cachedWorldLines.push_back(e.line);
            ++worldSegments;

            if (e.selected)
            {
                LineEntity s = e.line;
                s.color = kSelColor;
                s.width = std::max(s.width, kSelWidthPx);
                cachedSelectedLines.push_back(s);
                ++overlaySegments;
            }
        }
        else if (e.type == EntityType::Text)
        {
            const size_t before = cachedWorldLines.size();
            HersheyTextBuilder::BuildLines(e.text, cachedWorldLines);
            worldSegments += (cachedWorldLines.size() - before);

            if (e.selected)
            {
                TextEntity t = e.text;
                t.color = kSelColor;
                t.strokeWidth = std::max(t.strokeWidth, kSelWidthPx);

                const size_t beforeSel = cachedSelectedLines.size();
                HersheyTextBuilder::BuildLines(t, cachedSelectedLines);
                overlaySegments += (cachedSelectedLines.size() - beforeSel);
            }
        }
        else if (e.type == EntityType::SolidRect)
        {
            // SolidRects are drawn by rect passes elsewhere; for selection overlay,
            // draw an outline rectangle as lines.
            if (e.selected)
                emitRectOutline(e.solidRect);
        }
    }

    worldPass.BuildStatic(cachedWorldLines);
    selectedPass.BuildStatic(cachedSelectedLines);

    dirty = false;

    if ((selectionEvent && wantInfo) || wantTrace)
    {
        VKLog::Logf(VKLog::Selection, VKLog::Level::Info,
            "[SEL][RenderRebuild] reason=%s worldLines=%zu overlayLines=%zu selEntitiesSeen=%zu types(L=%zu T=%zu R=%zu)",
            reason.c_str(),
            cachedWorldLines.size(),
            cachedSelectedLines.size(),
            selEntitiesSeen,
            selLine, selText, selRect);
    }
}

void StatefulVectorRenderer::Redraw(const RenderContext& ctx)
{
    if (entityBook)
    {
        const uint64_t rev = entityBook->GetRevision();
        const std::string reason = entityBook->GetLastTouchReason() ? entityBook->GetLastTouchReason() : "";

        // Rebuild only when entity book changes. This eliminates per-frame rebuild spam.
        if (rev != lastSeenRevision)
        {
            dirty = true;
            lastSeenRevision = rev;
            lastSeenReason = reason;
        }
    }

    RebuildBatchesIfDirty();

    // World pass uses Application model/view/projection
    worldPass.DrawStatic(ctx);
}

void StatefulVectorRenderer::DrawSelectionOverlay(const RenderContext& ctx)
{
    selectedPass.DrawStatic(ctx);
}

#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_set>

#include "EntityBook.h" // INTENTIONAL: "most intelligent" = InteractionState can inline-call model APIs.

// Forward declare to avoid coupling AppCoreLib to RenderCoreLib
namespace RenderCore
{
    class SelectionWindow;
}

struct InteractionState
{
    // -------------------------------------------------
    // Active Tool (high-level user intent)
    // -------------------------------------------------
    enum class Tool
    {
        Select,
        SelectGrips,
        Erase,
        Pan,
        Zoom,
        // grow later
    };

    Tool tool = Tool::SelectGrips;

    // -------------------------------------------------
    // Input / Gesture Phase
    // -------------------------------------------------
    enum class Phase
    {
        Idle,
        Dragging,
        RectSelecting,
        // grow later
    };

    Phase phase = Phase::Idle;

    // -------------------------------------------------
    // Mouse state
    // -------------------------------------------------
    int mouseX = 0;
    int mouseY = 0;

    // -------------------------------------------------
    // Rectangle selection state (tool-agnostic)
    // -------------------------------------------------
    enum class RectMode
    {
        None,
        Window,
        Crossing
    };

    RectMode rectMode = RectMode::None;

    bool waitingForSecondClick = false; // two-click rectangle flow

    // Optional pointer/reference owned by application
    // (InteractionState does NOT own it — just references usage)
    RenderCore::SelectionWindow* selectionWindow = nullptr;

    // -------------------------------------------------
    // Hover highlight (transient)
    // -------------------------------------------------
    std::unordered_set<std::size_t> hoveredIds;

    // -------------------------------------------------
    // Grips selection set (separate from model selection)
    // -------------------------------------------------
    std::unordered_set<std::size_t> gripsIds;

    // -------------------------------------------------
    // Cursor intent (read by Crosshairs system)
    // -------------------------------------------------
    bool showCrosshairs = true;
    bool showSystemCursor = false;

    // -------------------------------------------------
    // Rendering / scene invalidation
    // -------------------------------------------------
    bool sceneDirty = true;     // geometry or selection colors changed, rebuild GPU buffers
    bool requestRedraw = false; // just redraw frame

    // -------------------------------------------------
    // Command statistics (optional HUD)
    // -------------------------------------------------
    int lastErased = 0;
    int totalErased = 0;

    // -------------------------------------------------
    // Utility helpers (optional but useful)
    // -------------------------------------------------
    void ResetRectSelection()
    {
        phase = Phase::Idle;
        rectMode = RectMode::None;
        waitingForSecondClick = false;
    }

    void ClearHover()
    {
        hoveredIds.clear();
    }

    

// Grips selection is stored purely in InteractionState (NOT in EntityBook).
inline void ClearGripsSelection()
{
    gripsIds.clear();
    requestRedraw = true;
}

inline int GripsCount() const
{
    return (int)gripsIds.size();
}
// Persistent selection lives in EntityBook (Entity::selected + cached selected-id list).
    // This helper routes the operation and clears interaction transients.
    inline void ClearSelection(EntityBook& book)
    {
        book.ClearSelection();
        hoveredIds.clear();
        ResetRectSelection();
        sceneDirty = true;
        requestRedraw = true;
    }

    // Convenience: selection count is a MODEL concern (EntityBook stores a cached list).
    inline int SelectedCount(const EntityBook* book) const
    {
        return book ? (int)book->SelectedIds().size() : 0;
    }
};

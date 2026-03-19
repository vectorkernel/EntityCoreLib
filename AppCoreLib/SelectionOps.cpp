#include "pch.h"

#include "SelectionOps.h"
#include "InteractionState.h"
#include "EntityBook.h" // from EntityCoreLib

namespace AppCore
{
    void ClearSelection(EntityBook& book, InteractionState& s)
    {
        book.ClearSelection();

        s.hoveredIds.clear();
        s.ResetRectSelection();

        s.sceneDirty = true;
        s.requestRedraw = true;
    }

    void SelectOnly(EntityBook& book, InteractionState& s, std::size_t id)
    {
        book.ClearSelection();

        for (auto& e : book.entities)
        {
            if (e.ID == id)
            {
                book.Select(e);
                break;
            }
        }

        s.sceneDirty = true;
        s.requestRedraw = true;
    }

    void ToggleSelection(EntityBook& book, InteractionState& s, std::size_t id)
    {
        for (auto& e : book.entities)
        {
            if (e.ID == id)
            {
                book.SetSelected(e, !e.selected);
                break;
            }
        }

        s.sceneDirty = true;
        s.requestRedraw = true;
    }
}

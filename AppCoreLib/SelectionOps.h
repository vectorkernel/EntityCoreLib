#pragma once

#include <cstddef>

struct InteractionState;
class EntityBook;

namespace AppCore
{
    // Clears persistent selection in the model and resets selection-related interaction state.
    void ClearSelection(EntityBook& book, InteractionState& s);

    // Common helpers
    void SelectOnly(EntityBook& book, InteractionState& s, std::size_t id);
    void ToggleSelection(EntityBook& book, InteractionState& s, std::size_t id);
}

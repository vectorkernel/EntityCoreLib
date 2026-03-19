// EntityBook.h
#pragma once
#include <algorithm>
#include <unordered_map>
#include <string>
#include <utility>
#include <vector>
#include "Entity.h"
#include "EntityCoreLog.h"
#include <expected>
#include <memory>

#include "EntityBookError.h"   // <-- add this

class EntityBook;

std::expected<std::unique_ptr<EntityBook>, EntityBookError>
CreateEntityBook(std::size_t capacity);

class EntityBook
{
public:
    friend std::expected<std::unique_ptr<EntityBook>, EntityBookError>
    CreateEntityBook(std::size_t capacity);
    EntityBook() = default;

    // NOTE:
    // This project intentionally exposes the raw entity array so command/selection
    // code can iterate without threading accessor plumbing through every callsite.
    // Use GetEntities()/GetEntitiesMutable() if you prefer a clearer API.
    //
    // (Requested) Must be public, not private.
    std::vector<Entity> entities;

    // -------------------------------------------------
    // Persistent selection (Option 2: cached selected-id list)
    // -------------------------------------------------
    const std::vector<std::size_t>& SelectedIds() const noexcept { return m_selectedIds; }
    void ClearSelection();
    void SetSelected(Entity& e, bool on);
    void Select(Entity& e) { SetSelected(e, true); }
    void Deselect(Entity& e) { SetSelected(e, false); }

    Entity& AddEntity(const Entity& e);

    template <typename Pred>
    void RemoveIf(Pred&& pred)
    {
        const auto before = entities.size();
        // IMPORTANT: call predicate exactly once per entity.
        auto it = std::remove_if(entities.begin(), entities.end(),
            [&](const Entity& e)
            {
                const bool remove = pred(e);
                if (remove)
                                        EntityCore::EmitLog(EntityCore::LogLevel::Trace, "EntityBook", "RemoveEntity", EntityCore::DescribeEntity(e));
                return remove;
            });

        entities.erase(it, entities.end());

        if (entities.size() != before)
        {
            ++revision;
            RebuildSelectionCache();
        }
    }

    void Clear();

    const std::vector<Entity>& GetEntities() const;
    std::vector<Entity>& GetEntitiesMutable();

    void SortByDrawOrder();

    // Bump revision for non-structural edits (e.g., selection/hover highlight that changes per-entity color).
    void Touch();

    // Same as Touch(), but records a debug reason string for easier diagnosis.
    void Touch(const char* reason);

    const char* GetLastTouchReason() const { return lastTouchReason.c_str(); }

    // Monotonic revision number that increments when the entity list changes
    // structurally (add/remove/clear/sort). Useful for renderer cache invalidation.
    uint64_t GetRevision() const { return revision; }

    std::size_t Capacity() const noexcept { return m_capacity; }
    void ClearEntityBook();
private:

    explicit EntityBook(std::size_t capacity) : m_capacity(capacity) {}

    void RebuildSelectionCache();
    void ClearSelectionCache();

    bool Initialize(); // if you have init that can fail

    uint64_t revision = 1;

    // Debug-only: why the last Touch() happened.
    std::string lastTouchReason;

    std::vector<std::size_t> m_selectedIds;
    std::unordered_map<std::size_t, std::size_t> m_selectedIndex;

    std::size_t m_capacity{};
};

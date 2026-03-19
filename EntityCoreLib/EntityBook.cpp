#include "pch.h"

// EntityBook.cpp
#include "EntityBook.h"
#include <algorithm>
#include <cstdio>

#include "EntityCoreLog.h"

Entity& EntityBook::AddEntity(const Entity& e)
{
    entities.push_back(e);
    if (entities.back().selected)
    {
        if (m_selectedIndex.find(entities.back().ID) == m_selectedIndex.end())
        {
            m_selectedIndex.emplace(entities.back().ID, m_selectedIds.size());
            m_selectedIds.push_back(entities.back().ID);
        }
    }
    ++revision;

    EntityCore::EmitLog(EntityCore::LogLevel::Trace, "EntityBook", "AddEntity",
        EntityCore::DescribeEntity(entities.back()));
    return entities.back();
}

void EntityBook::Clear()
{
    entities.clear();
    ClearSelectionCache();
    ++revision;

    EntityCore::EmitLog(EntityCore::LogLevel::Info, "EntityBook", "ClearEntityBook", "");
}

void EntityBook::ClearSelectionCache()
{
    m_selectedIds.clear();
    m_selectedIndex.clear();
}

void EntityBook::RebuildSelectionCache()
{
    ClearSelectionCache();
    m_selectedIds.reserve(64);
    for (const auto& e : entities)
    {
        if (!e.selected) continue;
        if (m_selectedIndex.find(e.ID) != m_selectedIndex.end())
            continue;
        m_selectedIndex.emplace(e.ID, m_selectedIds.size());
        m_selectedIds.push_back(e.ID);
    }
}

void EntityBook::SetSelected(Entity& e, bool on)
{
    if (on)
    {
        if (e.selected) return;
        e.selected = true;
        if (m_selectedIndex.find(e.ID) == m_selectedIndex.end())
        {
            m_selectedIndex.emplace(e.ID, m_selectedIds.size());
            m_selectedIds.push_back(e.ID);
        }
        Touch("Selection");
        return;
    }

    if (!e.selected) return;
    e.selected = false;

    auto it = m_selectedIndex.find(e.ID);
    if (it != m_selectedIndex.end())
    {
        const std::size_t idx = it->second;
        const std::size_t lastIdx = (m_selectedIds.empty() ? 0 : (m_selectedIds.size() - 1));
        if (!m_selectedIds.empty() && idx <= lastIdx)
        {
            const std::size_t lastId = m_selectedIds[lastIdx];
            m_selectedIds[idx] = lastId;
            m_selectedIds.pop_back();
            m_selectedIndex.erase(it);
            if (idx != lastIdx)
                m_selectedIndex[lastId] = idx;
        }
    }

    Touch("Selection");
}

void EntityBook::ClearSelection()
{
    if (m_selectedIds.empty())
        return;

    for (auto& e : entities)
        e.selected = false;

    ClearSelectionCache();
    Touch("ClearSelection");
}

const std::vector<Entity>& EntityBook::GetEntities() const
{
    return entities;
}

std::vector<Entity>& EntityBook::GetEntitiesMutable()
{
    return entities;
}

// Stable sort so insertion order is preserved within a drawOrder.
void EntityBook::SortByDrawOrder()
{
    std::stable_sort(entities.begin(), entities.end(),
        [](const Entity& a, const Entity& b)
        {
            if (a.drawOrder != b.drawOrder) return a.drawOrder < b.drawOrder;

            auto layer = [](EntityTag t)
                {
                    switch (t)
                    {
                    case EntityTag::Grid:   return 0;
                    case EntityTag::Scene:  return 1;
                    case EntityTag::User:   return 1;
                    case EntityTag::Cursor: return 2;
                    case EntityTag::Hud:    return 3;
                    default:                return 1;
                    }
                };

            int la = layer(a.tag);
            int lb = layer(b.tag);
            if (la != lb) return la < lb;

            return a.ID < b.ID;
        });

    ++revision;

    char buf[64];
    std::snprintf(buf, sizeof(buf), "count=%zu", entities.size());
    EntityCore::EmitLog(EntityCore::LogLevel::Trace, "EntityBook", "SortByDrawOrder", buf);
}

void EntityBook::Touch()
{
    Touch("Touch()");
}

void EntityBook::Touch(const char* reason)
{
    lastTouchReason = (reason && *reason) ? reason : "Touch";
    ++revision;

    // Hover is extremely high frequency; tag it so hosts can gate it.
    const bool isHover =
        (lastTouchReason == "Hover") ||
        (lastTouchReason.rfind("Hover", 0) == 0); // starts_with("Hover")

    char buf[256];
    std::snprintf(buf, sizeof(buf), "rev=%llu reason=%s",
        (unsigned long long)revision,
        lastTouchReason.c_str());

    EntityCore::EmitLog(EntityCore::LogLevel::Trace, "EntityBook",
        isHover ? "TouchHover" : "Touch",
        buf);
}



bool EntityBook::Initialize()
{
    // Reset storage
    entities.clear();

    // Reserve requested capacity
    entities.reserve(m_capacity);

    // Verify reserve actually happened (it should unless allocation failed)
    const std::size_t reserved = entities.capacity();

    char buf[128];
    std::snprintf(buf, sizeof(buf),
        "Initialize | requested=%zu reserved=%zu",
        m_capacity, reserved);

    EntityCore::EmitLog(
        EntityCore::LogLevel::Info,
        "EntityBook",
        "Initialize",
        buf);

    // If reserve somehow failed to meet requested capacity,
    // treat it as an initialization failure.
    if (reserved < m_capacity)
    {
        EntityCore::EmitLog(
            EntityCore::LogLevel::Error,
            "EntityBook",
            "Initialize",
            "Reserve did not meet requested capacity");

        return false;
    }

    return true;
}

void EntityBook::ClearEntityBook()
{
    // ...
}


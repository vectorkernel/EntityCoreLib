#include "pch.h"
#include "LayerTable.h"
#include <algorithm>
#include <sstream>

void LayerTable::ResetToDefault()
{
    m_layers.clear();
    m_indexById.clear();
    m_nextId = 1;

    Layer def;
    def.id = NextId();
    def.name = "Default";
    def.defaultColor = glm::vec4(1, 1, 1, 1);
    def.defaultLinetype = "Continuous";
    def.on = true;
    def.frozen = false;

    m_layers.push_back(def);
    RebuildIndex();
    m_currentLayerId = def.id;
}

uint32_t LayerTable::NextId()
{
    return m_nextId++;
}

void LayerTable::RebuildIndex()
{
    m_indexById.clear();
    for (std::size_t i = 0; i < m_layers.size(); ++i)
        m_indexById[m_layers[i].id] = i;
}

const LayerTable::Layer* LayerTable::Find(uint32_t id) const
{
    auto it = m_indexById.find(id);
    if (it == m_indexById.end())
        return nullptr;
    return &m_layers[it->second];
}

LayerTable::Layer* LayerTable::Find(uint32_t id)
{
    auto it = m_indexById.find(id);
    if (it == m_indexById.end())
        return nullptr;
    return &m_layers[it->second];
}

void LayerTable::SetCurrentLayer(uint32_t id)
{
    if (Find(id))
        m_currentLayerId = id;
}

std::string LayerTable::MakeUniqueName(const std::string& base) const
{
    auto exists = [&](const std::string& n)
    {
        for (const auto& l : m_layers)
            if (l.name == n) return true;
        return false;
    };

    if (!exists(base))
        return base;

    for (int i = 2; i < 10000; ++i)
    {
        std::ostringstream ss;
        ss << base << " " << i;
        if (!exists(ss.str()))
            return ss.str();
    }
    return base + " X";
}

uint32_t LayerTable::AddLayer(const std::string& desiredName)
{
    Layer l;
    l.id = NextId();
    l.name = desiredName.empty() ? MakeUniqueName("Layer") : MakeUniqueName(desiredName);
    l.defaultColor = glm::vec4(1, 1, 1, 1);
    l.defaultLinetype = "Continuous";
    l.on = true;
    l.frozen = false;

    m_layers.push_back(l);
    RebuildIndex();
    return l.id;
}

bool LayerTable::DeleteLayer(uint32_t id)
{
    if (m_layers.size() <= 1)
        return false;

    auto it = m_indexById.find(id);
    if (it == m_indexById.end())
        return false;

    // Don't allow deleting the current layer; caller should switch first.
    if (id == m_currentLayerId)
        return false;

    const std::size_t idx = it->second;
    m_layers.erase(m_layers.begin() + idx);
    RebuildIndex();
    return true;
}

bool LayerTable::RenameLayer(uint32_t id, const std::string& newName)
{
    if (newName.empty())
        return false;
    Layer* l = Find(id);
    if (!l) return false;

    // Enforce uniqueness
    l->name = MakeUniqueName(newName);
    return true;
}

bool LayerTable::SetLayerColor(uint32_t id, const glm::vec4& rgba)
{
    Layer* l = Find(id);
    if (!l) return false;
    l->defaultColor = rgba;
    return true;
}

bool LayerTable::SetLayerLinetype(uint32_t id, const std::string& linetypeName)
{
    Layer* l = Find(id);
    if (!l) return false;

    // Only accept known linetypes for now.
    if (std::find(m_linetypes.begin(), m_linetypes.end(), linetypeName) == m_linetypes.end())
        return false;

    l->defaultLinetype = linetypeName;
    return true;
}

bool LayerTable::SetLayerOn(uint32_t id, bool on)
{
    Layer* l = Find(id);
    if (!l) return false;
    l->on = on;
    return true;
}

bool LayerTable::SetLayerFrozen(uint32_t id, bool frozen)
{
    Layer* l = Find(id);
    if (!l) return false;
    l->frozen = frozen;
    return true;
}

bool LayerTable::IsLayerVisible(uint32_t id) const
{
    const Layer* l = Find(id);
    if (!l) return true;
    return l->on && !l->frozen;
}

bool LayerTable::IsLayerSelectable(uint32_t id) const
{
    const Layer* l = Find(id);
    if (!l) return true;
    return l->on && !l->frozen;
}

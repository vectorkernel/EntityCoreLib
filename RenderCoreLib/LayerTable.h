#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <glm/glm.hpp>

// Layer + linetype tables are global per drawing environment (shared by all model/paper spaces).
// Minimal CAD-like behavior:
// - Entities reference a layerId.
// - Each layer has a default color and linetype (currently only "Continuous").
// - Layers can be turned off (not drawn) and frozen (not drawn + not selectable).
class LayerTable
{
public:
    struct Layer
    {
        uint32_t id = 0;
        std::string name = "Layer";
        glm::vec4 defaultColor{ 1,1,1,1 };
        std::string defaultLinetype = "Continuous";
        bool on = true;
        bool frozen = false;
    };

    static constexpr uint32_t kInvalidLayerId = 0xFFFFFFFFu;

    void ResetToDefault();

    const std::vector<Layer>& GetLayers() const { return m_layers; }

    // Returns nullptr if not found.
    const Layer* Find(uint32_t id) const;
    Layer* Find(uint32_t id);

    uint32_t CurrentLayerId() const { return m_currentLayerId; }
    void SetCurrentLayer(uint32_t id);

    // Layer ops
    uint32_t AddLayer(const std::string& desiredName = "");
    bool DeleteLayer(uint32_t id);              // cannot delete the last remaining layer
    bool RenameLayer(uint32_t id, const std::string& newName);
    bool SetLayerColor(uint32_t id, const glm::vec4& rgba);
    bool SetLayerLinetype(uint32_t id, const std::string& linetypeName);
    bool SetLayerOn(uint32_t id, bool on);
    bool SetLayerFrozen(uint32_t id, bool frozen);

    // Visibility / selection tests
    bool IsLayerVisible(uint32_t id) const;
    bool IsLayerSelectable(uint32_t id) const;

    // Linetypes (placeholder for future expansion).
    const std::vector<std::string>& GetLinetypes() const { return m_linetypes; }

private:
    uint32_t NextId();

    std::vector<Layer> m_layers;
    std::unordered_map<uint32_t, std::size_t> m_indexById;
    uint32_t m_nextId = 1;
    uint32_t m_currentLayerId = kInvalidLayerId;

    std::vector<std::string> m_linetypes{ "Continuous" };

    void RebuildIndex();
    std::string MakeUniqueName(const std::string& base) const;
};

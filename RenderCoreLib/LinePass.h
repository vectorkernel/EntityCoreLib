#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>

#include "LineEntity.h"
#include "RenderContext.h"

#include <memory>

class ShaderProgram;

// Shared line renderer used by BOTH:
//  - Render-loop renderer (BeginFrame/Submit each frame)
//  - Stateful vector renderer (BuildStatic when dirty)
class LinePass
{
public:
    void Init();

    // Immediate-mode API (UI / per-frame)
    void BeginFrame();
    void Submit(const LineEntity& line);
    void DrawImmediate(const RenderContext& ctx);

    // Stateful API (vector drawings / redraw when dirty)
    void BuildStatic(const std::vector<LineEntity>& lines);
    void DrawStatic(const RenderContext& ctx);

private:
    struct StyleKey
    {
        glm::vec4 color;
        float width;

        bool operator==(const StyleKey& other) const
        {
            return color == other.color && width == other.width;
        }
    };

    struct StyleKeyHash
    {
        std::size_t operator()(const StyleKey& k) const noexcept
        {
            auto h1 = std::hash<float>{}(k.color.r);
            auto h2 = std::hash<float>{}(k.color.g);
            auto h3 = std::hash<float>{}(k.color.b);
            auto h4 = std::hash<float>{}(k.color.a);
            auto h5 = std::hash<float>{}(k.width);

            std::size_t h = h1;
            h ^= (h2 + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
            h ^= (h3 + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
            h ^= (h4 + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
            h ^= (h5 + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
            return h;
        }
    };

    struct StaticBatch
    {
        StyleKey style{};
        GLint firstVertex = 0;    // starting vertex in VBO
        GLsizei vertexCount = 0;  // number of vertices
    };

private:
    void EnsureCapacity(size_t vertexCount);

private:
    std::shared_ptr<ShaderProgram> shaderProg;
    GLuint shaderId = 0; // cached id (may change on hot-swap)
    GLuint vao = 0;
    GLuint vbo = 0;

    // Uniform locations (cached)
    GLint uProjection = -1;
    GLint uView = -1;
    GLint uModel = -1;
    GLint uColor = -1;
    GLint uLineWidth = -1;
    GLint uViewport = -1;

    // Immediate-mode working set (per-frame)
    std::vector<LineEntity> immediateLines;

    // Static-mode cached GPU data (rebuilt only when dirty)
    std::vector<glm::vec3> staticVertices;
    std::vector<StaticBatch> staticBatches;
    size_t capacityVerts = 0;
};


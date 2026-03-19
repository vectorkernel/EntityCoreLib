#pragma once

#include "Entity.h"
#include <glm/glm.hpp>
#include <vector>

// One renderer for all entity types.
// Internally it has "passes" (LinePass, later CirclePass, etc).
class Renderer
{
public:
    void Init();

    // Per-frame
    void BeginFrame();

    // Submit one entity (routes by type)
    void Update(const Entity& entity);

    // Execute draw calls
    void Draw(const glm::mat4& projection,
        const glm::mat4& view,
        const glm::mat4& model);

private:
    // -----------------------------
    // LinePass (internal compartment)
    // -----------------------------
    struct GPULine
    {
        glm::vec3 start{};
        glm::vec3 end{};
        glm::vec4 color{ 1,1,1,1 };
        float     width = 1.0f;
        size_t    vboOffset = 0; // in vertices (not bytes)
    };

    struct LinePass
    {
        void Init();
        void BeginFrame();
        void Submit(const LineEntity& line);
        void Draw(const glm::mat4& projection,
            const glm::mat4& view,
            const glm::mat4& model);

        unsigned int vao = 0;
        unsigned int vbo = 0;
        unsigned int shader = 0;

        std::vector<GPULine> lines;
    };

    LinePass linePass;
};


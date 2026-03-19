// RectPass.h
#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <algorithm>

#include "SolidRectEntity.h"
#include "RenderContext.h"
#include "ShaderLibrary.h"
#include "ShaderProgram.h"

// Simple filled-rectangle renderer (GL_TRIANGLES) used for HUD backgrounds / wipeouts.
// Header-only so you don't need to remember to add a new .cpp to the Visual Studio project.
class RectPass
{
public:
    void Init()
    {
        // Inline solid-color triangle shader (supports per-vertex color).
        std::unordered_map<GLenum, std::string> stages;
        stages[GL_VERTEX_SHADER] = R"(
            #version 330 core
            layout(location = 0) in vec3 aPos;
            layout(location = 1) in vec4 aColor;

            uniform mat4 projection;
            uniform mat4 view;
            uniform mat4 model;

            out vec4 vColor;

            void main()
            {
                vColor = aColor;
                gl_Position = projection * view * model * vec4(aPos, 1.0);
            }
        )";

        stages[GL_FRAGMENT_SHADER] = R"(
            #version 330 core
            in vec4 vColor;
            out vec4 FragColor;
            void main()
            {
                FragColor = vColor;
            }
        )";

        shaderProg = ShaderLibrary::Get().CreateFromSources("vk_solid_rect", stages);
        shaderId = shaderProg ? shaderProg->Id() : 0;

        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);

        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        capacityVerts = 1024;
        glBufferData(GL_ARRAY_BUFFER, capacityVerts * sizeof(Vertex), nullptr, GL_DYNAMIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));

        glBindVertexArray(0);

        uProjection = glGetUniformLocation(shaderId, "projection");
        uView       = glGetUniformLocation(shaderId, "view");
        uModel      = glGetUniformLocation(shaderId, "model");
    }

    void BeginFrame()
    {
        vertices.clear();
    }

    void Submit(const SolidRectEntity& r)
    {
        // Two triangles: (min.x,min.y) .. (max.x,max.y)
        const glm::vec3 a(r.min.x, r.min.y, r.min.z);
        const glm::vec3 b(r.max.x, r.min.y, r.min.z);
        const glm::vec3 c(r.max.x, r.max.y, r.min.z);
        const glm::vec3 d(r.min.x, r.max.y, r.min.z);

        // a-b-c, a-c-d
        vertices.push_back({ a, r.color });
        vertices.push_back({ b, r.color });
        vertices.push_back({ c, r.color });

        vertices.push_back({ a, r.color });
        vertices.push_back({ c, r.color });
        vertices.push_back({ d, r.color });
    }

    void DrawImmediate(const RenderContext& ctx)
    {
        if (!shaderProg)
            return;

        if (vertices.empty())
            return;

        // Refresh shader id if hot-swapped/replaced.
        if (shaderProg->Id() != shaderId)
        {
            shaderId = shaderProg->Id();
            uProjection = glGetUniformLocation(shaderId, "projection");
            uView       = glGetUniformLocation(shaderId, "view");
            uModel      = glGetUniformLocation(shaderId, "model");
        }

        EnsureCapacity(vertices.size());

        glUseProgram(shaderId);
        glUniformMatrix4fv(uProjection, 1, GL_FALSE, glm::value_ptr(ctx.projection));
        glUniformMatrix4fv(uView,       1, GL_FALSE, glm::value_ptr(ctx.view));
        glUniformMatrix4fv(uModel,      1, GL_FALSE, glm::value_ptr(ctx.model));

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(Vertex), vertices.data());

        glDisable(GL_CULL_FACE);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertices.size());

        glBindVertexArray(0);
        glUseProgram(0);
    }

private:
    struct Vertex
    {
        glm::vec3 pos;
        glm::vec4 color;
    };

    void EnsureCapacity(size_t vertexCount)
    {
        if (vertexCount <= capacityVerts)
            return;

        while (capacityVerts < vertexCount)
            capacityVerts *= 2;

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, capacityVerts * sizeof(Vertex), nullptr, GL_DYNAMIC_DRAW);
    }

private:
    std::shared_ptr<ShaderProgram> shaderProg;
    GLuint shaderId = 0;

    GLuint vao = 0;
    GLuint vbo = 0;

    GLint uProjection = -1;
    GLint uView = -1;
    GLint uModel = -1;

    std::vector<Vertex> vertices;
    size_t capacityVerts = 0;
};

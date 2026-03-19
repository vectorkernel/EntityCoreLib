#include "pch.h"
#include "LinePass.h"
#include "GLLine.h"
#include "ShaderProgram.h"

#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <string>

void LinePass::Init()
{
    // Use the shared line-width shader registered via GLLine + ShaderLibrary.
    shaderProg = GLLine::GetLineBlockProgram();
    shaderId = shaderProg ? shaderProg->Id() : 0;

glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    // Start with some capacity; can grow
    capacityVerts = 8192;
    glBufferData(GL_ARRAY_BUFFER, capacityVerts * sizeof(glm::vec3), nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);

    glBindVertexArray(0);

    // Cache uniforms
    uProjection = glGetUniformLocation(shaderId, "projection");
    uView = glGetUniformLocation(shaderId, "view");
    uModel = glGetUniformLocation(shaderId, "model");
    uColor = glGetUniformLocation(shaderId, "color");
    uLineWidth = glGetUniformLocation(shaderId, "lineWidth");
    uViewport = glGetUniformLocation(shaderId, "viewportPx");
}

// ---------------------------
// Immediate-mode (render-loop)
// ---------------------------
void LinePass::BeginFrame()
{
    immediateLines.clear();
}

void LinePass::Submit(const LineEntity& line)
{
    immediateLines.push_back(line);
}

void LinePass::DrawImmediate(const RenderContext& ctx)
{
    if (immediateLines.empty())
        return;

    // Hot-swap support: if the program was replaced in ShaderLibrary, refresh ids/locations.
    if (shaderProg)
    {
        const GLuint newId = shaderProg->Id();
        if (newId != shaderId)
        {
            shaderId = newId;
            uProjection = glGetUniformLocation(shaderId, "projection");
            uView = glGetUniformLocation(shaderId, "view");
            uModel = glGetUniformLocation(shaderId, "model");
            uColor = glGetUniformLocation(shaderId, "color");
            uLineWidth = glGetUniformLocation(shaderId, "lineWidth");
            uViewport = glGetUniformLocation(shaderId, "viewportPx");
        }
    }

    glUseProgram(shaderId);
    glBindVertexArray(vao);

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);

    glUniformMatrix4fv(uProjection, 1, GL_FALSE, glm::value_ptr(ctx.projection));
    glUniformMatrix4fv(uView, 1, GL_FALSE, glm::value_ptr(ctx.view));
    glUniformMatrix4fv(uModel, 1, GL_FALSE, glm::value_ptr(ctx.model));

    // Upload all vertices for this frame
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    const size_t neededVerts = immediateLines.size() * 2;
    EnsureCapacity(neededVerts);

    size_t v = 0;
    for (const auto& l : immediateLines)
    {
        glm::vec3 verts[2] = { l.start, l.end };
        glBufferSubData(GL_ARRAY_BUFFER, v * sizeof(glm::vec3), sizeof(verts), verts);
        v += 2;
    }

    // Draw each with its style (color/width)
    GLint first = 0;
    for (const auto& l : immediateLines)
    {
        glUniform4fv(uColor, 1, glm::value_ptr(l.color));
        glUniform1f(uLineWidth, l.width);
        GLint vp[4];
        glGetIntegerv(GL_VIEWPORT, vp);
        glUniform2f(uViewport, (float)vp[2], (float)vp[3]);
        glDrawArrays(GL_LINES, first, 2);
        first += 2;
    }

    glBindVertexArray(0);
}

// ---------------------------
// Static-mode (stateful CAD-like)
// ---------------------------
void LinePass::BuildStatic(const std::vector<LineEntity>& lines)
{
    staticVertices.clear();
    staticBatches.clear();

    if (lines.empty())
        return;

    // Group by style (color+width)
    std::unordered_map<StyleKey, std::vector<glm::vec3>, StyleKeyHash> groups;
    groups.reserve(lines.size());

    for (const auto& l : lines)
    {
        StyleKey key{ l.color, l.width };
        auto& verts = groups[key];
        verts.push_back(l.start);
        verts.push_back(l.end);
    }

    // Flatten into one vertex array + record batches
    staticVertices.reserve(lines.size() * 2);

    for (auto& kv : groups)
    {
        const StyleKey style = kv.first;
        auto& verts = kv.second;

        StaticBatch batch;
        batch.style = style;
        batch.firstVertex = (GLint)staticVertices.size();
        batch.vertexCount = (GLsizei)verts.size();

        staticVertices.insert(staticVertices.end(), verts.begin(), verts.end());
        staticBatches.push_back(batch);
    }

    // Upload once
    EnsureCapacity(staticVertices.size());

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, staticVertices.size() * sizeof(glm::vec3), staticVertices.data());
}

void LinePass::DrawStatic(const RenderContext& ctx)
{
    if (staticBatches.empty())
        return;

    // Hot-swap support: if the program was replaced in ShaderLibrary, refresh ids/locations.
    if (shaderProg)
    {
        const GLuint newId = shaderProg->Id();
        if (newId != shaderId)
        {
            shaderId = newId;
            uProjection = glGetUniformLocation(shaderId, "projection");
            uView = glGetUniformLocation(shaderId, "view");
            uModel = glGetUniformLocation(shaderId, "model");
            uColor = glGetUniformLocation(shaderId, "color");
            uLineWidth = glGetUniformLocation(shaderId, "lineWidth");
            uViewport = glGetUniformLocation(shaderId, "viewportPx");
        }
    }

    glUseProgram(shaderId);
    glBindVertexArray(vao);

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);

    glUniformMatrix4fv(uProjection, 1, GL_FALSE, glm::value_ptr(ctx.projection));
    glUniformMatrix4fv(uView, 1, GL_FALSE, glm::value_ptr(ctx.view));
    glUniformMatrix4fv(uModel, 1, GL_FALSE, glm::value_ptr(ctx.model));

    // Viewport uniform (used by the geometry shader to convert pixel width to NDC)
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    glUniform2f(uViewport, (float)vp[2], (float)vp[3]);

    for (const auto& b : staticBatches)
    {
        glUniform4fv(uColor, 1, glm::value_ptr(b.style.color));
        // Geometry shader expands to a quad; width is in pixels.
        glUniform1f(uLineWidth, b.style.width);
        glDrawArrays(GL_LINES, b.firstVertex, b.vertexCount);
    }

    glBindVertexArray(0);
}

void LinePass::EnsureCapacity(size_t vertexCount)
{
    if (vertexCount <= capacityVerts)
        return;

    while (capacityVerts < vertexCount)
        capacityVerts *= 2;

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, capacityVerts * sizeof(glm::vec3), nullptr, GL_DYNAMIC_DRAW);
}


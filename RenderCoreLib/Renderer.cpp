#include "pch.h"
#include "Renderer.h"
#include <glad/glad.h>

#include <glm/gtc/type_ptr.hpp>
#include <cstdio>

// -----------------------------
// Helpers
// -----------------------------
static unsigned int CompileShader(unsigned int type, const char* src)
{
    unsigned int s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::printf("Shader compile error:\n%s\n", log);
    }
    return s;
}

static unsigned int CreateProgram(const char* vsSrc, const char* fsSrc)
{
    unsigned int vs = CompileShader(GL_VERTEX_SHADER, vsSrc);
    unsigned int fs = CompileShader(GL_FRAGMENT_SHADER, fsSrc);

    unsigned int p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);

    int ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[2048];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::printf("Program link error:\n%s\n", log);
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

// -----------------------------
// Renderer public API
// -----------------------------
void Renderer::Init()
{
    linePass.Init();
}

void Renderer::BeginFrame()
{
    // Clear once per frame (keep your nice dark background)
    glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    linePass.BeginFrame();
}

void Renderer::Update(const Entity& entity)
{
    switch (entity.type)
    {
    case EntityType::Line:
        linePass.Submit(entity.line);
        break;
    default:
        break;
    }
}

void Renderer::Draw(const glm::mat4& projection,
    const glm::mat4& view,
    const glm::mat4& model)
{
    linePass.Draw(projection, view, model);
}

// -----------------------------
// LinePass implementation
// -----------------------------
void Renderer::LinePass::Init()
{
    // Minimal line shader
    const char* vs = R"(
        #version 330 core
        layout(location = 0) in vec3 aPos;

        uniform mat4 projection;
        uniform mat4 view;
        uniform mat4 model;

        void main()
        {
            gl_Position = projection * view * model * vec4(aPos, 1.0);
        }
    )";

    const char* fs = R"(
        #version 330 core
        out vec4 FragColor;
        uniform vec4 color;
        void main()
        {
            FragColor = color;
        }
    )";

    shader = CreateProgram(vs, fs);

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    // Reserve room for a bunch of line vertices (each line = 2 verts).
    // You can bump this if you need more.
    const size_t maxVerts = 8192;
    glBufferData(GL_ARRAY_BUFFER, maxVerts * sizeof(glm::vec3), nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);

    glBindVertexArray(0);
}

void Renderer::LinePass::BeginFrame()
{
    lines.clear();
}

void Renderer::LinePass::Submit(const LineEntity& line)
{
    GPULine gpu;
    gpu.start = line.start;
    gpu.end = line.end;
    gpu.color = line.color;
    gpu.width = line.width;

    // each line uses 2 vertices, and we store offsets in vertices
    gpu.vboOffset = lines.size() * 2;

    lines.push_back(gpu);
}

void Renderer::LinePass::Draw(const glm::mat4& projection,
    const glm::mat4& view,
    const glm::mat4& model)
{
    if (lines.empty())
        return;

    glUseProgram(shader);
    glBindVertexArray(vao);

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);

    glUniformMatrix4fv(glGetUniformLocation(shader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(glGetUniformLocation(shader, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shader, "model"), 1, GL_FALSE, glm::value_ptr(model));

    // Upload all line vertices (batched) into the VBO
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    for (const auto& l : lines)
    {
        glm::vec3 verts[2] = { l.start, l.end };

        glBufferSubData(
            GL_ARRAY_BUFFER,
            l.vboOffset * sizeof(glm::vec3),
            sizeof(verts),
            verts
        );
    }

    // Draw each line with its own color/width
    for (const auto& l : lines)
    {
        glUniform4fv(glGetUniformLocation(shader, "color"), 1, glm::value_ptr(l.color));
        glLineWidth(l.width);

        glDrawArrays(GL_LINES, (int)l.vboOffset, 2);
    }

    glBindVertexArray(0);
}


#pragma once

#include <glad/glad.h>

#include <vector>
#include <string_view>
#include <cstdint>
#include <memory>

#include <glm/glm.hpp>

#include "EntityBook.h"     // adjust if your path differs

// If your project has different header names/paths, keep your existing ones.

class Application
{
public:
    Application();
    ~Application();

    void Initialize();
    void Update(float inDeltaTime);
    void Render(float inAspectRatio);
    void Shutdown();

    // EntityCore logging callback
    static void LogSink(EntityCore::LogLevel level,
        const char* category,
        const char* event,
        std::string_view message);

private:
    struct Vertex
    {
        glm::vec3 pos;
        glm::vec4 color;
    };

private:
    void BuildGrid(int clientW, int clientH);
    void UploadLinesToGpu();

private:
    // Engine-side data
    // CreateEntityBook() yields a std::unique_ptr<EntityBook> (optionally wrapped),
    // so store the book as a pointer.
    std::unique_ptr<EntityBook> m_book; // matches your usage: m_book->Clear(), m_book->entities
    uint32_t m_nextId = 1;

    // GL resources
    GLuint m_program = 0;
    GLint  m_uMvp = -1;
    GLuint m_vbo = 0;

    // CPU vertex cache for upload
    std::vector<Vertex> m_vertices;

    // Resize tracking
    int m_lastClientW = 0;
    int m_lastClientH = 0;
};

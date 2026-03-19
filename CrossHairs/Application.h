#pragma once
#ifndef _H_APPLICATION_
#define _H_APPLICATION_

#include <vector>
#include <cstddef>

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>

#include "EntityBook.h"
#include "EntityCoreLog.h"
#include "PanZoomController.h"
#include "hersheyfont.h"
#include "HersheyTextBuilder.h"
#include "Crosshairs.h"

// Application owns:
// - an EntityBook filled with a grid of LineEntity entries
// - an EntityCore log sink that prints to the WinMain console
// - a minimal GL shader + VBO for drawing GL_LINES
class Application
{
private:
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

public:
    Application();
    virtual ~Application();

    virtual void Initialize();
    virtual void Update(float inDeltaTime);
    virtual void Render(float inAspectRatio);
    virtual void Shutdown();
    void OnMouseMoveClient(int x, int y);
    PanZoomController m_pz;
private:
    // Logging sink -> console
    static void LogSink(EntityCore::LogLevel level,
                        const char* category,
                        const char* event,
                        std::string_view message);

    // Build a grid in "pixel-space centered at origin":
    // x in [-w/2, +w/2], y in [-h/2, +h/2]
    void BuildGrid(int clientW, int clientH);
    void UploadLinesToGpu();

    GLuint Compile(GLenum type, const char* src);
    GLuint Link(GLuint vs, GLuint fs);

    struct Vertex
    {
        glm::vec3 pos;
        glm::vec4 color;
    };



private:
    std::unique_ptr<EntityBook> m_book;
    std::size_t m_nextId = 1;

    std::vector<Vertex> m_vertices;

    GLuint m_program = 0;
    GLuint m_vbo = 0;

    GLint m_uMvp = -1;

    // --- HUD (screen-space) ---
    std::vector<Vertex> m_hudVertices;
    GLuint m_hudVbo = 0;
    hershey_font* m_hudFont = nullptr;
    // --- Crosshairs (screen-space) ---
    RenderCore::Crosshairs m_crosshairs;
    std::vector<Vertex>    m_cursorVertices;
    GLuint                 m_cursorVbo = 0;
    // Cache for rebuilding grid if window ever changes size (even though fixed-size now)
    int m_lastClientW = 0;
    int m_lastClientH = 0;
};


#endif
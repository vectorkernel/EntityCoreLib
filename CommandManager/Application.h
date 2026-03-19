#pragma once
#ifndef _H_APPLICATION_
#define _H_APPLICATION_

#include <vector>
#include <cstddef>

// Windows types (WPARAM, etc.). Also disable min/max macros.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>

#include "EntityBook.h"
#include "EntityCoreLog.h"
#include "PanZoomController.h"
#include "hersheyfont.h"
#include "HersheyTextBuilder.h"
#include "Crosshairs.h"
#include "Picking.h"
#include "SelectionWindow.h"
#include "DragonCurve.h"

#include "InteractionState.h" // AppCoreLib

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
    void OnLeftButtonDownClient(int x, int y);
    void OnKeyDown(WPARAM key, bool ctrlDown);

    // -------------------------------------------------
    // CommandManager UI integration helpers
    // -------------------------------------------------
    bool IsIdleForCommand() const { return m_state.tool != InteractionState::Tool::Erase; }
    bool IsEraseActive() const { return m_state.tool == InteractionState::Tool::Erase; }
    int  LastErasedCount() const { return m_state.lastErased; }
    int  TotalErasedCount() const { return m_state.totalErased; }

    // Explicit command actions (avoid UI sending synthetic key presses).
    void StartEraseCommand();
    void CancelCommand();
    void CommitErase();
    void BeginWindowSelection(bool crossing);

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

    // -------------------------------------------------
    // Rendering helpers (functionized for readability)
    // -------------------------------------------------
    void EnsureViewportAndGrid(int w, int h);
    void BindLineVertexLayout();
    void DrawWorldPass(int w, int h, const glm::mat4& mvp);
    void DrawOverlayPass(int w, int h);
    void DrawHudPass(int w, int h, const glm::mat4& mvp);

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

    // Cache for rebuilding grid if window ever changes size
    int m_lastClientW = 0;
    int m_lastClientH = 0;

    // -------------------------------------------------
    // Unified interaction state (AppCoreLib)
    // -------------------------------------------------
    InteractionState m_state;

    // Rectangle selection overlay (RenderCoreLib)
    RenderCore::SelectionWindow m_selWindow;

private:
    void MarkDirty() { m_state.sceneDirty = true; }
};

#endif

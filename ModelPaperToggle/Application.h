#pragma once
#ifndef _H_APPLICATION_
#define _H_APPLICATION_

#include <vector>
#include <cstddef>
#include <memory>

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

// Text helpers (InitTextEntityOrDie, TextInitPolicy)
#include "TextEntityUtil.h"

// RenderCoreLib
#include "Crosshairs.h"
#include "Picking.h"
#include "SelectionWindow.h"
#include "GridUtil.h"
#include "LineColorUtil.h"

// AppCoreLib
#include "InteractionState.h"
#include "SelectionOps.h"
#include "HoverOps.h"

// Demo content
#include "DragonCurve.h"

// ------------------------------------------------------------
// ModelPaperToggle (baby-step #1):
// - Two independent "spaces" (Model vs Paper), each with its own
//   EntityBook + InteractionState + PanZoom + SelectionWindow.
// - F2 toggles the active space.
// - Hover highlight + selection continues to work in whichever
//   space is active (re-using the known-good EntityHighlighting flow).
// ------------------------------------------------------------
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
    void OnRightButtonDownClient(int x, int y);
    void OnRightButtonUpClient();
    void OnRightButtonDragMoveClient(int x, int y);
    void OnMouseWheelClient(int x, int y, int wheelDelta);
    void OnKeyDown(WPARAM key, bool ctrlDown);

private:
    // Logging sink -> console
    static void LogSink(EntityCore::LogLevel level,
                        const char* category,
                        const char* event,
                        std::string_view message);

    GLuint Compile(GLenum type, const char* src);
    GLuint Link(GLuint vs, GLuint fs);

    struct Vertex
    {
        glm::vec3 pos;
        glm::vec4 color;
    };

    enum class Space
    {
        Model,
        Paper
    };

    struct SpaceContext
    {
        std::unique_ptr<EntityBook> book;
        InteractionState state;
        PanZoomController pz;

        // view-aligned background grid (optional)
        RenderCore::GridCache gridCache;

        // per-space two-click window/crossing box
        RenderCore::SelectionWindow selWindow;

        // viewport cache for grid rebuild
        int lastClientW = 0;
        int lastClientH = 0;

        // CPU vertex cache (rebuilt when sceneDirty)
        std::vector<Vertex> vertices;
    };

    // Paper viewport rect in PAPER WORLD units (set by V two-click)
    bool      m_hasPaperViewport = false;
    glm::vec2 m_paperVpMin{ 0.0f, 0.0f };
    glm::vec2 m_paperVpMax{ 0.0f, 0.0f };
    bool      m_pendingModelZoomToViewport = false;

private:
    SpaceContext& Active();
    const SpaceContext& Active() const;

    void BuildModelScene(SpaceContext& ctx);
    void BuildPaperScene(SpaceContext& ctx);

    bool RebuildHoverSet(SpaceContext& ctx);
    void UploadLinesToGpu(SpaceContext& ctx);
    void EnsureLinesOnGpu(SpaceContext& ctx);

    void MarkDirty(SpaceContext& ctx) { ctx.state.sceneDirty = true; }

    // -------------------------------------------------
    // Render passes
    // -------------------------------------------------
    void SyncViewportAndRebuildSceneIfNeeded(SpaceContext& ctx, int w, int h);
    void BindLineVertexLayout();
    void DrawScenePass(const SpaceContext& ctx, const glm::mat4& mvp);
    void DrawOverlayPass(SpaceContext& ctx, int w, int h);
    void DrawHudPass(SpaceContext& ctx, int w, int h, const glm::mat4& sceneMvp);

    // Ensures the HUD font is available. If not, the application terminates with a clear error.
    void EnsureHudFontOrDie();

private:
    Space m_space = Space::Model;

    SpaceContext m_model;
    SpaceContext m_paper;

    std::size_t m_nextIdModel = 1;
    std::size_t m_nextIdPaper = 1;

    // GL program + shared VBO (we reuse the same GPU buffer; we just re-upload per space)
    GLuint m_program = 0;
    GLuint m_vbo = 0;
    // Tracks which SpaceContext's line vertices are currently resident in m_vbo
    SpaceContext* m_gpuLinesOwner = nullptr;

    GLint m_uMvp = -1;

    // --- HUD (screen-space) ---
    std::vector<Vertex> m_hudVertices;
    GLuint m_hudVbo = 0;
    hershey_font* m_hudFont = nullptr;

    // --- Crosshairs (screen-space) ---
    RenderCore::Crosshairs m_crosshairs;
    std::vector<Vertex>    m_cursorVertices;
    GLuint                 m_cursorVbo = 0;
};

#endif

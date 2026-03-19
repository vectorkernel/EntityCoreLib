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
#include "Crosshairs.h"
#include "InteractionState.h"

// RenderCoreLib
#include "GridUtil.h"
#include "LineColorUtil.h"
#include "Picking.h"
#include "GripsOverlay.h"

// AppCoreLib
#include "HoverOps.h"
#include "SelectionOps.h"
#include "GripsOps.h"

// A minimal "kicking the tires" app that demonstrates:
//
//  1) Regular selection set  -> stored in EntityBook (Entity::selected)
//  2) Grips selection set    -> stored in InteractionState.gripsIds (separate)
//  3) Separate grips render pass (screen-space overlay)
//
// Controls:
//   - S : Regular selection tool (toggles EntityBook selection)
//   - G : Grips selection tool (toggles InteractionState.gripsIds)
//   - ESC : Clear both sets
//   - Mouse move : hover highlight (yellow)
//   - LMB : apply pick-box selection to the active tool
//
class Application
{
public:
    Application();
    ~Application();

    void Initialize();
    void Update(float dt);
    void Render(float aspect);
    void Shutdown();

    void OnMouseMoveClient(int x, int y);
    void OnLeftButtonDownClient(int x, int y);
    void OnKeyDown(WPARAM key, bool ctrlDown);

    PanZoomController m_pz;

private:
    static void LogSink(EntityCore::LogLevel level,
                        const char* category,
                        const char* event,
                        std::string_view message);

    // -------------------------------------------------
    // scene + selection
    // -------------------------------------------------
    void build_test_scene();
    bool rebuild_hover_set();

    // -------------------------------------------------
    // gpu uploads
    // -------------------------------------------------
    void upload_world_lines_to_gpu();
    void upload_overlay_lines_to_gpu();

    // -------------------------------------------------
    // render passes (kept very explicit for teaching)
    // -------------------------------------------------
    void update_viewport_from_gl();
    void draw_world_lines_pass();
    void build_overlay_lines_for_crosshairs();
    void build_overlay_lines_for_grips();
    void draw_overlay_lines_pass();

    GLuint Compile(GLenum type, const char* src);
    GLuint Link(GLuint vs, GLuint fs);

    struct Vertex
    {
        glm::vec3 pos;
        glm::vec4 color;
    };

    std::unique_ptr<EntityBook> m_book;
    std::size_t m_nextId = 1;

    InteractionState m_state;
    RenderCore::Crosshairs m_crosshairs;
    RenderCore::GridCache m_gridCache;

    // World pass
    std::vector<Vertex> m_worldVertices;
    GLuint m_program = 0;
    GLuint m_worldVbo = 0;
    GLint  m_uMvp = -1;

    // Overlay pass (screen-space): crosshairs + grips
    std::vector<RenderCore::OverlayVertex> m_overlayVertices;
    GLuint m_overlayVbo = 0;

    // Viewport
    int m_lastClientW = 0;
    int m_lastClientH = 0;
};

#endif

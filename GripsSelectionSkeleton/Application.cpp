#include "Application.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "DragonCurve.h"

// RenderCoreLib helper that enforces TextEntity.font is set (keep consistent with repo policy).
#include "TextEntityUtil.h"
#include <iostream>

static const char* kVsSrc = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColor;
uniform mat4 uMvp;
out vec4 vColor;
void main(){
    gl_Position = uMvp * vec4(aPos, 1.0);
    vColor = aColor;
}
)GLSL";

static const char* kFsSrc = R"GLSL(
#version 330 core
in vec4 vColor;
out vec4 FragColor;
void main(){
    FragColor = vColor;
}
)GLSL";

Application::Application()
{
    EntityCore::SetLogSink(&Application::LogSink);
}

Application::~Application()
{
    Shutdown();
}

void Application::LogSink(EntityCore::LogLevel level,
                          const char* category,
                          const char* event,
                          std::string_view message)
{
    (void)level;
    std::ostringstream oss;
    oss << "[" << category << "] " << event << " : " << message << "\n";
    OutputDebugStringA(oss.str().c_str());
}

GLuint Application::Compile(GLenum type, const char* src)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char buf[4096];
        glGetShaderInfoLog(sh, (GLsizei)sizeof(buf), nullptr, buf);
        OutputDebugStringA(buf);
    }
    return sh;
}

GLuint Application::Link(GLuint vs, GLuint fs)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char buf[4096];
        glGetProgramInfoLog(p, (GLsizei)sizeof(buf), nullptr, buf);
        OutputDebugStringA(buf);
    }
    glDetachShader(p, vs);
    glDetachShader(p, fs);
    return p;
}

void Application::Initialize()
{
    // Build minimal scene
    build_test_scene();

    GLuint vs = Compile(GL_VERTEX_SHADER, kVsSrc);
    GLuint fs = Compile(GL_FRAGMENT_SHADER, kFsSrc);
    m_program = Link(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    m_uMvp = glGetUniformLocation(m_program, "uMvp");

    glGenBuffers(1, &m_worldVbo);
    glGenBuffers(1, &m_overlayVbo);

    // Crosshairs: use a small pick box, similar to EntityHighlighting.
    m_crosshairs.SetPickBoxSizePx(9);

    m_state.tool = InteractionState::Tool::SelectGrips; // default = grips tool for this demo
    m_state.showCrosshairs = true;
    m_state.sceneDirty = true;
}

void Application::Shutdown()
{
    if (m_worldVbo) { glDeleteBuffers(1, &m_worldVbo); m_worldVbo = 0; }
    if (m_overlayVbo) { glDeleteBuffers(1, &m_overlayVbo); m_overlayVbo = 0; }
    if (m_program)  { glDeleteProgram(m_program); m_program = 0; }
    m_book.reset();
}

void Application::build_test_scene()
{
    m_book = std::make_unique<EntityBook>();
    m_book->Clear();

    // keep the demo simple but meaningful:
    // - background grid is generated per-frame (RenderCore::UpdateBackgroundGrid)
    // - dragon curve is the persistent "user" geometry we can pick + grips-select

    DragonCurve dragon;
    const int iterations = 11; // 2^11 = 2048 segments

    // Put the dragon near world origin so pan/zoom starts in a useful place.
    const glm::vec3 origin(-200.0f, -120.0f, 0.0f);
    const auto segs = dragon.Build(iterations, origin);

    for (const auto& s : segs)
    {
        Entity e;
        e.ID = m_nextId++;
        e.type = EntityType::Line;
        e.tag = EntityTag::User;
        e.screenSpace = false;
        e.pickable = true;
        e.visible = true;
        e.selected = false;
        e.drawOrder = 0;
        e.line.p0 = s.a;
        e.line.p1 = s.b;
        e.line.color = glm::vec4(1.0f); // white by default (hover/selection/grips will override)
        e.line.width = 1.0f;
        m_book->entities.push_back(e);
    }
}

bool Application::rebuild_hover_set()
{
    if (!m_book) return false;
    if (m_lastClientW <= 0 || m_lastClientH <= 0) return false;

    const int sidePx = m_crosshairs.GetPickBoxSizePx();
    const int halfPx = std::max(1, sidePx / 2);

    auto pb = RenderCore::MakePickBox(m_pz, m_state.mouseX, m_state.mouseY,
                                     halfPx, m_lastClientW, m_lastClientH);

    return AppCore::RebuildHoverSet(*m_book, m_state, pb.worldMin, pb.worldMax);
}

void Application::upload_world_lines_to_gpu()
{
    if (!m_book) return;

    m_worldVertices.clear();
    m_worldVertices.reserve(m_book->entities.size() * 2);

    const glm::vec4 kRegularSelection(1.0f, 1.0f, 0.0f, 1.0f); // yellow highlight for EntityBook selection

    for (const auto& e : m_book->entities)
    {
        if (e.type != EntityType::Line) continue;
        if (!e.visible) continue;

        glm::vec4 c = RenderCore::EffectiveLineColor(e, &m_state.hoveredIds, kRegularSelection);

        m_worldVertices.push_back({ glm::vec3(e.line.p0.x, e.line.p0.y, 0.0f), c });
        m_worldVertices.push_back({ glm::vec3(e.line.p1.x, e.line.p1.y, 0.0f), c });
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_worldVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(m_worldVertices.size() * sizeof(Vertex)),
                 m_worldVertices.data(),
                 GL_DYNAMIC_DRAW);
}

void Application::upload_overlay_lines_to_gpu()
{
    glBindBuffer(GL_ARRAY_BUFFER, m_overlayVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(m_overlayVertices.size() * sizeof(RenderCore::OverlayVertex)),
                 m_overlayVertices.data(),
                 GL_DYNAMIC_DRAW);
}

void Application::update_viewport_from_gl()
{
    GLint vp[4] = { 0,0,0,0 };
    glGetIntegerv(GL_VIEWPORT, vp);
    m_lastClientW = vp[2];
    m_lastClientH = vp[3];

    m_pz.SetViewport(m_lastClientW, m_lastClientH);
    m_crosshairs.SetViewport(m_lastClientW, m_lastClientH);
}

void Application::draw_world_lines_pass()
{
    // world pass: grid + hover + regular selection highlight
    const glm::mat4 mvp = m_pz.GetMVP();
    glUniformMatrix4fv(m_uMvp, 1, GL_FALSE, &mvp[0][0]);

    glBindBuffer(GL_ARRAY_BUFFER, m_worldVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));

    glDrawArrays(GL_LINES, 0, (GLsizei)m_worldVertices.size());
}

static void append_overlay_from_line_entities(const std::vector<LineEntity>& lines,
                                              std::vector<RenderCore::OverlayVertex>& out)
{
    for (const auto& L : lines)
    {
        out.push_back({ glm::vec3(L.p0.x, L.p0.y, 0.0f), L.color });
        out.push_back({ glm::vec3(L.p1.x, L.p1.y, 0.0f), L.color });
    }
}

void Application::build_overlay_lines_for_crosshairs()
{
    if (!m_state.showCrosshairs) return;

    std::vector<LineEntity> cross;
    m_crosshairs.BuildLines(cross);
    append_overlay_from_line_entities(cross, m_overlayVertices);
}

void Application::build_overlay_lines_for_grips()
{
    // grips overlay is separate from regular selection, and only shows in grips tool mode.
    if (m_state.tool != InteractionState::Tool::SelectGrips) return;
    if (m_state.gripsIds.empty()) return;

    const glm::vec4 gripColor(0.0f, 0.55f, 1.0f, 1.0f); // blue-ish
    std::vector<RenderCore::OverlayVertex> grips;
    RenderCore::BuildGripsOverlayLines(*m_book,
                                       m_state.gripsIds,
                                       m_pz,
                                       m_lastClientW,
                                       m_lastClientH,
                                       4,
                                       gripColor,
                                       grips);

    m_overlayVertices.insert(m_overlayVertices.end(), grips.begin(), grips.end());
}

void Application::draw_overlay_lines_pass()
{
    // overlay pass: pixel coords, origin bottom-left
    const glm::mat4 ortho = glm::ortho(0.0f, (float)m_lastClientW,
                                       0.0f, (float)m_lastClientH,
                                       -1.0f, 1.0f);
    glUniformMatrix4fv(m_uMvp, 1, GL_FALSE, &ortho[0][0]);

    glBindBuffer(GL_ARRAY_BUFFER, m_overlayVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RenderCore::OverlayVertex), (void*)offsetof(RenderCore::OverlayVertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(RenderCore::OverlayVertex), (void*)offsetof(RenderCore::OverlayVertex, color));

    glDrawArrays(GL_LINES, 0, (GLsizei)m_overlayVertices.size());
}

void Application::Update(float /*dt*/)
{
    // this demo's scene is just the background grid.
    if (m_book && m_lastClientW > 0 && m_lastClientH > 0)
    {
        const bool gridChanged = RenderCore::UpdateBackgroundGrid(*m_book,
                                                                  m_pz,
                                                                  m_lastClientW,
                                                                  m_lastClientH,
                                                                  m_nextId,
                                                                  m_gridCache,
                                                                  /*spacingWorld*/ 50.0f,
                                                                  /*majorEvery*/ 5);
        if (gridChanged)
            m_state.sceneDirty = true;
    }

    // hover rebuild (yellow)
    const bool hoverChanged = rebuild_hover_set();
    if (hoverChanged)
    {
        m_state.sceneDirty = true;
        m_state.requestRedraw = true;

        // Debug: print hovered IDs when the set changes
        std::cout << "[hover] count=" << m_state.hoveredIds.size() << " ids:";
        int n = 0;
        for (auto id : m_state.hoveredIds)
        {
            std::cout << " " << id;
            if (++n >= 10) { std::cout << " ..."; break; }
        }
        std::cout << "\n";
    }

    if (m_state.sceneDirty)
    {
        upload_world_lines_to_gpu();
        m_state.sceneDirty = false;
        m_state.requestRedraw = true;
    }
}

void Application::OnMouseMoveClient(int x, int y)
{
    m_state.mouseX = x;
    m_state.mouseY = y;
    m_crosshairs.SetMouseClient(x, y);
}

void Application::OnLeftButtonDownClient(int x, int y)
{
    if (!m_book) return;

    m_state.mouseX = x;
    m_state.mouseY = y;
    m_crosshairs.SetMouseClient(x, y);

    // Use a pixel pick-box. This is the ONLY hit-test geometry in this demo.
    const int sidePx = m_crosshairs.GetPickBoxSizePx();
    const int halfPx = std::max(1, sidePx / 2);

    auto pb = RenderCore::MakePickBox(m_pz, x, y, halfPx, m_lastClientW, m_lastClientH);

    // ---- REGULAR SELECTION TOOL (EntityBook selection) ----
    if (m_state.tool == InteractionState::Tool::Select)
    {
        // "Toggle" selection for any hit ids. We keep it explicit and loud:
        // regular selection mutates the MODEL (EntityBook).
        for (const auto& e : m_book->entities)
        {
            if (e.type != EntityType::Line) continue;
            if (e.screenSpace) continue;
            if (!e.pickable) continue;
            if (!e.visible) continue;
            if (e.tag != EntityTag::User) continue;

            glm::vec2 a(e.line.p0.x, e.line.p0.y);
            glm::vec2 b(e.line.p1.x, e.line.p1.y);
            if (AppCore::SegmentIntersectsAabb2D(a, b, pb.worldMin, pb.worldMax))
            {
                AppCore::ToggleSelection(*m_book, m_state, e.ID);
            }
        }

        m_state.sceneDirty = true;
        return;
    }

    // ---- GRIPS SELECTION TOOL (InteractionState.gripsIds) ----
    if (m_state.tool == InteractionState::Tool::SelectGrips)
    {
        // Grips selection is a SEPARATE set stored in InteractionState.
        // We route it through AppCoreLib to keep "grips selection dogma" in one place.
        AppCore::ApplyGripsFromAabb(*m_book, m_state, pb.worldMin, pb.worldMax, AppCore::GripsApplyMode::Toggle);
        return;
    }
}

void Application::OnKeyDown(WPARAM key, bool /*ctrlDown*/)
{
    switch (key)
    {
    case 'S':
        m_state.tool = InteractionState::Tool::Select;
        m_state.requestRedraw = true;
        break;

    case 'G':
        m_state.tool = InteractionState::Tool::SelectGrips;
        m_state.requestRedraw = true;
        break;

    case VK_ESCAPE:
        if (m_book)
        {
            AppCore::ClearSelection(*m_book, m_state); // clears model selection
            AppCore::ClearGrips(m_state);              // clears grips selection
        }
        break;
    }
}

void Application::Render(float aspect)
{
    if (!m_book) return;

    (void)aspect; // this app uses pan/zoom controller mvp

    update_viewport_from_gl();

    glClearColor(0.08f, 0.08f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(m_program);

    // -------------------------
    // pass 1: world lines
    // -------------------------
    draw_world_lines_pass();

    // -------------------------
    // pass 2: overlay lines
    // -------------------------
    m_overlayVertices.clear();
    build_overlay_lines_for_crosshairs();
    build_overlay_lines_for_grips();
    upload_overlay_lines_to_gpu();
    draw_overlay_lines_pass();
}

#include "Application.h"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <string>
#include <filesystem>

// RenderCoreLib helper that enforces TextEntity.font is set.
#include "TextEntityUtil.h"
#include "GripsOps.h"
#include "GripsOverlay.h"
bool Application::RebuildHoverSet()
{
    if (!m_book) return false;
    if (m_lastClientW <= 0 || m_lastClientH <= 0) return false;

    const int sidePx = m_crosshairs.GetPickBoxSizePx();
    const int halfPx = std::max(1, sidePx / 2);

    auto pb = RenderCore::MakePickBox(m_pz, m_state.mouseX, m_state.mouseY, halfPx, m_lastClientW, m_lastClientH);

    return AppCore::RebuildHoverSet(*m_book, m_state, pb.worldMin, pb.worldMax);
}

void Application::OnLeftButtonDownClient(int x, int y)
{
    if (!m_book)
        return;

    // keep mouse state consistent even if caller didn't send WM_MOUSEMOVE
    m_state.mouseX = x;
    m_state.mouseY = y;
    m_crosshairs.SetMouseClient(x, y);

    const glm::vec2 w = m_pz.ScreenToWorld(x, y);
    const glm::vec3 p(glm::vec3(w.x, w.y, 0.0f));

    // Two-point LINE creation with a temporary jig.
    if (!m_lineHasFirst)
    {
        m_lineHasFirst = true;
        m_lineP0 = p;
        m_lineP1 = p;
        m_jigDirty = true;
        m_state.requestRedraw = true;
        return;
    }

    // Second click: commit line into EntityBook and clear jig
    Entity e{};
    e.ID = m_nextId++;
    e.type = EntityType::Line;
    e.tag = EntityTag::User;
    e.screenSpace = false;
    e.pickable = true;
    e.visible = true;

    e.line.p0 = m_lineP0;
    e.line.p1 = p;
    e.line.color = glm::vec4(1, 1, 1, 1);
    e.line.width = 1.0f;

    m_book->AddEntity(e);

    m_lineHasFirst = false;
    m_jigDirty = true;

    MarkDirty(); // rebuild scene VBO (new entity)
}


static void LogOnce(const char* key, const std::string& msg)
{
    static std::unordered_set<std::string> seen;
    if (seen.insert(key).second)
        std::cout << msg << "\n";
}

#include <glm/gtc/matrix_transform.hpp> // glm::ortho

static void DebugPrintOnce(const char* key, const std::string& msg)
{
    static std::unordered_set<std::string> seen;
    if (seen.insert(key).second)
        std::cout << msg << "\n";
}

Application::Application() = default;

Application::~Application()
{
    // In case Shutdown wasn't called.
    Shutdown();
}



void Application::OnKeyDown(WPARAM key, bool /*ctrlDown*/)
{
    if (!m_book)
        return;

    if (key == VK_ESCAPE)
    {
        m_state.waitingForSecondClick = false;
        m_state.rectMode = InteractionState::RectMode::None;
        m_selWindow.Reset();

        m_state.ClearSelection(*m_book);
        AppCore::ClearGrips(m_state);
        m_state.hoveredIds.clear();
        MarkDirty();

        if (m_state.tool == InteractionState::Tool::Erase)
        {
            m_state.tool = InteractionState::Tool::SelectGrips;
            m_state.lastErased = 0;
        }

        // ESC always returns to grips tool in your current logic
        m_crosshairs.SetMode(RenderCore::CrosshairsMode::PointPick);
        return;
    }

    // Start/cancel ERASE command
    if (key == 'E')
    {
        const bool enteringErase = (m_state.tool == InteractionState::Tool::SelectGrips);

        if (enteringErase)
            m_state.tool = InteractionState::Tool::Erase;
        else
            m_state.tool = InteractionState::Tool::SelectGrips;

        m_state.ClearSelection(*m_book);
        AppCore::ClearGrips(m_state);
        m_state.lastErased = 0;

        m_state.waitingForSecondClick = false;
        m_state.rectMode = InteractionState::RectMode::None;
        m_selWindow.Reset();

        // IMPORTANT: crosshairs follow tool state
        if (m_state.tool == InteractionState::Tool::Erase)
            m_crosshairs.SetMode(RenderCore::CrosshairsMode::PickerOnly);      // box only
        else
            m_crosshairs.SetMode(RenderCore::CrosshairsMode::PointPick); // grips mode

        return;
    }

    if (m_state.tool == InteractionState::Tool::Erase)
    {
        if (key == VK_RETURN)
        {
            std::size_t before = m_book->entities.size();

            m_book->RemoveIf([&](const Entity& e)
                {
                    if (e.type != EntityType::Line) return false;
                    if (e.screenSpace) return false;
                    if (e.tag != EntityTag::User) return false;
                    return e.selected;
                });

            std::size_t after = m_book->entities.size();
            m_state.lastErased = (int)(before - after);
            m_state.totalErased += m_state.lastErased;

            // return to grips mode with no selection
            m_state.tool = InteractionState::Tool::SelectGrips;
            m_state.waitingForSecondClick = false;
            m_state.rectMode = InteractionState::RectMode::None;
            m_selWindow.Reset();

            m_state.hoveredIds.clear();
            m_state.ClearSelection(*m_book);
            AppCore::ClearGrips(m_state);
            MarkDirty();

            m_crosshairs.SetMode(RenderCore::CrosshairsMode::PointPick);
            return;
        }

        if (key == 'W')
        {
            m_state.rectMode = InteractionState::RectMode::Window;
            m_state.waitingForSecondClick = true;
            m_selWindow.Reset();

            // W/C => point-pick crosshairs (lines only, no center box)
            m_crosshairs.SetMode(RenderCore::CrosshairsMode::PointPick);
            return;
        }

        if (key == 'C')
        {
            m_state.rectMode = InteractionState::RectMode::Crossing;
            m_state.waitingForSecondClick = true;
            m_selWindow.Reset();

            // W/C => point-pick crosshairs (lines only, no center box)
            m_crosshairs.SetMode(RenderCore::CrosshairsMode::PointPick);
            return;
        }
    }
}

void Application::OnMouseMoveClient(int x, int y)
{
    m_state.mouseX = x;
    m_state.mouseY = y;

    // Crosshairs expects Win32 client coords (origin top-left, Y down)
    m_crosshairs.SetMouseClient(x, y);

    // Live jig update (second point follows cursor)
    if (m_lineHasFirst)
    {
        const glm::vec2 w = m_pz.ScreenToWorld(x, y);
        m_lineP1 = glm::vec3(w.x, w.y, 0.0f);
        m_jigDirty = true;
        m_state.requestRedraw = true;
    }
}

void Application::LogSink(EntityCore::LogLevel level,
                          const char* category,
                          const char* event,
                          std::string_view message)
{
    // TRACE is extremely noisy (AddEntity logs for every grid line).
    // It will make the UI feel "hung" due to console I/O.
    if (level == EntityCore::LogLevel::Trace)
        return;
    const char* lvl = "TRACE";
    switch (level)
    {
    case EntityCore::LogLevel::Trace: lvl = "TRACE"; break;
    case EntityCore::LogLevel::Info:  lvl = "INFO "; break;
    case EntityCore::LogLevel::Warn:  lvl = "WARN "; break;
    case EntityCore::LogLevel::Error: lvl = "ERROR"; break;
    default: break;
    }

    std::cout << "[" << lvl << "] " << category << "::" << event;
    if (!message.empty())
        std::cout << " | " << message;
    std::cout << "\n";
}

GLuint Application::Compile(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok = GL_FALSE;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[2048]{};
        glGetShaderInfoLog(s, (GLsizei)sizeof(log), nullptr, log);
        std::cout << "Shader compile failed: " << log << "\n";
    }
    return s;
}

GLuint Application::Link(GLuint vs, GLuint fs)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);

    GLint ok = GL_FALSE;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[2048]{};
        glGetProgramInfoLog(p, (GLsizei)sizeof(log), nullptr, log);
        std::cout << "Program link failed: " << log << "\n";
    }
    return p;
}

void Application::Initialize()
{
    EntityCore::SetLogSink(&Application::LogSink);
    std::cout << "Application::Initialize\n";

    // AppCoreLib state references RenderCoreLib selection window but does not own it.
    m_state.selectionWindow = &m_selWindow;

    // Create EntityBook via factory so you get capacity + init logs
    auto created = CreateEntityBook(8192); // pick a capacity you want
    if (!created)
    {
        // Factory should already have logged CreateFailed w/ message.
        // Optionally print something here too, but avoid double-logging.
        std::cout << "FATAL: EntityBook creation failed\n";
        return; // or throw / set a failure flag
    }
    m_book = std::move(*created);

    // Minimal GL 3.3 shader for colored lines (GL_LINES)
    const char* vsSrc = R"GLSL(
        #version 330 core
        layout(location = 0) in vec3 aPos;
        layout(location = 1) in vec4 aColor;

        uniform mat4 uMvp;
        out vec4 vColor;

        void main()
        {
            vColor = aColor;
            gl_Position = uMvp * vec4(aPos, 1.0);
        }
    )GLSL";

    const char* fsSrc = R"GLSL(
        #version 330 core
        in vec4 vColor;
        out vec4 FragColor;

        void main()
        {
            FragColor = vColor;
        }
    )GLSL";

    GLuint vs = Compile(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = Compile(GL_FRAGMENT_SHADER, fsSrc);
    m_program = Link(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    m_uMvp = glGetUniformLocation(m_program, "uMvp");

    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_hudVbo);
    glGenBuffers(1, &m_cursorVbo);
    glGenBuffers(1, &m_jigVbo);

    // crosshair defaults
    m_crosshairs.SetPickBoxSizePx(12);
    m_crosshairs.SetMode(RenderCore::CrosshairsMode::PointPick);
    m_crosshairs.SetBackground(RenderCore::CrosshairsBackground::Dark);


    // -----------------------------------------------------
    // Determine font directory (safe + deterministic)
    // -----------------------------------------------------

    char* hershey_dir = nullptr;
    size_t hershey_len = 0;

    _dupenv_s(&hershey_dir, &hershey_len, "HERSHEY_FONTS_DIR");

    std::string fontDir = (hershey_dir && hershey_len > 0)
        ? std::string(hershey_dir)
        : "C:\\ProgramData\\hershey-fonts";

    if (hershey_dir)
        free(hershey_dir);

    // Print once what THIS process sees
    std::cout << "PanZoom using HERSHEY_FONTS_DIR = " << fontDir << "\n";

    // Check expected file explicitly
    std::filesystem::path expected = std::filesystem::path(fontDir) / "rowmans.jhf";

    if (!std::filesystem::exists(expected))
    {
        std::cout << "ERROR: Missing font file: " << expected << "\n";
    }
    else
    {
        std::cout << "Found font file: " << expected << "\n";
    }

    // -----------------------------------------------------
    // Load font
    // -----------------------------------------------------

    m_hudFont = hershey_font_load("rowmans");

    if (!m_hudFont)
    {
        std::cout << "FATAL: hershey_font_load('rowmans') returned NULL\n";
        throw std::runtime_error("Hershey font load failed");
    }

    std::cout << "hershey_font_load succeeded.\n";

    // -----------------------------------------------------
    // User geometry (persists). Background grid is maintained separately.
    // -----------------------------------------------------
    {
        DragonCurve dragon;
        const int iterations = 12; // stress test; use 10 if too heavy
        glm::vec3 origin = { -200.0f, 0.0f, 0.0f };

        const auto segs = dragon.Build(iterations, origin);

        auto addUserLine = [&](glm::vec3 a, glm::vec3 b, glm::vec4 color)
        {
            Entity e;
            e.ID = m_nextId++;
            e.type = EntityType::Line;
            e.tag = EntityTag::User;
            e.drawOrder = 1; // above grid
            e.screenSpace = false;
            e.pickable = true;
            e.visible = true;
            e.line.p0 = a;
            e.line.p1 = b;
            e.line.color = color;
            e.line.width = 1.0f;
            m_book->AddEntity(e);
        };

        glm::vec4 c(0.85f, 0.85f, 0.95f, 1.0f);
        for (const auto& s : segs)
            addUserLine(s.a, s.b, c);

        m_book->SortByDrawOrder();
    }

    // Seed an initial grid using the intended startup client size.
    // The first Render() will re-sync based on the real viewport.
    m_lastClientW = 800;
    m_lastClientH = 600;
    m_pz.SetViewport(m_lastClientW, m_lastClientH);
    RenderCore::UpdateBackgroundGrid(*m_book, m_pz, m_lastClientW, m_lastClientH, m_nextId, m_gridCache);

    UploadLinesToGpu();
}

void Application::Update(float /*inDeltaTime*/)
{
    // Hover highlighting follows the cursor.
    if (RebuildHoverSet())
        MarkDirty();

    // Keep the infinite grid aligned to the current view.
    if (m_book && m_lastClientW > 0 && m_lastClientH > 0)
    {
        if (RenderCore::UpdateBackgroundGrid(*m_book, m_pz, m_lastClientW, m_lastClientH, m_nextId, m_gridCache))
            MarkDirty();
    }

    if (m_state.sceneDirty)
    {
        UploadLinesToGpu();
        m_state.sceneDirty = false;
    }
}

void Application::Render(float /*inAspectRatio*/)
{
    GLint vp[4]{};
    glGetIntegerv(GL_VIEWPORT, vp);
    const int w = vp[2];
    const int h = vp[3];

    SyncViewportAndRebuildSceneIfNeeded(w, h);

    // Feed viewport into pan/zoom + overlays.
    m_pz.SetViewport(w, h);
    m_crosshairs.SetViewport(w, h);
    const glm::mat4 sceneMvp = m_pz.GetMVP();

    // Update jig VBO (model-space line) if active.
    if (m_lineHasFirst && m_jigDirty)
    {
        Vertex v[2]{};
        v[0].pos = m_lineP0;
        v[1].pos = m_lineP1;
        // Bright, visible color for the jig
        v[0].color = glm::vec4(0.2f, 0.8f, 1.0f, 1.0f);
        v[1].color = v[0].color;

        glBindBuffer(GL_ARRAY_BUFFER, m_jigVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);
        m_jigDirty = false;
    }

    DrawScenePass(sceneMvp);
    DrawOverlayPass(w, h);
    DrawHudPass(w, h, sceneMvp);
}

void Application::SyncViewportAndRebuildSceneIfNeeded(int w, int h)
{
    if (w <= 0 || h <= 0)
        return;

    if (w == m_lastClientW && h == m_lastClientH)
        return;

    m_lastClientW = w;
    m_lastClientH = h;

    // Background grid is view-aligned, so we rebuild the grid entities here too.
    if (m_book)
    {
        m_pz.SetViewport(w, h);
        if (RenderCore::UpdateBackgroundGrid(*m_book, m_pz, w, h, m_nextId, m_gridCache))
        {
            UploadLinesToGpu();
            std::cout << "Viewport " << w << "x" << h << " -> rebuilt grid\n";
        }
    }
}

void Application::BindLineVertexLayout()
{
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));
}

void Application::DrawScenePass(const glm::mat4& mvp)
{
    glUseProgram(m_program);
    glUniformMatrix4fv(m_uMvp, 1, GL_FALSE, &mvp[0][0]);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    BindLineVertexLayout();
    glDrawArrays(GL_LINES, 0, (GLsizei)m_vertices.size());

    if (m_lineHasFirst)
    {
        glBindBuffer(GL_ARRAY_BUFFER, m_jigVbo);
        BindLineVertexLayout();
        glDrawArrays(GL_LINES, 0, 2);
    }
}

void Application::DrawOverlayPass(int w, int h)
{
    // Pixel-space MVP: origin bottom-left, X right, Y up
    const glm::mat4 overlayMvp = glm::ortho(0.0f, (float)w, 0.0f, (float)h, -1.0f, +1.0f);

    std::vector<LineEntity> overlayLines;
    m_crosshairs.BuildLines(overlayLines);

    // If a W/C selection is active (ERASE), draw its outline too
    if (m_selWindow.IsActive())
    {
        const glm::vec4 rc = (m_state.rectMode == InteractionState::RectMode::Crossing)
            ? glm::vec4(0.95f, 0.35f, 0.35f, 1.0f)  // red-ish for crossing
            : glm::vec4(0.35f, 0.95f, 0.35f, 1.0f); // green-ish for window

        m_selWindow.BuildOverlayOutline(w, h, rc, 1.0f, overlayLines);
    }

    // -------------------------------------------------
    // GRIPS OVERLAY (screen-space, constant pixel size)
    // -------------------------------------------------
    if (m_state.tool == InteractionState::Tool::SelectGrips && m_book && !m_state.gripsIds.empty())
    {
        const glm::vec4 gripColor(0.0f, 0.55f, 1.0f, 1.0f); // blue-ish

        std::vector<RenderCore::OverlayVertex> grips;
        RenderCore::BuildGripsOverlayLines(*m_book,
                                           m_state.gripsIds,
                                           m_pz,
                                           w,
                                           h,
                                           /*halfSizePx*/ 4,
                                           gripColor,
                                           grips);

        // Append into our existing LineEntity list.
        for (size_t i = 0; i + 1 < grips.size(); i += 2)
        {
            LineEntity L;
            L.p0 = grips[i].pos;
            L.p1 = grips[i + 1].pos;
            L.color = grips[i].color;
            L.width = 1.0f;
            overlayLines.push_back(L);
        }
    }

    m_cursorVertices.clear();
    m_cursorVertices.reserve(overlayLines.size() * 2);
    for (const auto& L : overlayLines)
    {
        m_cursorVertices.push_back({ L.p0, L.color });
        m_cursorVertices.push_back({ L.p1, L.color });
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_cursorVbo);
    glBufferData(GL_ARRAY_BUFFER,
        (GLsizeiptr)(m_cursorVertices.size() * sizeof(Vertex)),
        m_cursorVertices.data(),
        GL_DYNAMIC_DRAW);

    glUseProgram(m_program);
    glUniformMatrix4fv(m_uMvp, 1, GL_FALSE, &overlayMvp[0][0]);
    BindLineVertexLayout();
    glDrawArrays(GL_LINES, 0, (GLsizei)m_cursorVertices.size());
}


void Application::DrawHudPass(int w, int h, const glm::mat4& sceneMvp)
{
    if (!m_hudFont)
        return;

    // Pixel-space MVP: origin bottom-left, X right, Y up
    const glm::mat4 hudMvp = glm::ortho(0.0f, (float)w, 0.0f, (float)h, -1.0f, +1.0f);

    auto buildRectTriangles = [&](float x0, float y0, float x1, float y1, const glm::vec4& color, std::vector<Vertex>& out)
    {
        out.clear();
        out.reserve(6);
        const glm::vec3 a(x0, y0, 0.0f);
        const glm::vec3 b(x1, y0, 0.0f);
        const glm::vec3 c(x1, y1, 0.0f);
        const glm::vec3 d(x0, y1, 0.0f);

        out.push_back({ a, color });
        out.push_back({ b, color });
        out.push_back({ c, color });

        out.push_back({ a, color });
        out.push_back({ c, color });
        out.push_back({ d, color });
    };

    // World origin in pixels (debug)
    const glm::vec4 clip = sceneMvp * glm::vec4(0, 0, 0, 1);
    const glm::vec3 ndc = glm::vec3(clip) / clip.w; // -1..+1
    const float sx = (ndc.x * 0.5f + 0.5f) * (float)w;
    const float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * (float)h;

    std::ostringstream oss;
    oss << "Ortho: " << m_pz.GetModeName()
        << " | pan=(" << m_pz.pan.x << "," << m_pz.pan.y << ")"
        << " zoom=" << m_pz.zoom
        << " | world(0,0)->screen(" << (int)sx << "," << (int)sy << ")"
        << " | cmd=" << ((m_state.tool == InteractionState::Tool::Erase) ? "ERASE" : "GRIPS")
        << " sel=" << m_state.SelectedCount(m_book.get())
        << " hover=" << (int)m_state.hoveredIds.size()
        << " | erased(last=" << m_state.lastErased << " total=" << m_state.totalErased << ")";

    if (m_state.tool == InteractionState::Tool::Erase)
        oss << " | [W]=window [C]=crossing [ENTER]=commit [ESC]=cancel";
    else
        oss << " | [E]=erase [ESC]=clear";

    TextEntity t;
    RenderCore::InitTextEntityOrDie(
        t,
        oss.str(),
        m_hudFont,
        RenderCore::TextInitPolicy::AssertAndMessageBox,
        "EntityHighlighting HUD"
    );

    t.wordWrapEnabled = true;
    t.hAlign = TextHAlign::Left;
    t.boxWidth = std::max(200.0f, (float)w * 0.70f);
    t.boxHeight = 110.0f;
    t.position = { 12.0f, (float)h - 12.0f - t.boxHeight, 0.0f }; // Y-up pixel coordinates
    t.scale = 0.35f; // 65% smaller
    t.strokeWidth = 1.0f;
    t.color = { 1, 1, 0, 1 };
    t.backgroundEnabled = true;
    t.backgroundColor = { 0, 0, 0, 0.75f };
    t.backgroundPadding = 10.0f;
    t.backgroundOutlineEnabled = true;
    t.backgroundOutlineColor = { 1, 1, 1, 0.35f };
    t.backgroundOutlineThickness = 1.0f;

    std::vector<LineEntity> lines;
    HersheyTextBuilder::BuildLines(t, lines);

    // Bottom-right HUD (mode/status) pinned to client corner.
    TextEntity br;
    RenderCore::InitTextEntityOrDie(
        br,
        std::string("Client ") + std::to_string(w) + "x" + std::to_string(h) +
            "\nMode: " + ((m_state.tool == InteractionState::Tool::Erase) ? "ERASE" : "GRIPS"),
        m_hudFont,
        RenderCore::TextInitPolicy::AssertAndMessageBox,
        "EntityHighlighting HUD BR"
    );
    br.wordWrapEnabled = true;
    br.hAlign = TextHAlign::Right;
    br.boxWidth = 220.0f;
    br.boxHeight = 60.0f;
    br.scale = 0.35f;
    br.strokeWidth = 1.0f;
    br.color = { 1, 1, 0, 1 };
    br.backgroundEnabled = true;
    br.backgroundColor = { 0, 0, 0, 0.75f };
    br.backgroundPadding = 10.0f;
    br.backgroundOutlineEnabled = true;
    br.backgroundOutlineColor = { 1, 1, 1, 0.35f };
    br.backgroundOutlineThickness = 1.0f;

    // Position is the lower-left of its box in Y-up coords.
    br.position = { (float)w - 12.0f - br.boxWidth, 12.0f, 0.0f };

    std::vector<LineEntity> linesBR;
    HersheyTextBuilder::BuildLines(br, linesBR);

    // Combine lines
    lines.insert(lines.end(), linesBR.begin(), linesBR.end());
    DebugPrintOnce("HUD_segments", std::string("HUD: BuildLines produced ") + std::to_string(lines.size()) + " segments.");

    // HUD background triangles (two boxes)
    static int s_lastW = 0, s_lastH = 0;
    if (s_lastW != w || s_lastH != h)
    {
        std::cout << "ClientArea " << w << "x" << h << "\n";
        s_lastW = w; s_lastH = h;
    }

    std::vector<Vertex> bgVerts;
    // Top-left box
    {
        const float pad = std::max(0.0f, t.backgroundPadding);
        const float x0 = t.position.x - pad;
        const float y0 = t.position.y - pad;
        const float x1 = t.position.x + t.boxWidth + pad;
        const float y1 = t.position.y + t.boxHeight + pad;
        buildRectTriangles(x0, y0, x1, y1, t.backgroundColor, bgVerts);

        glBindBuffer(GL_ARRAY_BUFFER, m_hudVbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(bgVerts.size() * sizeof(Vertex)), bgVerts.data(), GL_DYNAMIC_DRAW);

        glUseProgram(m_program);
        glUniformMatrix4fv(m_uMvp, 1, GL_FALSE, &hudMvp[0][0]);
        BindLineVertexLayout();
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)bgVerts.size());
    }
    // Bottom-right box
    {
        const float pad = std::max(0.0f, br.backgroundPadding);
        const float x0 = br.position.x - pad;
        const float y0 = br.position.y - pad;
        const float x1 = br.position.x + br.boxWidth + pad;
        const float y1 = br.position.y + br.boxHeight + pad;
        buildRectTriangles(x0, y0, x1, y1, br.backgroundColor, bgVerts);

        glBindBuffer(GL_ARRAY_BUFFER, m_hudVbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(bgVerts.size() * sizeof(Vertex)), bgVerts.data(), GL_DYNAMIC_DRAW);

        glUseProgram(m_program);
        glUniformMatrix4fv(m_uMvp, 1, GL_FALSE, &hudMvp[0][0]);
        BindLineVertexLayout();
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)bgVerts.size());
    }

    // HUD outline rectangles (lines)
    {
        std::vector<LineEntity> outlines;
        outlines.reserve(8);
        auto addBox = [&](const TextEntity& tt)
        {
            const float pad = std::max(0.0f, tt.backgroundPadding);
            const float x0 = tt.position.x - pad;
            const float y0 = tt.position.y - pad;
            const float x1 = tt.position.x + tt.boxWidth + pad;
            const float y1 = tt.position.y + tt.boxHeight + pad;

            LineEntity e;
            e.color = tt.backgroundOutlineColor;
            e.width = tt.backgroundOutlineThickness;

            e.p0 = { x0,y0,0 }; e.p1 = { x1,y0,0 }; outlines.push_back(e);
            e.p0 = { x1,y0,0 }; e.p1 = { x1,y1,0 }; outlines.push_back(e);
            e.p0 = { x1,y1,0 }; e.p1 = { x0,y1,0 }; outlines.push_back(e);
            e.p0 = { x0,y1,0 }; e.p1 = { x0,y0,0 }; outlines.push_back(e);
        };
        addBox(t);
        addBox(br);

        // append to text line list so we can draw outlines as GL_LINES with the text
        lines.insert(lines.begin(), outlines.begin(), outlines.end());
    }


m_hudVertices.clear();
    m_hudVertices.reserve(lines.size() * 2);
    for (const auto& L : lines)
    {
        m_hudVertices.push_back({ L.p0, L.color });
        m_hudVertices.push_back({ L.p1, L.color });
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_hudVbo);
    glBufferData(GL_ARRAY_BUFFER,
        (GLsizeiptr)(m_hudVertices.size() * sizeof(Vertex)),
        m_hudVertices.data(),
        GL_DYNAMIC_DRAW);

    glUseProgram(m_program);
    glUniformMatrix4fv(m_uMvp, 1, GL_FALSE, &hudMvp[0][0]);
    BindLineVertexLayout();
    glDrawArrays(GL_LINES, 0, (GLsizei)m_hudVertices.size());
}

void Application::Shutdown()
{
    // Avoid double shutdown from dtor
    if (m_program == 0 && m_vbo == 0)
        return;

    std::cout << "Application::Shutdown\n";

    EntityCore::SetLogSink(nullptr);

    if (m_vbo)
    {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_hudVbo)
    {
        glDeleteBuffers(1, &m_hudVbo);
        m_hudVbo = 0;
    }

    if (m_hudFont)
    {
        hershey_font_free(m_hudFont);
        m_hudFont = nullptr;
    }
    if (m_program)
    {
        glDeleteProgram(m_program);
        m_program = 0;
    }
    if (m_cursorVbo)
    {
        glDeleteBuffers(1, &m_cursorVbo);
        m_cursorVbo = 0;
    }
    if (m_jigVbo)
    {
        glDeleteBuffers(1, &m_jigVbo);
        m_jigVbo = 0;
    }
    m_vertices.clear();
    if (m_book) m_book->Clear();
}

void Application::UploadLinesToGpu()
{
    if (!m_book) return;

    m_vertices.clear();
    m_vertices.reserve(m_book->entities.size() * 2);

    const glm::vec4 kHighlight(1.0f, 1.0f, 0.0f, 1.0f); // yellow

    for (const auto& e : m_book->entities)
    {
        if (e.type != EntityType::Line)
            continue;

        glm::vec4 c = RenderCore::EffectiveLineColor(e, &m_state.hoveredIds, kHighlight);

        // In GRIPS tool mode, also highlight entities in the grips selection set.
        if (m_state.tool == InteractionState::Tool::SelectGrips &&
            (m_state.gripsIds.find(e.ID) != m_state.gripsIds.end()))
        {
            c = kHighlight;
        }

        m_vertices.push_back({ e.line.p0, c });
        m_vertices.push_back({ e.line.p1, c });
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(m_vertices.size() * sizeof(Vertex)),
                 m_vertices.data(),
                 GL_DYNAMIC_DRAW);
}


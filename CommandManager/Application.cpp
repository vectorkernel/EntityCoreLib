#include "Application.h"

#include <iostream>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <string>
#include <filesystem>

#include "HoverOps.h"
#include "GripsOps.h"
#include "SelectionOps.h"

#include "LineColorUtil.h"
#include "GripsOverlay.h"


static bool SegmentIntersectsAabb2D(const glm::vec2& a, const glm::vec2& b,
    const glm::vec2& mn, const glm::vec2& mx)
{
    // Quick reject: segment bbox vs aabb
    glm::vec2 smin(std::min(a.x, b.x), std::min(a.y, b.y));
    glm::vec2 smax(std::max(a.x, b.x), std::max(a.y, b.y));
    if (smax.x < mn.x || smin.x > mx.x || smax.y < mn.y || smin.y > mx.y)
        return false;

    // Liang–Barsky style clip in 2D
    glm::vec2 d = b - a;
    float t0 = 0.0f, t1 = 1.0f;

    auto clip = [&](float p, float q) -> bool {
        if (std::abs(p) < 1e-8f) return q >= 0.0f;
        float r = q / p;
        if (p < 0.0f) { if (r > t1) return false; if (r > t0) t0 = r; }
        else { if (r < t0) return false; if (r < t1) t1 = r; }
        return true;
        };

    if (!clip(-d.x, a.x - mn.x)) return false;
    if (!clip(d.x, mx.x - a.x)) return false;
    if (!clip(-d.y, a.y - mn.y)) return false;
    if (!clip(d.y, mx.y - a.y)) return false;

    return true;
}



void Application::OnLeftButtonDownClient(int x, int y)
{
    if (!m_book) return;

    // Crosshair policy is driven by InteractionState.
    // - grips tool: show pick square
    // - erase with W/C active: point pick crosshairs
    if (m_state.tool == InteractionState::Tool::Erase && m_state.rectMode != InteractionState::RectMode::None)
        m_crosshairs.SetMode(RenderCore::CrosshairsMode::PointPick);
    else
        m_crosshairs.SetMode(RenderCore::CrosshairsMode::GripsSelection);

    // keep mouse state consistent even if caller didn't send WM_MOUSEMOVE
    m_state.mouseX = x;
    m_state.mouseY = y;
    m_crosshairs.SetMouseClient(x, y);

    // Use a pixel pick-box (the ONLY hit-test primitive in this demo)
    const int sidePx = m_crosshairs.GetPickBoxSizePx();
    const int halfPx = std::max(1, sidePx / 2);

    auto pb = RenderCore::MakePickBox(m_pz, x, y, halfPx, m_lastClientW, m_lastClientH);

    // -------------------------------------------------
    // ERASE: two-click rectangle selection (W/C)
    // -------------------------------------------------
    if (m_state.tool == InteractionState::Tool::Erase && m_state.waitingForSecondClick)
    {
        if (!m_selWindow.IsActive())
        {
            // First click
            m_selWindow.BeginClient(x, y);
            m_selWindow.UpdateClient(x, y);
            return;
        }

        // Second click -> finalize
        m_selWindow.UpdateClient(x, y);
        m_selWindow.End();

        // Build WORLD AABB from the client rectangle
        int x0, y0, x1, y1;
        m_selWindow.GetClientRect(x0, y0, x1, y1);

        // Convert 4 corners to world and take min/max
        glm::vec2 w00 = m_pz.ScreenToWorld((float)x0, (float)y0);
        glm::vec2 w10 = m_pz.ScreenToWorld((float)x1, (float)y0);
        glm::vec2 w01 = m_pz.ScreenToWorld((float)x0, (float)y1);
        glm::vec2 w11 = m_pz.ScreenToWorld((float)x1, (float)y1);

        glm::vec2 mn(
            std::min(std::min(w00.x, w10.x), std::min(w01.x, w11.x)),
            std::min(std::min(w00.y, w10.y), std::min(w01.y, w11.y)));
        glm::vec2 mx(
            std::max(std::max(w00.x, w10.x), std::max(w01.x, w11.x)),
            std::max(std::max(w00.y, w10.y), std::max(w01.y, w11.y)));

        const bool crossing = (m_state.rectMode == InteractionState::RectMode::Crossing);

        bool any = false;
        for (auto& e : m_book->entities)
        {
            if (!e.visible) continue;
            if (!e.pickable) continue;
            if (e.type != EntityType::Line) continue;
            if (e.screenSpace) continue;
            if (e.tag != EntityTag::User) continue;

            glm::vec2 a(e.line.p0.x, e.line.p0.y);
            glm::vec2 b(e.line.p1.x, e.line.p1.y);

            bool hit = false;
            if (!crossing)
            {
                // window: both endpoints must be inside
                const bool aIn = (a.x >= mn.x && a.x <= mx.x && a.y >= mn.y && a.y <= mx.y);
                const bool bIn = (b.x >= mn.x && b.x <= mx.x && b.y >= mn.y && b.y <= mx.y);
                hit = aIn && bIn;
            }
            else
            {
                // crossing: any intersection
                hit = AppCore::SegmentIntersectsAabb2D(a, b, mn, mx);
            }

            if (hit)
            {
                m_book->Select(e);
                any = true;
            }
        }

        if (any)
            m_state.sceneDirty = true;

        // Exit rectangle selection mode, stay in ERASE command
        m_state.waitingForSecondClick = false;
        m_state.rectMode = InteractionState::RectMode::None;
        m_selWindow.Reset();
        return;
    }

    // -------------------------------------------------
    // GRIPS TOOL: update grips selection set (InteractionState.gripsIds)
    // -------------------------------------------------
    if (m_state.tool == InteractionState::Tool::SelectGrips)
    {
        const bool changed = AppCore::ApplyGripsFromAabb(*m_book, m_state, pb.worldMin, pb.worldMax,
                                                        AppCore::GripsApplyMode::Toggle);
        if (changed)
            m_state.sceneDirty = true; // we highlight grips set in world pass
        return;
    }

    // -------------------------------------------------
    // ERASE: pick-box adds to EntityBook selection
    // -------------------------------------------------
    if (m_state.tool == InteractionState::Tool::Erase)
    {
        bool any = false;
        for (auto& e : m_book->entities)
        {
            if (!e.visible) continue;
            if (!e.pickable) continue;
            if (e.type != EntityType::Line) continue;
            if (e.screenSpace) continue;
            if (e.tag != EntityTag::User) continue;

            glm::vec2 a(e.line.p0.x, e.line.p0.y);
            glm::vec2 b(e.line.p1.x, e.line.p1.y);

            if (AppCore::SegmentIntersectsAabb2D(a, b, pb.worldMin, pb.worldMax))
            {
                m_book->Select(e);
                any = true;
            }
        }

        if (any)
            m_state.sceneDirty = true;
    }
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

// -------------------------------------------------
// CommandManager UI integration helpers
// -------------------------------------------------
void Application::StartEraseCommand()
{
    if (!m_book) return;
    if (m_state.tool == InteractionState::Tool::Erase) return;

    m_state.tool = InteractionState::Tool::Erase;
    AppCore::ClearSelection(*m_book, m_state);
    AppCore::ClearGrips(m_state);
    m_state.lastErased = 0;

    m_state.waitingForSecondClick = false;
    m_state.rectMode = InteractionState::RectMode::None;
    m_selWindow.Reset();
    MarkDirty();
}

void Application::CancelCommand()
{
    if (!m_book) return;

    m_state.waitingForSecondClick = false;
    m_state.rectMode = InteractionState::RectMode::None;
    m_selWindow.Reset();

    AppCore::ClearSelection(*m_book, m_state);
    AppCore::ClearGrips(m_state);
    m_state.hoveredIds.clear();
    MarkDirty();

    if (m_state.tool == InteractionState::Tool::Erase)
    {
        m_state.tool = InteractionState::Tool::SelectGrips;
        m_state.lastErased = 0;
    }
}

void Application::CommitErase()
{
    if (!m_book) return;
    if (m_state.tool != InteractionState::Tool::Erase) return;

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
    AppCore::ClearSelection(*m_book, m_state);
    AppCore::ClearGrips(m_state);
    MarkDirty();
}

void Application::BeginWindowSelection(bool crossing)
{
    if (!m_book) return;
    if (m_state.tool != InteractionState::Tool::Erase) return;
    m_state.rectMode = crossing ? InteractionState::RectMode::Crossing : InteractionState::RectMode::Window;
    m_state.waitingForSecondClick = true;
    m_selWindow.Reset();
}



void Application::OnKeyDown(WPARAM key, bool /*ctrlDown*/)
{
    if (!m_book) return;

    switch (key)
    {
    case VK_ESCAPE:
        CancelCommand();
        return;

    case 'E':
        if (m_state.tool == InteractionState::Tool::Erase) CancelCommand();
        else StartEraseCommand();
        return;

    case VK_RETURN:
        if (m_state.tool == InteractionState::Tool::Erase) CommitErase();
        return;

    case 'W':
        if (m_state.tool == InteractionState::Tool::Erase) BeginWindowSelection(false);
        return;

    case 'C':
        if (m_state.tool == InteractionState::Tool::Erase) BeginWindowSelection(true);
        return;

    default:
        break;
    }
}

void Application::OnMouseMoveClient(int x, int y)
{
    m_state.mouseX = x;
    m_state.mouseY = y;

    // Crosshairs expects Win32 client coords (origin top-left, Y down)
    m_crosshairs.SetMouseClient(x, y);

    // If we are mid “two-click” rectangle selection, keep updating the live rectangle
    if (m_state.waitingForSecondClick && m_selWindow.IsActive())
        m_selWindow.UpdateClient(x, y);
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

    // crosshair defaults
    m_crosshairs.SetPickBoxSizePx(12);
    m_crosshairs.SetMode(RenderCore::CrosshairsMode::GripsSelection);
    m_crosshairs.SetBackground(RenderCore::CrosshairsBackground::Dark);

    // ---- unified interaction state (AppCoreLib) ----
    m_state.tool = InteractionState::Tool::SelectGrips;
    m_state.phase = InteractionState::Phase::Idle;
    m_state.rectMode = InteractionState::RectMode::None;
    m_state.waitingForSecondClick = false;
    m_state.selectionWindow = &m_selWindow;
    m_state.sceneDirty = true;
    m_state.requestRedraw = true;



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

    // Build the initial 800x600 grid (matches WinMain's intended client size).
    // We'll still auto-rebuild on first Render() based on actual viewport.
    BuildGrid(800, 600);
    UploadLinesToGpu();
}

void Application::Update(float /*inDeltaTime*/ )
{
    if (!m_book) return;

    // Hover highlighting follows the cursor (pick-box around crosshair).
    if (m_lastClientW > 0 && m_lastClientH > 0)
    {
        const int sidePx = m_crosshairs.GetPickBoxSizePx();
        const int halfPx = std::max(1, sidePx / 2);
        auto pb = RenderCore::MakePickBox(m_pz, m_state.mouseX, m_state.mouseY, halfPx, m_lastClientW, m_lastClientH);
        if (AppCore::RebuildHoverSet(*m_book, m_state, pb.worldMin, pb.worldMax))
            m_state.sceneDirty = true;
    }

    if (m_state.sceneDirty)
    {
        UploadLinesToGpu();
        m_state.sceneDirty = false;
    }
}

void Application::Render(float /*inAspectRatio*/)
{

    // Get current client size from GL viewport.
    GLint vp[4]{};
    glGetIntegerv(GL_VIEWPORT, vp);
    const int w = vp[2];
    const int h = vp[3];

    EnsureViewportAndGrid(w, h);

    // Feed viewport into the pan/zoom controller and use its MVP
    m_pz.SetViewport(w, h);
    m_crosshairs.SetViewport(w, h);
    glm::mat4 mvp = m_pz.GetMVP();

    DrawWorldPass(w, h, mvp);
    DrawOverlayPass(w, h);
    DrawHudPass(w, h, mvp);
}

void Application::EnsureViewportAndGrid(int w, int h)
{
    if (w == m_lastClientW && h == m_lastClientH)
        return;

    m_lastClientW = w;
    m_lastClientH = h;

    BuildGrid(w, h);
    UploadLinesToGpu();

    std::cout << "Viewport " << w << "x" << h << " -> rebuilt grid\n";
}

void Application::BindLineVertexLayout()
{
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));
}

void Application::DrawWorldPass(int /*w*/, int /*h*/, const glm::mat4& mvp)
{
    glUseProgram(m_program);
    glUniformMatrix4fv(m_uMvp, 1, GL_FALSE, &mvp[0][0]);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    BindLineVertexLayout();
    glDrawArrays(GL_LINES, 0, (GLsizei)m_vertices.size());
}

void Application::DrawOverlayPass(int w, int h)
{
    // Pixel-space MVP: origin bottom-left, X right, Y up
    glm::mat4 overlayMvp = glm::ortho(0.0f, (float)w, 0.0f, (float)h, -1.0f, +1.0f);

    std::vector<LineEntity> overlayLines;
    // Always draw full crosshairs (GripsSelection mode includes the pick square).
    m_crosshairs.BuildLines(overlayLines);

    // If a W/C selection is active (ERASE), draw its outline too
    if (m_selWindow.IsActive())
    {
        glm::vec4 rc = (m_state.rectMode == InteractionState::RectMode::Crossing)
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

// Convert to our Vertex list
    m_cursorVertices.clear();
    m_cursorVertices.reserve(overlayLines.size() * 2);

    for (const auto& L : overlayLines)
    {
        m_cursorVertices.push_back({ L.p0, L.color });
        m_cursorVertices.push_back({ L.p1, L.color });
    }

    // Upload to GPU
    glBindBuffer(GL_ARRAY_BUFFER, m_cursorVbo);
    glBufferData(GL_ARRAY_BUFFER,
        (GLsizeiptr)(m_cursorVertices.size() * sizeof(Vertex)),
        m_cursorVertices.data(),
        GL_DYNAMIC_DRAW);

    // Draw using same shader, different MVP
    glUseProgram(m_program);
    glUniformMatrix4fv(m_uMvp, 1, GL_FALSE, &overlayMvp[0][0]);

    BindLineVertexLayout();
    glDrawArrays(GL_LINES, 0, (GLsizei)m_cursorVertices.size());
}

void Application::DrawHudPass(int w, int h, const glm::mat4& mvp)
{
    if (!m_hudFont)
        return;

    // 1) Compute where world origin (0,0,0) ends up on screen (pixels)
    glm::vec4 clip = mvp * glm::vec4(0, 0, 0, 1);
    glm::vec3 ndc = glm::vec3(clip) / clip.w; // -1..+1

    float sx = (ndc.x * 0.5f + 0.5f) * (float)w;
    float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * (float)h; // top-left style pixel Y (down)

    // 2) Build HUD message
    std::ostringstream oss;
    oss << "Ortho: " << m_pz.GetModeName()
        << " | pan=(" << m_pz.pan.x << "," << m_pz.pan.y << ")"
        << " zoom=" << m_pz.zoom
        << " | world(0,0)->screen(" << (int)sx << "," << (int)sy << ")"
        << " | cmd=" << ((m_state.tool == InteractionState::Tool::Erase) ? "ERASE" : "GRIPS")
        << " sel=" << (int)m_book->SelectedIds().size()
        << " hover=" << (int)m_state.hoveredIds.size()
        << " grips=" << (int)m_state.gripsIds.size()
        << " | erased(last=" << m_state.lastErased << " total=" << m_state.totalErased << ")";

    if (m_state.tool == InteractionState::Tool::Erase)
        oss << " | [W]=window [C]=crossing [ENTER]=commit [ESC]=cancel";
    else
        oss << " | [E]=erase [ESC]=clear";

    // 3) Convert Hershey text -> LineEntity list
    TextEntity t;
    t.font = m_hudFont;
    t.wordWrapEnabled = false;
    t.hAlign = TextHAlign::Left;

    t.boxWidth = (float)w;
    t.boxHeight = 40.0f;

    // Y-up pixel coordinates: put box near the top-left
    t.position = { 10.0f, (float)h - 10.0f - t.boxHeight, 0.0f };
    t.scale = 1.0f;
    t.strokeWidth = 1.0f;
    t.color = { 1, 1, 0, 1 };

    std::vector<LineEntity> lines;
    t.text = oss.str();
    if (t.text.empty())
        t.text = "HUD ONLINE";

    if (!t.font)
    {
        LogOnce("hud_no_font", "HUD: No font (t.font is NULL) -> skipping Hershey HUD.");
    }
    else if (!t.text.empty())
    {
        HersheyTextBuilder::BuildLines(t, lines);
    }

    // 4) Convert LineEntity -> Vertex list
    m_hudVertices.clear();
    m_hudVertices.reserve(lines.size() * 2);
    for (const auto& L : lines)
    {
        m_hudVertices.push_back({ L.p0, L.color });
        m_hudVertices.push_back({ L.p1, L.color });
    }

    // 5) Upload HUD lines to its own VBO
    glBindBuffer(GL_ARRAY_BUFFER, m_hudVbo);
    glBufferData(GL_ARRAY_BUFFER,
        (GLsizeiptr)(m_hudVertices.size() * sizeof(Vertex)),
        m_hudVertices.data(),
        GL_DYNAMIC_DRAW);

    // 6) Set HUD projection: pixel coordinates, origin bottom-left (matches builder)
    glm::mat4 hudMvp = glm::ortho(0.0f, (float)w, 0.0f, (float)h, -1.0f, +1.0f);
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
    m_vertices.clear();
    m_book->Clear();
}

void Application::BuildGrid(int clientW, int clientH)
{
    if (!m_book) return; // safety

    m_book->Clear();

    const float halfW = 0.5f * (float)clientW;
    const float halfH = 0.5f * (float)clientH;

    const float spacing = 50.0f; // adjust as desired

    auto addLine = [&](glm::vec3 a, glm::vec3 b, glm::vec4 color)
    {
        Entity e;
        e.ID = m_nextId++;
        e.type = EntityType::Line;
        e.tag = EntityTag::Scene;
        e.drawOrder = 0;
        e.screenSpace = false;

        e.line.p0 = a;
        e.line.p1 = b;
        e.line.color = color;
        e.line.width = 1.0f;

        m_book->AddEntity(e);
    };

    // Vertical grid lines
    for (float x = -halfW; x <= halfW + 0.01f; x += spacing)
    {
        bool major = (std::fmod(std::abs(x), spacing * 5.0f) < 0.01f);
        glm::vec4 c = major ? glm::vec4(0.30f, 0.32f, 0.36f, 1.0f)
                            : glm::vec4(0.20f, 0.22f, 0.26f, 1.0f);

        addLine({ x, -halfH, 0.0f }, { x, halfH, 0.0f }, c);
    }

    // Horizontal grid lines
    for (float y = -halfH; y <= halfH + 0.01f; y += spacing)
    {
        bool major = (std::fmod(std::abs(y), spacing * 5.0f) < 0.01f);
        glm::vec4 c = major ? glm::vec4(0.30f, 0.32f, 0.36f, 1.0f)
                            : glm::vec4(0.20f, 0.22f, 0.26f, 1.0f);

        addLine({ -halfW, y, 0.0f }, { halfW, y, 0.0f }, c);
    }

    // Axis lines (more visible)
    addLine({ -halfW, 0.0f, 0.0f }, { halfW, 0.0f, 0.0f }, { 0.70f, 0.20f, 0.20f, 1.0f });
    addLine({ 0.0f, -halfH, 0.0f }, { 0.0f, halfH, 0.0f }, { 0.20f, 0.70f, 0.20f, 1.0f });


    // ---------------------------
// Dragon (User geometry)
// ---------------------------
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
                e.tag = EntityTag::User;     // IMPORTANT: erasable geometry
                e.drawOrder = 1;             // above grid
                e.screenSpace = false;
                e.line.p0 = a;
                e.line.p1 = b;
                e.line.color = color;
                e.line.width = 1.0f;
                m_book->AddEntity(e);
            };

        glm::vec4 c(0.85f, 0.85f, 0.95f, 1.0f);

        for (const auto& s : segs)
            addUserLine(s.a, s.b, c);
    }

    m_book->SortByDrawOrder();
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

        glm::vec4 c = e.line.color;

        if (!e.screenSpace && e.tag == EntityTag::User)
        {
            const bool hovered = (m_state.hoveredIds.find(e.ID) != m_state.hoveredIds.end());
            const bool gripsSel = (m_state.tool == InteractionState::Tool::SelectGrips) && (m_state.gripsIds.find(e.ID) != m_state.gripsIds.end());
            if (e.selected || hovered || gripsSel)
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

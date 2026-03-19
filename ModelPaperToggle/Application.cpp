#include "Application.h"

#include <iostream>
#include <filesystem>
#include <sstream>
#include <cstdlib>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <cfloat>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace
{
    std::filesystem::path GetExeDir()
    {
        wchar_t buf[MAX_PATH]{};
        const DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
        if (n == 0 || n >= (DWORD)std::size(buf))
            return std::filesystem::current_path();
        return std::filesystem::path(buf).parent_path();
    }

    void FatalMessageBoxAndExit(const std::string& msg)
    {
        MessageBoxA(nullptr, msg.c_str(), "ModelPaperToggle - Fatal Error", MB_OK | MB_ICONERROR);
        std::cerr << "FATAL: " << msg << "\n";
        ExitProcess(1);
    }
}

// ------------------------------------------------------------
// GL helpers
// ------------------------------------------------------------

Application::Application() = default;
Application::~Application() = default;

void Application::LogSink(EntityCore::LogLevel level,
                          const char* category,
                          const char* event,
                          std::string_view message)
{
    const char* lvl = "INFO";
    if (level == EntityCore::LogLevel::Warn)  lvl = "WARN";
    if (level == EntityCore::LogLevel::Error) lvl = "ERROR";

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

Application::SpaceContext& Application::Active()
{
    return (m_space == Space::Model) ? m_model : m_paper;
}

const Application::SpaceContext& Application::Active() const
{
    return (m_space == Space::Model) ? m_model : m_paper;
}

static void AddLine(EntityBook& book, std::size_t& nextId,
                    const glm::vec3& a, const glm::vec3& b,
                    const glm::vec4& color,
                    EntityTag tag, int drawOrder, bool pickable)
{
    Entity e;
    e.ID = nextId++;
    e.type = EntityType::Line;
    e.tag = tag;
    e.drawOrder = drawOrder;
    e.screenSpace = false;
    e.pickable = pickable;
    e.visible = true;
    e.line.p0 = a;
    e.line.p1 = b;
    e.line.color = color;
    e.line.width = 1.0f;
    book.AddEntity(e);
}

void Application::BuildModelScene(SpaceContext& ctx)
{
    if (!ctx.book)
        return;

    ctx.book->Clear();
    ctx.state.ClearHover();
    ctx.state.gripsIds.clear();

    // User geometry: dragon curve
    {
        DragonCurve dragon;
        const int iterations = 12;
        const glm::vec3 origin = { -200.0f, 0.0f, 0.0f };
        const auto segs = dragon.Build(iterations, origin);

        glm::vec4 c(0.85f, 0.85f, 0.95f, 1.0f);
        for (const auto& s : segs)
            AddLine(*ctx.book, m_nextIdModel, s.a, s.b, c, EntityTag::User, 1, true);

        ctx.book->SortByDrawOrder();
    }

    // Seed initial grid
    ctx.lastClientW = 800;
    ctx.lastClientH = 600;
    ctx.pz.SetViewport(ctx.lastClientW, ctx.lastClientH);
    RenderCore::UpdateBackgroundGrid(*ctx.book, ctx.pz, ctx.lastClientW, ctx.lastClientH,
                                     m_nextIdModel, ctx.gridCache);

    ctx.state.sceneDirty = true;
}

void Application::BuildPaperScene(SpaceContext& ctx)
{
    if (!ctx.book)
        return;

    ctx.book->Clear();
    ctx.state.ClearHover();
    ctx.state.gripsIds.clear();

    // "Paper" sheet in paper units (arbitrary).
    // Baby step: no model viewport yet, just distinct scene content so we can toggle.
    const float w = 420.0f;  // A3-ish width
    const float h = 297.0f;  // A3-ish height

    glm::vec4 border(0.85f, 0.85f, 0.85f, 1.0f);
    glm::vec4 vpRect(0.65f, 0.85f, 0.65f, 1.0f);

    // Sheet border
    AddLine(*ctx.book, m_nextIdPaper, {0, 0, 0}, {w, 0, 0}, border, EntityTag::User, 1, false);
    AddLine(*ctx.book, m_nextIdPaper, {w, 0, 0}, {w, h, 0}, border, EntityTag::User, 1, false);
    AddLine(*ctx.book, m_nextIdPaper, {w, h, 0}, {0, h, 0}, border, EntityTag::User, 1, false);
    AddLine(*ctx.book, m_nextIdPaper, {0, h, 0}, {0, 0, 0}, border, EntityTag::User, 1, false);

    // Origin marker (paper space): show where (x==0, y==0) is.
    // Draw a small cross with an "L" cue at the corner.
    {
        const float s = 18.0f;     // marker size in paper units
        const float t = 6.0f;      // tick length
        const glm::vec4 originC(0.95f, 0.35f, 0.35f, 1.0f); // red-ish

        // Axes cue along +X and +Y from the origin
        AddLine(*ctx.book, m_nextIdPaper, {0, 0, 0}, {s, 0, 0}, originC, EntityTag::User, 2, false);
        AddLine(*ctx.book, m_nextIdPaper, {0, 0, 0}, {0, s, 0}, originC, EntityTag::User, 2, false);

        // Small cross centered at the origin for visibility
        AddLine(*ctx.book, m_nextIdPaper, {-t, 0, 0}, {+t, 0, 0}, originC, EntityTag::User, 2, false);
        AddLine(*ctx.book, m_nextIdPaper, {0, -t, 0}, {0, +t, 0}, originC, EntityTag::User, 2, false);
    }

    // Title block (simple)
    const float tbH = 30.0f;
    AddLine(*ctx.book, m_nextIdPaper, {0, tbH, 0}, {w, tbH, 0}, border, EntityTag::User, 1, false);
    AddLine(*ctx.book, m_nextIdPaper, {w - 120.0f, 0, 0}, {w - 120.0f, tbH, 0}, border, EntityTag::User, 1, false);

    // Placeholder "viewport" rectangle where model will eventually be drawn
    const float vpx0 = 20.0f;
    const float vpy0 = tbH + 20.0f;
    const float vpx1 = w - 20.0f;
    const float vpy1 = h - 20.0f;

    AddLine(*ctx.book, m_nextIdPaper, {vpx0, vpy0, 0}, {vpx1, vpy0, 0}, vpRect, EntityTag::User, 1, false);
    AddLine(*ctx.book, m_nextIdPaper, {vpx1, vpy0, 0}, {vpx1, vpy1, 0}, vpRect, EntityTag::User, 1, false);
    AddLine(*ctx.book, m_nextIdPaper, {vpx1, vpy1, 0}, {vpx0, vpy1, 0}, vpRect, EntityTag::User, 1, false);
    AddLine(*ctx.book, m_nextIdPaper, {vpx0, vpy1, 0}, {vpx0, vpy0, 0}, vpRect, EntityTag::User, 1, false);

    ctx.book->SortByDrawOrder();

    ctx.lastClientW = 800;
    ctx.lastClientH = 600;
    ctx.pz.SetViewport(ctx.lastClientW, ctx.lastClientH);

    // Frame the paper nicely (PanZoomController has no SetWorldRect in the modern libs).
    // With OrthoMode::Center, putting pan at the paper center makes the sheet sit in view.
    ctx.pz.pan = { w * 0.5f, h * 0.5f };
    ctx.pz.zoom = 1.0f;

    ctx.state.sceneDirty = true;
}

void Application::Initialize()
{
    EntityCore::SetLogSink(&Application::LogSink);
    std::cout << "ModelPaperToggle::Initialize\n";

    // Create books via factory (matches other up-to-date samples)
    auto createdModel = CreateEntityBook(8192);
    auto createdPaper = CreateEntityBook(4096);
    if (!createdModel || !createdPaper)
    {
        std::cout << "FATAL: EntityBook creation failed\n";
        return;
    }

    m_model.book = std::move(*createdModel);
    m_paper.book = std::move(*createdPaper);

    // AppCoreLib state references RenderCoreLib selection window but does not own it.
    m_model.state.selectionWindow = &m_model.selWindow;
    m_paper.state.selectionWindow = &m_paper.selWindow;

    // Minimal GL 3.3 shader for colored lines
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

    // Crosshair defaults
    m_crosshairs.SetPickBoxSizePx(12);
    m_crosshairs.SetMode(RenderCore::CrosshairsMode::GripsSelection);
    m_crosshairs.SetBackground(RenderCore::CrosshairsBackground::Dark);

    // -----------------------------------------------------
    // Font init
    // -----------------------------------------------------
    // IMPORTANT:
    // The HUD (including coordinate tracker) depends on a Hershey font.
    // We *fail loudly* if no font is available so we never silently ship an invisible HUD.
    EnsureHudFontOrDie();

    // Build both scenes
    BuildModelScene(m_model);
    BuildPaperScene(m_paper);

    // Start in model space
    m_space = Space::Model;
    UploadLinesToGpu(Active());
    Active().state.sceneDirty = false;
}

void Application::EnsureHudFontOrDie()
{
    if (m_hudFont)
        return;

    // Collect diagnostics and candidate locations.
    std::ostringstream diag;
    diag << "HUD font 'rowmans' is required, but could not be loaded.\n\n";

    // 1) Try the known-good load path used by other samples: load by name.
    // Most Hershey loaders use HERSHEY_FONTS_DIR internally when loading by name.
    m_hudFont = hershey_font_load("rowmans");
    if (m_hudFont)
        return;

    // 2) Build explicit file candidates (in case the loader supports file-path loading
    // or to produce actionable diagnostics).
    char* hershey_dir = nullptr;
    size_t hershey_len = 0;
    _dupenv_s(&hershey_dir, &hershey_len, "HERSHEY_FONTS_DIR");
    const std::string envDir = (hershey_dir && hershey_len > 0) ? std::string(hershey_dir) : std::string();
    if (hershey_dir)
        free(hershey_dir);

    const std::filesystem::path exeDir = GetExeDir();
    const std::filesystem::path fallbackDir = std::filesystem::path("C:\\ProgramData\\hershey-fonts");

    const std::filesystem::path candidates[] = {
        envDir.empty() ? std::filesystem::path() : (std::filesystem::path(envDir) / "rowmans.jhf"),
        fallbackDir / "rowmans.jhf",
        exeDir / "rowmans.jhf",
        exeDir / "fonts" / "rowmans.jhf",
    };

    diag << "Tried hershey_font_load(\"rowmans\") and it returned NULL.\n";
    diag << "Checked these file locations:\n";
    for (const auto& p : candidates)
    {
        if (p.empty())
            continue;
        diag << "  - " << p.string() << "  [" << (std::filesystem::exists(p) ? "exists" : "missing") << "]\n";
    }

    // 3) If the implementation happens to accept file paths, try those that exist.
    // (If it doesn't, these will just return NULL; we still want a single, loud failure.)
    for (const auto& p : candidates)
    {
        if (p.empty() || !std::filesystem::exists(p))
            continue;
        m_hudFont = hershey_font_load(p.string().c_str());
        if (m_hudFont)
            return;
    }

    diag << "\nHow to fix:\n";
    diag << "  1) Install Hershey fonts and set HERSHEY_FONTS_DIR to the folder containing rowmans.jhf, or\n";
    diag << "  2) Copy rowmans.jhf to C:\\\\ProgramData\\\\hershey-fonts\\\\rowmans.jhf, or\n";
    diag << "  3) Place rowmans.jhf next to the executable (or in .\\fonts\\rowmans.jhf).\n\n";
    diag << "Without this font, HUD text cannot be drawn, so the application will exit.\n";

    FatalMessageBoxAndExit(diag.str());
}

void Application::Shutdown()
{
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
    if (m_cursorVbo)
    {
        glDeleteBuffers(1, &m_cursorVbo);
        m_cursorVbo = 0;
    }
}

bool Application::RebuildHoverSet(SpaceContext& ctx)
{
    if (!ctx.book)
        return false;

    if (ctx.lastClientW <= 0 || ctx.lastClientH <= 0)
        return false;

    // Build a small pick AABB based on the crosshair pick box size.
    const int halfPx = std::max(1, m_crosshairs.GetPickBoxSizePx() / 2);
    const RenderCore::PickBox pb = RenderCore::MakePickBox(
        ctx.pz,
        ctx.state.mouseX, ctx.state.mouseY,
        halfPx,
        ctx.lastClientW, ctx.lastClientH);

    // AppCore owns hover logic for the entity model.
    return AppCore::RebuildHoverSet(*ctx.book, ctx.state, pb.worldMin, pb.worldMax);
}

void Application::UploadLinesToGpu(SpaceContext& ctx)
{
    if (!ctx.book)
        return;

    ctx.vertices.clear();
    ctx.vertices.reserve(ctx.book->entities.size() * 2);

    const glm::vec4 kHighlight(1.0f, 1.0f, 0.0f, 1.0f); // yellow

    for (const auto& e : ctx.book->entities)
    {
        if (e.type != EntityType::Line)
            continue;

        const glm::vec4 c = RenderCore::EffectiveLineColor(e, &ctx.state.hoveredIds, kHighlight);

        Vertex v0{ e.line.p0, c };
        Vertex v1{ e.line.p1, c };
        ctx.vertices.push_back(v0);
        ctx.vertices.push_back(v1);
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(ctx.vertices.size() * sizeof(Vertex)),
                 ctx.vertices.data(),
                 GL_DYNAMIC_DRAW);
}

void Application::Update(float /*inDeltaTime*/)
{
    SpaceContext& ctx = Active();

    // Hover highlighting follows the cursor.
    if (RebuildHoverSet(ctx))
        MarkDirty(ctx);

    // Keep the infinite grid aligned to the model view only
    if ((&ctx == &m_model) && ctx.book && ctx.lastClientW > 0 && ctx.lastClientH > 0)
    {
        if (RenderCore::UpdateBackgroundGrid(*ctx.book, ctx.pz, ctx.lastClientW, ctx.lastClientH,
                                             m_nextIdModel, ctx.gridCache))
            MarkDirty(ctx);
    }

    if (ctx.state.sceneDirty)
    {
        UploadLinesToGpu(ctx);
        ctx.state.sceneDirty = false;
    }
}

void Application::EnsureLinesOnGpu(SpaceContext& ctx)
{
    // We use a single shared VBO for line vertices.
    // If the last upload was for a different space, we must re-upload even if sceneDirty is false.
    if (m_gpuLinesOwner != &ctx || ctx.state.sceneDirty)
    {
        UploadLinesToGpu(ctx);
        ctx.state.sceneDirty = false;
        m_gpuLinesOwner = &ctx;
    }
}

void Application::BindLineVertexLayout()
{
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));
}

void Application::DrawScenePass(const SpaceContext& ctx, const glm::mat4& mvp)
{
    glUseProgram(m_program);
    glUniformMatrix4fv(m_uMvp, 1, GL_FALSE, &mvp[0][0]);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    BindLineVertexLayout();
    glDrawArrays(GL_LINES, 0, (GLsizei)ctx.vertices.size());
}

// Convert a paper-space world rect into a client pixel rect.
// Returns false if the rect is too small or entirely off-screen.
static bool PaperRectToPixelRect(
    const PanZoomController& paperPz,
    int screenW, int screenH,
    const glm::vec2& paperMin,
    const glm::vec2& paperMax,
    int& outX, int& outY, int& outW, int& outH)
{
    const glm::mat4 mvp = paperPz.GetMVP();

    auto toNdc = [&](float x, float y) -> glm::vec2
    {
        glm::vec4 c = mvp * glm::vec4(x, y, 0.0f, 1.0f);
        if (c.w != 0.0f) { c.x /= c.w; c.y /= c.w; }
        return { c.x, c.y };
    };

    glm::vec2 n0 = toNdc(paperMin.x, paperMin.y);
    glm::vec2 n1 = toNdc(paperMax.x, paperMin.y);
    glm::vec2 n2 = toNdc(paperMin.x, paperMax.y);
    glm::vec2 n3 = toNdc(paperMax.x, paperMax.y);

    float ndcMinX = std::min(std::min(n0.x, n1.x), std::min(n2.x, n3.x));
    float ndcMaxX = std::max(std::max(n0.x, n1.x), std::max(n2.x, n3.x));
    float ndcMinY = std::min(std::min(n0.y, n1.y), std::min(n2.y, n3.y));
    float ndcMaxY = std::max(std::max(n0.y, n1.y), std::max(n2.y, n3.y));

    auto ndcToPx = [&](float nx, float ny) -> glm::vec2
    {
        float px = (nx * 0.5f + 0.5f) * (float)screenW;
        float py = (ny * 0.5f + 0.5f) * (float)screenH;
        return { px, py };
    };

    glm::vec2 pMin = ndcToPx(ndcMinX, ndcMinY);
    glm::vec2 pMax = ndcToPx(ndcMaxX, ndcMaxY);

    int x0 = (int)std::floor(std::min(pMin.x, pMax.x));
    int x1 = (int)std::ceil(std::max(pMin.x, pMax.x));
    int y0 = (int)std::floor(std::min(pMin.y, pMax.y));
    int y1 = (int)std::ceil(std::max(pMin.y, pMax.y));

    x0 = std::max(0, std::min(x0, screenW));
    x1 = std::max(0, std::min(x1, screenW));
    y0 = std::max(0, std::min(y0, screenH));
    y1 = std::max(0, std::min(y1, screenH));

    outX = x0;
    outY = y0;
    outW = x1 - x0;
    outH = y1 - y0;

    return (outW > 2 && outH > 2);
}

static bool ComputeUserBounds(const EntityBook& book, glm::vec2& outMin, glm::vec2& outMax)
{
    bool any = false;
    glm::vec2 mn( FLT_MAX,  FLT_MAX);
    glm::vec2 mx(-FLT_MAX, -FLT_MAX);

    auto grow = [&](const glm::vec3& p)
    {
        mn.x = std::min(mn.x, p.x);
        mn.y = std::min(mn.y, p.y);
        mx.x = std::max(mx.x, p.x);
        mx.y = std::max(mx.y, p.y);
        any = true;
    };

    for (const Entity& e : book.entities)
    {
        if (!e.visible) continue;
        if (e.tag != EntityTag::User) continue;

        if (e.type == EntityType::Line)
        {
            grow(e.line.p0);
            grow(e.line.p1);
        }
    }

    if (!any) return false;
    outMin = mn;
    outMax = mx;
    return true;
}

static void ZoomExtents(PanZoomController& pz, int vw, int vh, const glm::vec2& mn, const glm::vec2& mx)
{
    const float dx = std::max(mx.x - mn.x, 1e-3f);
    const float dy = std::max(mx.y - mn.y, 1e-3f);

    const float margin = 0.90f; // 10% padding
    const float zx = (vw * margin) / dx;
    const float zy = (vh * margin) / dy;

    pz.zoom = std::max(1e-6f, std::min(zx, zy));
    pz.pan  = (mn + mx) * 0.5f;
}


void Application::SyncViewportAndRebuildSceneIfNeeded(SpaceContext& ctx, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;

    if (w == ctx.lastClientW && h == ctx.lastClientH)
        return;

    ctx.lastClientW = w;
    ctx.lastClientH = h;

    if (ctx.book)
    {
        ctx.pz.SetViewport(w, h);

        if (&ctx == &m_model)
        {
            if (RenderCore::UpdateBackgroundGrid(*ctx.book, ctx.pz, w, h, m_nextIdModel, ctx.gridCache))
                ctx.state.sceneDirty = true;
        }
    }
}

void Application::DrawOverlayPass(SpaceContext& ctx, int w, int h)
{
    // Pixel-space MVP: origin bottom-left, X right, Y up
    const glm::mat4 overlayMvp = glm::ortho(0.0f, (float)w, 0.0f, (float)h, -1.0f, +1.0f);

    std::vector<LineEntity> overlayLines;
    m_crosshairs.BuildLines(overlayLines);

    // If a W/C selection is active (ERASE), draw its outline too
    if (ctx.selWindow.IsActive())
    {
        const glm::vec4 rc = (ctx.state.rectMode == InteractionState::RectMode::Crossing)
            ? glm::vec4(0.95f, 0.35f, 0.35f, 1.0f)
            : glm::vec4(0.35f, 0.95f, 0.35f, 1.0f);

        ctx.selWindow.BuildOverlayOutline(w, h, rc, /*widthPx*/ 1.5f, overlayLines);
    }

    // Convert overlay lines to vertices
    m_cursorVertices.clear();
    m_cursorVertices.reserve(overlayLines.size() * 2);

    for (const auto& l : overlayLines)
    {
        Vertex v0{ l.p0, l.color };
        Vertex v1{ l.p1, l.color };
        m_cursorVertices.push_back(v0);
        m_cursorVertices.push_back(v1);
    }

    glUseProgram(m_program);
    glUniformMatrix4fv(m_uMvp, 1, GL_FALSE, &overlayMvp[0][0]);

    glBindBuffer(GL_ARRAY_BUFFER, m_cursorVbo);
    glBufferData(GL_ARRAY_BUFFER,
        (GLsizeiptr)(m_cursorVertices.size() * sizeof(Vertex)),
        m_cursorVertices.data(),
        GL_DYNAMIC_DRAW);

    BindLineVertexLayout();
    glDrawArrays(GL_LINES, 0, (GLsizei)m_cursorVertices.size());
}

void Application::DrawHudPass(SpaceContext& ctx, int w, int h, const glm::mat4& /*sceneMvp*/)
{
    // We should never get here without a font, because Initialize() calls EnsureHudFontOrDie().
    // Keep a defensive loud check anyway to avoid invisible HUD regressions.
    if (!m_hudFont)
        EnsureHudFontOrDie();

    const char* spaceName = (m_space == Space::Model) ? "MODEL" : "PAPER";

    std::ostringstream oss;
    oss << "SPACE: " << spaceName << "   (F2 toggles)";
    oss << "\nHover: " << ctx.state.hoveredIds.size();
    oss << "   Selected: " << ctx.state.SelectedCount(ctx.book.get());

    // Pixel-space MVP: origin bottom-left, X right, Y up
    const glm::mat4 overlayMvp = glm::ortho(0.0f, (float)w, 0.0f, (float)h, -1.0f, +1.0f);

    std::vector<LineEntity> hudLines;

    // --- Top-left HUD (space + hover/selection counts) ---
    {
        TextEntity t;
        RenderCore::InitTextEntityOrDie(
            t,
            oss.str(),
            m_hudFont,
            RenderCore::TextInitPolicy::AssertAndMessageBox,
            "ModelPaperToggle HUD"
        );

        t.wordWrapEnabled = true;
        t.hAlign = TextHAlign::Left;
        t.boxWidth = std::max(260.0f, (float)w * 0.60f);
        t.boxHeight = 80.0f;
        t.position = { 12.0f, (float)h - 12.0f - t.boxHeight, 0.0f }; // Y-up pixel coords
        t.scale = 0.35f;
        t.strokeWidth = 1.0f;
        t.color = { 1, 1, 0, 1 };
        t.backgroundEnabled = true;
        t.backgroundColor = { 0, 0, 0, 0.75f };
        t.backgroundPadding = 10.0f;
        t.backgroundOutlineEnabled = true;
        t.backgroundOutlineColor = { 1, 1, 1, 0.35f };
        t.backgroundOutlineThickness = 1.0f;

        HersheyTextBuilder::BuildLines(t, hudLines);
    }

    // --- Paper-space coordinate tracker (bottom-right; paper space only) ---
    if (m_space == Space::Paper)
    {
        // Convert Win32 client coords (origin top-left, Y down) -> paper-space (world) coords.
        // We do this via inverse MVP so we don't need any extra PanZoomController API.
        const float fx = (w > 0) ? (float)ctx.state.mouseX / (float)w : 0.0f;
        const float fy = (h > 0) ? (float)ctx.state.mouseY / (float)h : 0.0f;

        // NDC: X [-1..+1] left->right, Y [-1..+1] bottom->top.
        const float ndcX = fx * 2.0f - 1.0f;
        const float ndcY = 1.0f - fy * 2.0f;

        const glm::mat4 invMvp = glm::inverse(ctx.pz.GetMVP());
        const glm::vec4 world = invMvp * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
        const glm::vec2 paperXY = (world.w != 0.0f)
            ? glm::vec2(world.x / world.w, world.y / world.w)
            : glm::vec2(world.x, world.y);

        std::ostringstream oss2;
        oss2.setf(std::ios::fixed);
        oss2.precision(2);
        oss2 << "PAPER XY: " << paperXY.x << ", " << paperXY.y;

        TextEntity t;
        RenderCore::InitTextEntityOrDie(
            t,
            oss2.str(),
            m_hudFont,
            RenderCore::TextInitPolicy::AssertAndMessageBox,
            "PaperSpace XY HUD"
        );

        t.wordWrapEnabled = false;
        t.hAlign = TextHAlign::Left; // we place the box explicitly
        t.boxWidth = 320.0f;
        t.boxHeight = 34.0f;
        t.position = { (float)w - 12.0f - t.boxWidth, 12.0f, 0.0f }; // bottom-right
        t.scale = 0.35f;
        t.strokeWidth = 1.0f;
        t.color = { 0.65f, 0.95f, 0.65f, 1.0f };
        t.backgroundEnabled = true;
        t.backgroundColor = { 0, 0, 0, 0.75f };
        t.backgroundPadding = 10.0f;
        t.backgroundOutlineEnabled = true;
        t.backgroundOutlineColor = { 1, 1, 1, 0.35f };
        t.backgroundOutlineThickness = 1.0f;

        HersheyTextBuilder::BuildLines(t, hudLines);
    }

    m_hudVertices.clear();
    m_hudVertices.reserve(hudLines.size() * 2);
    for (const auto& l : hudLines)
    {
        m_hudVertices.push_back(Vertex{ l.p0, l.color });
        m_hudVertices.push_back(Vertex{ l.p1, l.color });
    }

    glUseProgram(m_program);
    glUniformMatrix4fv(m_uMvp, 1, GL_FALSE, &overlayMvp[0][0]);

    glBindBuffer(GL_ARRAY_BUFFER, m_hudVbo);
    glBufferData(GL_ARRAY_BUFFER,
        (GLsizeiptr)(m_hudVertices.size() * sizeof(Vertex)),
        m_hudVertices.data(),
        GL_DYNAMIC_DRAW);

    BindLineVertexLayout();
    glDrawArrays(GL_LINES, 0, (GLsizei)m_hudVertices.size());
}

void Application::Render(float /*inAspectRatio*/)
{
    GLint vp[4]{};
    glGetIntegerv(GL_VIEWPORT, vp);
    const int w = vp[2];
    const int h = vp[3];

    // Keep both spaces sized correctly (paper rect -> pixel conversion relies on this)
    SyncViewportAndRebuildSceneIfNeeded(m_model, w, h);
    SyncViewportAndRebuildSceneIfNeeded(m_paper, w, h);

    m_model.pz.SetViewport(w, h);
    m_paper.pz.SetViewport(w, h);
    m_crosshairs.SetViewport(w, h);

    if (m_space == Space::Model)
    {
        // Normal: model full-screen
        EnsureLinesOnGpu(m_model);
        const glm::mat4 modelMvp = m_model.pz.GetMVP();
        DrawScenePass(m_model, modelMvp);
        DrawOverlayPass(m_model, w, h);
        DrawHudPass(m_model, w, h, modelMvp);
        return;
    }

    // Paper full-screen
    EnsureLinesOnGpu(m_paper);
    const glm::mat4 paperMvp = m_paper.pz.GetMVP();
    DrawScenePass(m_paper, paperMvp);

    // Model drawn INSIDE the paper viewport rectangle
    if (m_hasPaperViewport)
    {
        int vx, vy, vw, vh;
        if (PaperRectToPixelRect(m_paper.pz, w, h, m_paperVpMin, m_paperVpMax, vx, vy, vw, vh))
        {
            glEnable(GL_SCISSOR_TEST);
            glScissor(vx, vy, vw, vh);

            // Optional: clear just the viewport area so it reads as a "window"
            glClear(GL_COLOR_BUFFER_BIT);

            glViewport(vx, vy, vw, vh);

            // Make the model MVP match the sub-viewport size, otherwise everything will be tiny.
            m_model.pz.SetViewport(vw, vh);

            // One-time: when a viewport is created/changed, zoom model extents so the dragon is readable.
            if (m_pendingModelZoomToViewport && m_model.book)
            {
                glm::vec2 mn, mx;
                if (ComputeUserBounds(*m_model.book, mn, mx))
                    ZoomExtents(m_model.pz, vw, vh, mn, mx);
                m_pendingModelZoomToViewport = false;
            }

            EnsureLinesOnGpu(m_model);
            const glm::mat4 modelMvp = m_model.pz.GetMVP();
            DrawScenePass(m_model, modelMvp);

            // Restore model controller viewport back to full window (used in MODEL mode, HUD, etc.)
            m_model.pz.SetViewport(w, h);

            // restore full-screen viewport for overlays/HUD
            glViewport(0, 0, w, h);
            glDisable(GL_SCISSOR_TEST);
        }
    }

    DrawOverlayPass(m_paper, w, h);
    DrawHudPass(m_paper, w, h, paperMvp);
}

void Application::OnMouseMoveClient(int x, int y)
{
    SpaceContext& ctx = Active();
    ctx.state.mouseX = x;
    ctx.state.mouseY = y;

    // Crosshairs takes Win32 client coordinates (origin top-left, Y down).
    m_crosshairs.SetMouseClient(x, y);

    // Update rubber-band rect if active
    if (ctx.state.waitingForSecondClick && ctx.selWindow.IsActive())
        ctx.selWindow.UpdateClient(x, y);

    ctx.state.requestRedraw = true;
}

void Application::OnLeftButtonDownClient(int x, int y)
{
    SpaceContext& ctx = Active();
    if (!ctx.book)
        return;

    // Paper viewport creation: V enters a two-click window rect mode.
    if (m_space == Space::Paper &&
        ctx.state.waitingForSecondClick &&
        ctx.state.rectMode == InteractionState::RectMode::Window)
    {
        // First click: begin
        if (!ctx.selWindow.IsActive())
        {
            ctx.selWindow.BeginClient(x, y);
            ctx.selWindow.UpdateClient(x, y);
            ctx.state.requestRedraw = true;
            return;
        }

        // Second click: finalize
        ctx.selWindow.UpdateClient(x, y);
        ctx.selWindow.End();

        int x0, y0, x1, y1;
        ctx.selWindow.GetClientRect(x0, y0, x1, y1);

        // Convert client pixels -> paper world
        glm::vec2 w00 = ctx.pz.ScreenToWorld(x0, y0);
        glm::vec2 w11 = ctx.pz.ScreenToWorld(x1, y1);

        glm::vec2 mn(std::min(w00.x, w11.x), std::min(w00.y, w11.y));
        glm::vec2 mx(std::max(w00.x, w11.x), std::max(w00.y, w11.y));

        m_hasPaperViewport = true;
        m_paperVpMin = mn;
        m_paperVpMax = mx;

        m_pendingModelZoomToViewport = true;

        // Leave rect mode
        ctx.state.waitingForSecondClick = false;
        ctx.state.rectMode = InteractionState::RectMode::None;
        ctx.selWindow.Reset();
        m_crosshairs.SetMode(RenderCore::CrosshairsMode::GripsSelection);

        ctx.state.requestRedraw = true;
        std::cout << "Viewport set in PAPER: [(" << mn.x << "," << mn.y << ") - (" << mx.x << "," << mx.y << ")]\n";
        return;
    }

    // Default: click toggles persistent selection of hovered entities.
    for (const auto id : ctx.state.hoveredIds)
        AppCore::ToggleSelection(*ctx.book, ctx.state, id);

    ctx.state.sceneDirty = true;
    ctx.state.requestRedraw = true;
}

void Application::OnKeyDown(WPARAM key, bool /*ctrlDown*/)
{
    if (key == VK_F2)
    {
        m_space = (m_space == Space::Model) ? Space::Paper : Space::Model;

        // Ensure overlays (cursor->px conversion) have a sane height immediately.
        SpaceContext& ctx = Active();
        if (ctx.lastClientW == 0 || ctx.lastClientH == 0)
        {
            GLint vp[4]{};
            glGetIntegerv(GL_VIEWPORT, vp);
            ctx.lastClientW = vp[2];
            ctx.lastClientH = vp[3];
        }

        // Force a re-upload for the newly active space
        ctx.state.sceneDirty = true;
        ctx.state.requestRedraw = true;
        std::cout << "Toggled space -> " << ((m_space == Space::Model) ? "MODEL" : "PAPER") << "\n";
        return;
    }

    // Quick zoom reset
    if (key == 'R')
    {
        SpaceContext& ctx = Active();
        ctx.pz.pan = { 0.0f, 0.0f };
        ctx.pz.zoom = 1.0f;
        ctx.state.sceneDirty = true;
        ctx.state.requestRedraw = true;
    }

    // V: define paper viewport rectangle (two-click window)
    if (key == 'V')
    {
        if (m_space != Space::Paper)
            return;

        SpaceContext& ctx = Active();

        // Cancel if already in the middle of a rect pick
        if (ctx.state.waitingForSecondClick)
        {
            ctx.state.waitingForSecondClick = false;
            ctx.state.rectMode = InteractionState::RectMode::None;
            ctx.selWindow.Reset();
            m_crosshairs.SetMode(RenderCore::CrosshairsMode::GripsSelection);
            ctx.state.requestRedraw = true;
            std::cout << "Viewport pick cancelled\n";
            return;
        }

        ctx.state.rectMode = InteractionState::RectMode::Window;
        ctx.state.waitingForSecondClick = true;
        ctx.selWindow.Reset();
        m_crosshairs.SetMode(RenderCore::CrosshairsMode::PointPick);
        ctx.state.requestRedraw = true;
        std::cout << "Viewport: click first corner, then opposite corner (press V again to cancel)\n";
        return;
    }

    // Keep the ortho test keys available (apply to the active space)
    switch (key)
    {
    case '1': Active().pz.mode = PanZoomController::OrthoMode::Center;     Active().state.requestRedraw = true; break;
    case '2': Active().pz.mode = PanZoomController::OrthoMode::BottomLeft; Active().state.requestRedraw = true; break;
    case '3': Active().pz.mode = PanZoomController::OrthoMode::TopLeft;    Active().state.requestRedraw = true; break;
    default: break;
    }
}

void Application::OnRightButtonDownClient(int x, int y)
{
    SpaceContext& ctx = Active();
    ctx.pz.OnMouseDown(x, y);
    ctx.state.requestRedraw = true;
}

void Application::OnRightButtonUpClient()
{
    SpaceContext& ctx = Active();
    ctx.pz.OnMouseUp();
    ctx.state.requestRedraw = true;
}

void Application::OnRightButtonDragMoveClient(int x, int y)
{
    SpaceContext& ctx = Active();
    ctx.pz.OnMouseMove(x, y);
    ctx.state.requestRedraw = true;
}

void Application::OnMouseWheelClient(int x, int y, int wheelDelta)
{
    SpaceContext& ctx = Active();
    ctx.pz.OnMouseWheel(x, y, wheelDelta);
    ctx.state.requestRedraw = true;
}

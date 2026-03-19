// Application.cpp
#include "Application.h"
#include "Logger.h"
#include "DragonCurve.h"
#include "SceneJsonIO.h"
#include "CmdWindow.h"
#include "RenderStyle.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdarg>
#include <sstream>
#include <string>
#include <vector>
#include <random>
#include <unordered_set>
#include <unordered_map>
#include <limits>

#ifdef _WIN32
#include <commdlg.h>
#endif

#include <glm/gtc/matrix_transform.hpp>

#include "hersheyfont.h"

// Forward declarations for LINE helpers.
// These are defined later in this translation unit, but are used by event handlers above.
static glm::vec3 OrthoProjectFromCursor(const glm::vec3& origin, const glm::vec3& cursor, float distanceOverride = -1.0f);
static glm::vec3 ApplyOneShotSnap(Application::SnapMode snap, const glm::vec3& p);


static std::string TrimCopy(const std::string& s)
{
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static std::string ToUpperCopyA(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return (char)std::toupper(c); });
    return s;
}

// NOTE: Your project uses the C-style hershey font handle.
static hershey_font* g_hersheyFont = nullptr;


// Demo model-space content (used for viewport rendering and grip-hover in paper-space).
// For now we reuse the dragon curve segments as our "model space" entities.
static const std::vector<std::pair<glm::vec2, glm::vec2>>& GetDemoModelSegments2D()
{
    static std::vector<std::pair<glm::vec2, glm::vec2>> segs2d;
    static bool built = false;
    if (!built)
    {
        built = true;
        DragonCurve curve;
        const int dragonIterations = 12; // 4096 segments
        const glm::vec3 origin(0.0f, 0.0f, 0.0f);
        const auto segs = curve.Build(dragonIterations, origin);
        segs2d.reserve(segs.size());
        for (const auto& s : segs)
            segs2d.push_back({ glm::vec2(s.a.x, s.a.y), glm::vec2(s.b.x, s.b.y) });
    }
    return segs2d;
}

// ------------------------------------------------------------
// Small helpers
// ------------------------------------------------------------
static Entity* FindEntityById(EntityBook& book, std::size_t id)
{
    auto& ents = book.GetEntitiesMutable();
    for (auto& e : ents)
        if (e.ID == id)
            return &e;
    return nullptr;
}

static Entity MakeLine(uint32_t id,
    EntityTag tag,
    int drawOrder,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec4& color,
    float thickness,
    bool screenSpace)
{
    Entity e;
    e.ID = id;
    e.tag = tag;
    e.type = EntityType::Line;
    e.drawOrder = drawOrder;
    e.screenSpace = screenSpace;

    e.line.start = a;
    e.line.end = b;
    e.line.color = color;
    e.line.thickness = thickness;
    return e;
}

static Entity MakeText(uint32_t id,
    EntityTag tag,
    int drawOrder,
    const std::string& text,
    const glm::vec3& pos,
    float boxW,
    float boxH,
    bool wrap,
    TextHAlign align,
    float scale,
    const glm::vec4& color,
    float strokeWidth,
    bool screenSpace,
    bool backgroundEnabled = false,
    const glm::vec4& backgroundColor = glm::vec4(0,0,0,1),
    float backgroundPadding = 6.0f)
{
    Entity e;
    e.ID = id;
    e.tag = tag;
    e.type = EntityType::Text;
    e.drawOrder = drawOrder;
    e.screenSpace = screenSpace;

    e.text.text = text;
    e.text.position = pos;
    e.text.boxWidth = boxW;
    e.text.boxHeight = boxH;
    e.text.wordWrapEnabled = wrap;
    e.text.hAlign = align;
    e.text.scale = scale;
    e.text.color = color;
    e.text.strokeWidth = strokeWidth;

    e.text.backgroundEnabled = backgroundEnabled;
    e.text.backgroundColor = backgroundColor;
    e.text.backgroundPadding = backgroundPadding;

    // Critical: project expects the raw hershey_font* handle here.
    e.text.font = g_hersheyFont;

    return e;
}

// ------------------------------------------------------------

Application::Application()
{
}

void Application::Init(HWND hwnd, int windowWidth, int windowHeight)
{
    hwndMain = hwnd;
    clientWidth = std::max(1, windowWidth);
    clientHeight = std::max(1, windowHeight);

    // Initialize global layer table for this drawing environment.
    layerTable.ResetToDefault();

    // Load font once (if available).
    if (!g_hersheyFont)
    {
        // hersheyfont.h provides hershey_font_load(fontname) which loads:
        //   %HERSHEY_FONTS_DIR%/<fontname>.jhf
        // and defaults to C:\ProgramData\hershey-fonts if the env var isn't set.
        // Example names: "futural", "rowmans", etc.
        g_hersheyFont = hershey_font_load("futural");

#if _DEBUG
    VKLog::Logf(VKLog::Pick, VKLog::Level::Debug,
        "LMB Down mouseClient=(%d,%d) mouseWorld=(%.3f,%.3f)",
        mouseClient.x, mouseClient.y, mouseWorld.x, mouseWorld.y);
#endif
    }

    // Model-space defaults
    modelZoom = 1.0f;
    {
        // Center WORLD origin (0,0) in the client window.
        // world = client/zoom + pan
        // want world(0,0) at client(center) => pan = -center/zoom
        const float invZoom = 1.0f / std::max(0.0001f, modelZoom);
        modelPan = glm::vec2(
            -(clientWidth * 0.5f) * invZoom,
            -(clientHeight * 0.5f) * invZoom);
    }

    // Paper-space defaults (US Letter, inches)
    spaceMode = SpaceMode::Model;
    paperZoom = 1.0f;
    paperPanIn = glm::vec2(0.0f, 0.0f);

    MarkAllDirty();
    EnsureCursorEntities();
    OnResize(clientWidth, clientHeight);
}



void Application::UpdateCameraMatrices()
{
    // world->client: Scale(zoom * unitsToPixels) * Translate(-pan)
    const glm::vec2 unitsToPx = ActiveUnitsToPixels();
    const float z = std::max(0.0001f, ActiveZoom());
    const glm::vec2 s = glm::vec2(z * unitsToPx.x, z * unitsToPx.y);

    view = glm::mat4(1.0f);
    view = glm::scale(view, glm::vec3(s.x, s.y, 1.0f));
    view = glm::translate(view, glm::vec3(-ActivePan(), 0.0f));
}


void Application::OnResize(int w, int h)
{
    clientWidth = std::max(1, w);
    clientHeight = std::max(1, h);

    // Ortho in client pixel units, origin top-left, Y down.
    projection = glm::ortho(0.0f, (float)clientWidth, (float)clientHeight, 0.0f, -1.0f, 1.0f);

    // Apply current pan/zoom to the view matrix (instead of forcing identity).
    UpdateCameraMatrices();

    model = glm::mat4(1.0f);

    MarkAllDirty();
}


void Application::MarkAllDirty()
{
    dirtyScene = true;
    dirtyPickTree = true;
}

void Application::Update(float /*deltaTime*/)
{
    OnMouseMove();

    if (dirtyScene)
    {
        // Remove old grid/scene, rebuild.
        entityBook.RemoveIf([](const Entity& e)
            {
                // NOTE: Do not remove EntityTag::User here. User/script-created
                // entities should persist across view changes and rebuilds.
                return e.tag == EntityTag::Grid || e.tag == EntityTag::Scene || e.tag == EntityTag::Hud || e.tag == EntityTag::Paper;
            });

        hudEntitiesValid = false;

        RebuildGrid();
        RebuildScene();

        // Cursor entities stay; make sure ordering is consistent.
        entityBook.SortByDrawOrder();

        dirtyScene = false;
        dirtyPickTree = true;
    }

    // Cursor overlay updated every frame (box always, crosshair only when selection inactive).
    EnsureCursorEntities();
    UpdateCursorEntities();
    UpdateLineJig();

    // Grips/Grip-hover are disabled while any command is active (current or pending).
    // Grips selection mode is only active when the app is idle.
    if (pendingCommand == PendingCommand::None)
    {
        // Grip hover/selection is available even when Selection mode is OFF.
        EnsureGripEntities(0);
        UpdateGripEntities();
    }
    else
    {
        // Hide immediately to avoid leaving artifacts during commands like ERASE.
        ClearGrip();
        UpdateGripEntities();
    }

    // Debug HUD text (projection bounds, center, cursor position).
    EnsureHudEntities();
    UpdateHudEntities();

    // Hover only when selection mode is active and we are NOT doing a marquee drag.
    if (selectionMode && !marqueeActive)
    {
        EnsurePickTree();
        UpdateHover();
    }
    else
    {
        ClearHover();
    }
}

void Application::RefreshMouseWorldCache()
{
    OnMouseMove();
}

void Application::ToggleOrtho()
{
    orthoEnabled = !orthoEnabled;
}

void Application::SetOneShotSnap(SnapMode mode)
{
    oneShotSnap = mode;
}

void Application::DebugSelectionf(const char* fmt, ...) const
{
    if (!debugSelection || fmt == nullptr)
        return;

    // Bridge the old debugSelection flag to the new logger.
    // Users can now enable/disable selection logs via: LOG ON/OFF SEL
    // and tune verbosity via: LOG LEVEL SEL DEBUG/TRACE
    if (!VKLog::Enabled(VKLog::Selection, VKLog::Level::Debug))
        return;

    va_list args;
    va_start(args, fmt);
    VKLog::VLogf(VKLog::Selection, VKLog::Level::Debug, fmt, args);
    va_end(args);
}

void Application::OnMouseMove()
{
    // Cache cursor position in the currently-selected paper camera (inches) and, when applicable,
    // in model space through the active viewport.
    if (IsPaperSpace())
    {
        const bool prevInVp = mouseInActiveViewport;

        mouseWorldPaper = ClientToPaperWorld(mouseClient);

        mouseInActiveViewport = false;
        mouseWorldModel = glm::vec2(0.0f, 0.0f);

        const Viewport* vp = GetActiveViewport();
        if (vp && PaperPointInViewport(*vp, mouseWorldPaper))
        {
            mouseInActiveViewport = true;
            if (auto m = ClientToViewportModel(mouseClient.x, mouseClient.y))
                mouseWorldModel = *m;
        }

        // Interaction-space world:
        // - Inside an active viewport => model space (edit-through-viewport)
        // - Otherwise => paper space
        mouseWorld = mouseInActiveViewport ? mouseWorldModel : mouseWorldPaper;

        // Switching between paper-entity picking and model-entity picking changes the pick set.
        if (prevInVp != mouseInActiveViewport)
            dirtyPickTree = true;
    }
    else
    {
        mouseWorld = ClientToWorld(mouseClient);
        mouseWorldPaper = mouseWorld;
        mouseWorldModel = mouseWorld;
        mouseInActiveViewport = false;
    }
}

glm::vec2 Application::ClientToWorld(const glm::ivec2& c) const
{
    // client -> world = (client / (zoom * unitsToPixels)) + pan
    const float fx = static_cast<float>(c.x);
    const float fy = static_cast<float>(c.y);
    const float z = std::max(0.0001f, ActiveZoom());
    const glm::vec2 unitsToPx = ActiveUnitsToPixels();
    const float sx = std::max(0.0001f, z * unitsToPx.x);
    const float sy = std::max(0.0001f, z * unitsToPx.y);
    return glm::vec2(fx / sx, fy / sy) + ActivePan();
}

glm::vec2 Application::WorldToClient(const glm::vec2& w) const
{
    // world -> client = (world - pan) * (zoom * unitsToPixels)
    const float z = std::max(0.0001f, ActiveZoom());
    const glm::vec2 unitsToPx = ActiveUnitsToPixels();
    return (w - ActivePan()) * glm::vec2(z * unitsToPx.x, z * unitsToPx.y);
}



// ------------------------------
// Viewports (paper space)
// ------------------------------
const Application::Viewport* Application::GetActiveViewport() const
{
    if (activeViewportIndex < 0 || activeViewportIndex >= (int)viewports.size())
        return nullptr;
    return &viewports[(std::size_t)activeViewportIndex];
}

glm::vec2 Application::ClientToPaperWorld(const glm::ivec2& client) const
{
    // Convert using paper-space active transform (even if a viewport is active).
    // The Application's Active* functions are already in paper-space when SpaceMode::Paper.
    return ClientToWorld(client);
}

glm::vec2 Application::PaperViewportCenter(const Viewport& vp) const
{
    const glm::vec2 mn(std::min(vp.p0In.x, vp.p1In.x), std::min(vp.p0In.y, vp.p1In.y));
    const glm::vec2 mx(std::max(vp.p0In.x, vp.p1In.x), std::max(vp.p0In.y, vp.p1In.y));
    return 0.5f * (mn + mx);
}

bool Application::PaperPointInViewport(const Viewport& vp, const glm::vec2& paperIn) const
{
    const glm::vec2 mn(std::min(vp.p0In.x, vp.p1In.x), std::min(vp.p0In.y, vp.p1In.y));
    const glm::vec2 mx(std::max(vp.p0In.x, vp.p1In.x), std::max(vp.p0In.y, vp.p1In.y));
    return (paperIn.x >= mn.x && paperIn.x <= mx.x && paperIn.y >= mn.y && paperIn.y <= mx.y);
}

int Application::FindViewportAtPaperPoint(const glm::vec2& paperIn) const
{
    for (int i = (int)viewports.size() - 1; i >= 0; --i)
    {
        if (PaperPointInViewport(viewports[(std::size_t)i], paperIn))
            return i;
    }
    return -1;
}



static int FindViewportIndexByBorderEntityId(const std::vector<Application::Viewport>& vps, uint32_t entityId)
{
    for (int i = 0; i < (int)vps.size(); ++i)
    {
        const auto& vp = vps[(std::size_t)i];
        if (entityId >= vp.id + 1 && entityId <= vp.id + 4)
            return i;
    }
    return -1;
}

void Application::SetActiveViewport(int index)
{
    if (index < -1) index = -1;
    if (index >= (int)viewports.size()) index = -1;

    for (auto& vp : viewports) vp.active = false;

    activeViewportIndex = index;
    if (activeViewportIndex >= 0)
        viewports[(std::size_t)activeViewportIndex].active = true;

    MarkAllDirty();
}

void Application::BeginViewportCreate()
{
    if (!IsPaperSpace())
        return;

    viewportCreateArmed = true;
    viewportCreateTwoClick = false;
    viewportCreateHasFirst = false;
    viewportCreateDragging = false;
    viewportFirstCornerIn = {};
    viewportCurrentCornerIn = {};
}

void Application::BeginViewportCommand(CmdWindow* cmdWnd)
{
    BeginViewportCreate();
    if (!viewportCreateArmed)
        return;

    viewportCreateTwoClick = true;
    viewportCreateCmdWnd = cmdWnd;
    if (cmdWnd)
        cmdWnd->AppendText(L"VIEWPORT: Pick first corner.\r\n");
}



void Application::CancelViewportCreate()
{
    viewportCreateArmed = false;
    viewportCreateTwoClick = false;
    viewportCreateHasFirst = false;
    viewportCreateDragging = false;
    viewportCreateCmdWnd = nullptr;
}

void Application::UpdateViewportCreateDrag()
{
    if (!IsPaperSpace() || !viewportCreateArmed || !viewportCreateHasFirst)
        return;

    // Drag-create updates only while dragging; two-click updates continuously to show a live preview.
    if (!viewportCreateTwoClick && !viewportCreateDragging)
        return;

    viewportCurrentCornerIn = ClientToPaperWorld(mouseClient);
}

static uint32_t EnsureLayerByName(LayerTable& lt, const std::string& name)
{
    for (const auto& L : lt.GetLayers())
        if (L.name == name)
            return L.id;

    const uint32_t id = lt.AddLayer(name);
    lt.RenameLayer(id, name);
    return id;

}

static bool ComputeModelExtents2D(const EntityBook& book, glm::vec2& outMin, glm::vec2& outMax)
{
    bool any = false;
    glm::vec2 mn( std::numeric_limits<float>::infinity(),  std::numeric_limits<float>::infinity());
    glm::vec2 mx(-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity());

    const auto& ents = book.GetEntities();
    for (const Entity& e : ents)
    {
        if (e.screenSpace) continue;
        if (!(e.tag == EntityTag::Scene || e.tag == EntityTag::User)) continue;

        if (e.type == EntityType::Line)
        {
            const glm::vec2 a(e.line.start.x, e.line.start.y);
            const glm::vec2 b(e.line.end.x,   e.line.end.y);
            mn = glm::min(mn, glm::min(a, b));
            mx = glm::max(mx, glm::max(a, b));
            any = true;
        }
        else if (e.type == EntityType::Text)
        {
            // Text is converted to lines at render time; include its anchor point as a conservative extent.
            const glm::vec2 p(e.text.position.x, e.text.position.y);
            mn = glm::min(mn, p);
            mx = glm::max(mx, p);
            any = true;
        }
    }

    if (!any) return false;
    outMin = mn; outMax = mx;
    return true;
}

static void SeedViewportZoomToFit_OriginCentered(Application::Viewport& vp, const EntityBook& book)
{
    glm::vec2 mn, mx;
    if (!ComputeModelExtents2D(book, mn, mx))
        return;

    // Fit extents while forcing model origin (0,0) at viewport center.
    const float halfX = std::max(std::abs(mn.x), std::abs(mx.x));
    const float halfY = std::max(std::abs(mn.y), std::abs(mx.y));

    const glm::vec2 pMn(std::min(vp.p0In.x, vp.p1In.x), std::min(vp.p0In.y, vp.p1In.y));
    const glm::vec2 pMx(std::max(vp.p0In.x, vp.p1In.x), std::max(vp.p0In.y, vp.p1In.y));
    const glm::vec2 vpSize = pMx - pMn;

    const float safeHalfX = std::max(0.001f, halfX);
    const float safeHalfY = std::max(0.001f, halfY);

    const float paperPerModelX = vpSize.x / (2.0f * safeHalfX);
    const float paperPerModelY = vpSize.y / (2.0f * safeHalfY);
    const float paperPerModel = 0.90f * std::min(paperPerModelX, paperPerModelY); // margin

    const float modelPerPaper = std::max(0.0001f, vp.modelUnitsPerPaperUnit);

    vp.modelCenter = glm::vec2(0.0f, 0.0f);
    vp.modelZoom = std::max(0.0001f, paperPerModel * modelPerPaper);

}

void Application::CommitViewportCreate()
{
    if (!IsPaperSpace() || !viewportCreateArmed || !viewportCreateHasFirst)
        return;

    // If we weren't dragging (e.g. click without move), treat current corner as mouse point now.
    if (!viewportCreateDragging)
        viewportCurrentCornerIn = ClientToPaperWorld(mouseClient);

    const glm::vec2 p0 = viewportFirstCornerIn;
    const glm::vec2 p1 = viewportCurrentCornerIn;

    // Reject tiny viewports (inches)
    const float w = std::abs(p1.x - p0.x);
    const float h = std::abs(p1.y - p0.y);
    if (w < 0.05f || h < 0.05f)
    {
        CancelViewportCreate();
        return;
    }

    Viewport vp{};
    vp.id = (uint32_t)nextId++; // stable id
    vp.p0In = p0;
    vp.p1In = p1;

    vp.modelUnitsPerPaperUnit = 10.0f;
    vp.modelCenter = glm::vec2(0.0f, 0.0f);
    vp.modelZoom = 1.0f;

    // Seed viewport camera so we can immediately see the model-space demo (dragon) inside the viewport.
    SeedViewportZoomToFit_OriginCentered(vp, entityBook);

    vp.locked = false;
    vp.active = false;
    vp.contentsVisible = true;

    // Place viewport border on the CURRENT layer (user-selected) at creation time.
// If no current layer is set, fall back to VPORT-BORDER.
    vp.borderLayerId = layerTable.CurrentLayerId();
    if (vp.borderLayerId == LayerTable::kInvalidLayerId)
        vp.borderLayerId = EnsureLayerByName(layerTable, "VPORT-BORDER");

    viewports.push_back(vp);

    // If this viewport was created via the VIEWPORT command (two-click), report completion.
    CmdWindow* finishedCmdWnd = nullptr;
    if (viewportCreateTwoClick && viewportCreateCmdWnd)
    {
        viewportCreateCmdWnd->AppendText(L"VIEWPORT: Completed.\r\n");
        finishedCmdWnd = viewportCreateCmdWnd;
    }

    CancelViewportCreate();
    if (finishedCmdWnd)
        finishedCmdWnd->ShowBasePromptIfIdle();
    MarkAllDirty();
}

void Application::ToggleActiveViewportLock()
{
    if (!IsPaperSpace())
        return;

    if (activeViewportIndex < 0 || activeViewportIndex >= (int)viewports.size())
        return;

    viewports[(std::size_t)activeViewportIndex].locked = !viewports[(std::size_t)activeViewportIndex].locked;
}


const Application::Viewport* Application::GetSelectedViewport() const
{
    if (!IsPaperSpace())
        return nullptr;
    if (selectedViewportIndex < 0 || selectedViewportIndex >= (int)viewports.size())
        return nullptr;
    return &viewports[(std::size_t)selectedViewportIndex];
}

const Application::Viewport* Application::GetViewportByIndex(int index) const
{
    if (!IsPaperSpace())
        return nullptr;
    if (index < 0 || index >= (int)viewports.size())
        return nullptr;
    return &viewports[(std::size_t)index];
}

bool Application::GetSelectedViewports(std::vector<int>& outViewportIndices) const
{
    outViewportIndices.clear();
    if (!IsPaperSpace())
        return false;
    if (selectedIndices.empty())
        return false;

    // Map selected border entity ids -> viewport index and edge bit.
    // Require that every selected entity belongs to SOME viewport border, and that
    // each included viewport has ALL 4 border segments selected.
    std::unordered_map<int, int> maskByVp;
    const auto& ents = entityBook.GetEntities();

    for (std::size_t selIdx : selectedIndices)
    {
        if (selIdx >= ents.size())
            return false;

        const uint32_t id = (uint32_t)ents[selIdx].ID;
        const int vpIndex = FindViewportIndexByBorderEntityId(viewports, id);
        if (vpIndex < 0)
            return false;

        const uint32_t base = viewports[(std::size_t)vpIndex].id;
        int bit = 0;
        if (id == base + 1) bit = 1 << 0;
        else if (id == base + 2) bit = 1 << 1;
        else if (id == base + 3) bit = 1 << 2;
        else if (id == base + 4) bit = 1 << 3;
        else
            return false;

        maskByVp[vpIndex] |= bit;
    }

    if (maskByVp.empty())
        return false;

    outViewportIndices.reserve(maskByVp.size());
    for (const auto& kv : maskByVp)
    {
        if (kv.second != 0xF)
            return false;
        outViewportIndices.push_back(kv.first);
    }

    std::sort(outViewportIndices.begin(), outViewportIndices.end());
    return true;
}

bool Application::IsViewportSelection(int& outViewportIndex) const
{
    outViewportIndex = -1;
    if (!IsPaperSpace())
        return false;
    if (selectedViewportIndex < 0 || selectedViewportIndex >= (int)viewports.size())
        return false;
    if (selectedIndices.size() != 4)
        return false;

    const auto& vp = viewports[(std::size_t)selectedViewportIndex];
    const auto& ents = entityBook.GetEntities();

    bool found[4] = { false,false,false,false };

    for (std::size_t idx : selectedIndices)
    {
        if (idx >= ents.size())
            return false;
        const uint32_t id = static_cast<uint32_t>(ents[idx].ID);
        if (id == vp.id + 1) found[0] = true;
        else if (id == vp.id + 2) found[1] = true;
        else if (id == vp.id + 3) found[2] = true;
        else if (id == vp.id + 4) found[3] = true;
        else
            return false;
    }

    if (found[0] && found[1] && found[2] && found[3])
    {
        outViewportIndex = selectedViewportIndex;
        return true;
    }
    return false;
}

void Application::SetSelectedViewportLocked(bool locked)
{
    if (auto* vp = const_cast<Viewport*>(GetSelectedViewport()))
    {
        vp->locked = locked;
        MarkAllDirty();
    }
}

void Application::SetSelectedViewportModelUnitsPerPaperUnit(float muPerPaper)
{
    if (auto* vp = const_cast<Viewport*>(GetSelectedViewport()))
    {
        vp->modelUnitsPerPaperUnit = std::max(0.0001f, muPerPaper);
        MarkAllDirty();
    }
}

std::optional<glm::vec2> Application::ClientToViewportModel(int cx, int cy) const
{
    if (!IsPaperSpace())
        return std::nullopt;

    const Viewport* vp = GetActiveViewport();
    if (!vp)
        return std::nullopt;

    const glm::vec2 paperIn = ClientToPaperWorld(glm::ivec2(cx, cy));
    if (!PaperPointInViewport(*vp, paperIn))
        return std::nullopt;

    const glm::vec2 centerPaper = PaperViewportCenter(*vp);

    const float modelPerPaper = std::max(0.0001f, vp->modelUnitsPerPaperUnit);
    const float paperPerModel = (1.0f / modelPerPaper) * std::max(0.0001f, vp->modelZoom);

    const glm::vec2 dPaper = (paperIn - centerPaper);
    const glm::vec2 model = vp->modelCenter + dPaper / paperPerModel;
    return model;
}


bool Application::IsViewportInteractionActive() const
{
    return IsPaperSpace() && HasActiveViewport() && mouseInActiveViewport;
}

bool Application::IsCmdLineIdle() const
{
    // PendingCommand covers interactive commands that intentionally suppress the base prompt
    // (e.g. LINE, ERASE). Viewport creation is a separate state machine.
    if (pendingCommand != PendingCommand::None)
        return false;
    if (viewportCreateArmed)
        return false;
    return true;
}

void Application::GetInteractionPixelsPerWorld(float& outPxPerWorldX, float& outPxPerWorldY) const
{
    // Returns pixels-per-world-unit for the current interaction-space:
    // - model: pixels per model unit
    // - paper: pixels per inch
    if (IsViewportInteractionActive())
    {
        const Viewport* vp = GetActiveViewport();
        if (vp)
        {
            const float modelPerPaper = std::max(0.0001f, vp->modelUnitsPerPaperUnit);
            const float paperPerModel = (1.0f / modelPerPaper) * std::max(0.0001f, vp->modelZoom);

            const float dpiX = std::max(1.0f, page.dpiX);
            const float dpiY = std::max(1.0f, page.dpiY);
            const float zPaper = std::max(0.0001f, paperZoom);

            outPxPerWorldX = (zPaper * dpiX) * paperPerModel;
            outPxPerWorldY = (zPaper * dpiY) * paperPerModel;
            return;
        }
    }

    // Default camera scale.
    const float z = std::max(0.0001f, ActiveZoom());
    const glm::vec2 unitsToPx = ActiveUnitsToPixels();
    outPxPerWorldX = z * std::max(0.0001f, unitsToPx.x);
    outPxPerWorldY = z * std::max(0.0001f, unitsToPx.y);
}
glm::vec2 Application::ViewportModelToClient(const Viewport& vp, const glm::vec2& modelPt) const
{
    const glm::vec2 centerPaper = PaperViewportCenter(vp);

    const float modelPerPaper = std::max(0.0001f, vp.modelUnitsPerPaperUnit);
    const float paperPerModel = (1.0f / modelPerPaper) * std::max(0.0001f, vp.modelZoom);

    const glm::vec2 paperIn = centerPaper + (modelPt - vp.modelCenter) * paperPerModel;
    return WorldToClient(paperIn);
}


glm::vec2 Application::ViewportModelToClientPx(const Viewport& vp, const glm::vec2& modelPt) const
{
    return ViewportModelToClient(vp, modelPt);
}


glm::vec2 Application::PaperToClientPx(const glm::vec2& paperIn) const
{
    return WorldToClient(paperIn);
}

// Build a viewport-specific view matrix that maps model space into paper space (inches),
// then through the current paper camera into client pixels.
glm::mat4 Application::GetViewportViewMatrix(const Viewport& vp) const
{
    const glm::vec2 mn(std::min(vp.p0In.x, vp.p1In.x), std::min(vp.p0In.y, vp.p1In.y));
    const glm::vec2 mx(std::max(vp.p0In.x, vp.p1In.x), std::max(vp.p0In.y, vp.p1In.y));
    const glm::vec2 centerPaper = (mn + mx) * 0.5f;

    const float modelPerPaper = std::max(0.0001f, vp.modelUnitsPerPaperUnit);
    const float paperPerModel = (1.0f / modelPerPaper) * std::max(0.0001f, vp.modelZoom);

    glm::mat4 modelToPaper(1.0f);
    modelToPaper = glm::translate(modelToPaper, glm::vec3(centerPaper.x, centerPaper.y, 0.0f));
    modelToPaper = glm::scale(modelToPaper, glm::vec3(paperPerModel, paperPerModel, 1.0f));
    modelToPaper = glm::translate(modelToPaper, glm::vec3(-vp.modelCenter.x, -vp.modelCenter.y, 0.0f));

    // 'view' is the current paper-space camera matrix (inches -> pixels), set by UpdateCameraMatrices().
    return view * modelToPaper;
}

// ------------------------------------------------------------
// Right-mouse panning
// ------------------------------------------------------------
void Application::BeginMousePan(int clientX, int clientY)
{
    mousePanning = true;
    mousePanLastClient = { clientX, clientY };
}

void Application::UpdateMousePan(int clientX, int clientY)
{
    if (!mousePanning)
        return;

    const glm::ivec2 now(clientX, clientY);
    const glm::ivec2 delta = now - mousePanLastClient;
    mousePanLastClient = now;

    // If a paper-space viewport is active (and unlocked) and the cursor is inside it,
    // pan the viewport's model camera instead of the paper-space camera.
    if (IsPaperSpace())
    {
        if (activeViewportIndex >= 0 && activeViewportIndex < (int)viewports.size())
        {
            Viewport& vp = viewports[(std::size_t)activeViewportIndex];
            if (!vp.locked)
            {
                const glm::vec2 paperIn = ClientToPaperWorld(now);
                if (PaperPointInViewport(vp, paperIn))
                {
                    float pxPerWorldX = 1.0f, pxPerWorldY = 1.0f;
                    GetInteractionPixelsPerWorld(pxPerWorldX, pxPerWorldY);
                    const float invX = 1.0f / std::max(0.0001f, pxPerWorldX);
                    const float invY = 1.0f / std::max(0.0001f, pxPerWorldY);

                    // Match existing pan direction convention: dragging right pans the view right.
                    vp.modelCenter -= glm::vec2(delta.x * invX, delta.y * invY);
                    MarkAllDirty();
                    return;
                }
            }
        }
    }

    // Convert client pixel delta into world delta.
    const float z = std::max(0.0001f, ActiveZoom());
    const glm::vec2 unitsToPx = ActiveUnitsToPixels();
    const float invX = 1.0f / std::max(0.0001f, z * unitsToPx.x);
    const float invY = 1.0f / std::max(0.0001f, z * unitsToPx.y);
    ActivePan() -= glm::vec2(delta.x * invX, delta.y * invY);

    UpdateCameraMatrices();
    MarkAllDirty();
}


void Application::EndMousePan()
{
    mousePanning = false;
}

// ------------------------------------------------------------
// Zoom/pan helpers
// ------------------------------------------------------------
void Application::ZoomAtClient(int cx, int cy, float zoomFactor)
{
    // If a paper-space viewport is active (and unlocked) and the cursor is inside it,
    // zoom should operate on the viewport's model camera, not on paper-space itself.
    if (IsPaperSpace())
    {
        Viewport* vp = nullptr;
        if (activeViewportIndex >= 0 && activeViewportIndex < (int)viewports.size())
            vp = &viewports[(std::size_t)activeViewportIndex];

        if (vp && !vp->locked)
        {
            const glm::vec2 paperIn = ClientToPaperWorld(glm::ivec2(cx, cy));
            if (PaperPointInViewport(*vp, paperIn))
            {
                const auto beforeOpt = ClientToViewportModel(cx, cy);
                if (!beforeOpt.has_value())
                    return;

                const glm::vec2 before = beforeOpt.value();
                const float old = vp->modelZoom;
                vp->modelZoom = std::clamp(vp->modelZoom * zoomFactor, 0.02f, 200.0f);

                // Keep the same model point under the cursor.
                const auto afterOpt = ClientToViewportModel(cx, cy);
                if (afterOpt.has_value())
                {
                    const glm::vec2 after = afterOpt.value();
                    vp->modelCenter += (before - after);
                }

                MarkAllDirty();
                return;
            }
        }
    }


    float& z = ActiveZoom();
    const float oldZoom = z;
    z = std::clamp(z * zoomFactor, 0.02f, 200.0f);

    // Keep the point under the cursor stable while zooming:
    const glm::vec2 client((float)cx, (float)cy);
    const glm::vec2 unitsToPx = ActiveUnitsToPixels();
    const glm::vec2 sOld(std::max(0.0001f, oldZoom * unitsToPx.x), std::max(0.0001f, oldZoom * unitsToPx.y));
    const glm::vec2 sNew(std::max(0.0001f, z * unitsToPx.x), std::max(0.0001f, z * unitsToPx.y));

    const glm::vec2 worldUnder = glm::vec2(client.x / sOld.x, client.y / sOld.y) + ActivePan();
    ActivePan() = worldUnder - glm::vec2(client.x / sNew.x, client.y / sNew.y);

    UpdateCameraMatrices();
    MarkAllDirty();
}


void Application::PanByPixels(int dx, int dy)
{
    // If a paper-space viewport is active (and unlocked) and the cursor is inside it,
    // panning should operate on the viewport's model camera.
    if (IsPaperSpace())
    {
        Viewport* vp = nullptr;
        if (activeViewportIndex >= 0 && activeViewportIndex < (int)viewports.size())
            vp = &viewports[(std::size_t)activeViewportIndex];

        if (vp && !vp->locked)
        {
            const glm::vec2 paperIn = ClientToPaperWorld(mouseClient);
            if (PaperPointInViewport(*vp, paperIn))
            {
                const float zPaper = std::max(0.0001f, paperZoom);
                const glm::vec2 u2p = ActiveUnitsToPixels(); // paper dpi
                const glm::vec2 dpPaperIn = glm::vec2((float)dx / (zPaper * u2p.x), (float)dy / (zPaper * u2p.y));

                const float modelPerPaper = std::max(0.0001f, vp->modelUnitsPerPaperUnit);
                const float paperPerModel = (1.0f / modelPerPaper) * std::max(0.0001f, vp->modelZoom);

                // Dragging right should move the view right, which means modelCenter moves left.
                vp->modelCenter -= dpPaperIn / paperPerModel;

                MarkAllDirty();
                return;
            }
        }
    }


    const float z = std::max(0.0001f, ActiveZoom());
    const glm::vec2 unitsToPx = ActiveUnitsToPixels();
    const float invX = 1.0f / std::max(0.0001f, z * unitsToPx.x);
    const float invY = 1.0f / std::max(0.0001f, z * unitsToPx.y);
    ActivePan() += glm::vec2(dx * invX, dy * invY);

    UpdateCameraMatrices();
    MarkAllDirty();
}


// ------------------------------------------------------------
// Modes
// ------------------------------------------------------------
void Application::ToggleSelectionMode()
{
    selectionMode = !selectionMode;

    // When leaving selection mode, clear hover highlight (crosshair will return).
    if (!selectionMode)
        ClearHover();

    MarkAllDirty();
}

void Application::ToggleGrid()
{
    gridEnabled = !gridEnabled;
    MarkAllDirty();
}

void Application::ToggleWipeout()
{
    wipeoutEnabled = !wipeoutEnabled;
    MarkAllDirty();
}

// ------------------------------------------------------------
// Paper space
// ------------------------------------------------------------
glm::vec2& Application::ActivePan()
{
    return (spaceMode == SpaceMode::Paper) ? paperPanIn : modelPan;
}

const glm::vec2& Application::ActivePan() const
{
    return (spaceMode == SpaceMode::Paper) ? paperPanIn : modelPan;
}

float& Application::ActiveZoom()
{
    return (spaceMode == SpaceMode::Paper) ? paperZoom : modelZoom;
}

float Application::ActiveZoom() const
{
    return (spaceMode == SpaceMode::Paper) ? paperZoom : modelZoom;
}

glm::vec2 Application::ActiveUnitsToPixels() const
{
    if (spaceMode == SpaceMode::Paper)
        return glm::vec2(std::max(1.0f, page.dpiX), std::max(1.0f, page.dpiY));
    return glm::vec2(1.0f, 1.0f);
}

void Application::TogglePaperSpaceMode()
{
    if (spaceMode == SpaceMode::Paper)
    {
        spaceMode = SpaceMode::Model;
        UpdateCameraMatrices();
        MarkAllDirty();
        return;
    }

    spaceMode = SpaceMode::Paper;

    // Choose a zoom that fits the whole letter page comfortably in the window.
    const float W = page.widthIn;
    const float H = page.heightIn;
    const float dpiX = std::max(1.0f, page.dpiX);
    const float dpiY = std::max(1.0f, page.dpiY);

    const float fitX = (float)clientWidth  / (W * dpiX);
    const float fitY = (float)clientHeight / (H * dpiY);
    paperZoom = std::clamp(std::min(fitX, fitY) * 0.90f, 0.02f, 200.0f);

    // Center the page.
    const glm::vec2 pageCenter(W * 0.5f, H * 0.5f);
    const glm::vec2 clientCenter((float)clientWidth * 0.5f, (float)clientHeight * 0.5f);
    const glm::vec2 s(std::max(0.0001f, paperZoom * dpiX), std::max(0.0001f, paperZoom * dpiY));
    paperPanIn = pageCenter - glm::vec2(clientCenter.x / s.x, clientCenter.y / s.y);

    UpdateCameraMatrices();
    MarkAllDirty();
}

RECT Application::GetPageClientRect() const
{
    // Page bounds in world (paper inches): (0,0) .. (W,H)
    const float W = page.widthIn;
    const float H = page.heightIn;

    const glm::vec2 c00 = WorldToClient(glm::vec2(0.0f, 0.0f));
    const glm::vec2 c11 = WorldToClient(glm::vec2(W, H));

    const float left = std::min(c00.x, c11.x);
    const float top = std::min(c00.y, c11.y);
    const float right = std::max(c00.x, c11.x);
    const float bottom = std::max(c00.y, c11.y);

    RECT r{};
    r.left = (LONG)std::floor(left);
    r.top = (LONG)std::floor(top);
    r.right = (LONG)std::ceil(right);
    r.bottom = (LONG)std::ceil(bottom);
    return r;
}

// ------------------------------------------------------------
// Picking / selection
// ------------------------------------------------------------
void Application::OnLeftClick(HWND /*hwnd*/)
{
    // Backwards-compat wrapper (existing call sites).
    // This is treated as "LMB down".
    OnLeftDown(nullptr);
}

void Application::OnLeftClick()
{
    OnLeftDown(nullptr);

    // NOTE: This function intentionally just forwards to OnLeftDown.
    // The closing brace was previously missing, which caused subsequent
    // member function definitions to be parsed as illegal local functions.
}

void Application::OnLeftDoubleClick(HWND /*hwnd*/)
{
    if (!IsPaperSpace())
        return;

    // Activate/deactivate viewport by double-clicking inside its rectangle.
    const glm::vec2 pIn = ClientToPaperWorld(mouseClient);
    const int hit = FindViewportAtPaperPoint(pIn);

    if (hit >= 0)
        SetActiveViewport(hit);
    else
        SetActiveViewport(-1);
}

void Application::OnLeftDown(HWND /*hwnd*/)
{
    // LINE point-entry: left click picks nodes.
    if (pendingCommand == PendingCommand::Line)
    {
        // Determine, at click-time (not from stale cached state), whether we are editing
        // through an active viewport or placing a paper-space annotation.
        bool inActiveVpNow = false;
        if (IsPaperSpace())
        {
            const Viewport* vp = GetActiveViewport();
            if (vp && PaperPointInViewport(*vp, mouseWorldPaper))
                inActiveVpNow = true;
        }

        const glm::vec2 pick2 = inActiveVpNow ? mouseWorldModel : mouseWorldPaper;
        glm::vec3 p(pick2.x, pick2.y, 0.0f);
        p = ApplyOneShotSnap(oneShotSnap, p);
        if (lineStage == LineStage::FirstNode)
        {
            lineFirstNode = p;
            oneShotSnap = SnapMode::None;
            lineHasFirst = true;
            lineStage = LineStage::SecondNode;
            if (lineCmdWnd) lineCmdWnd->AppendText(L"\r\nsecond node: ");
            return;
        }
        if (lineStage == LineStage::SecondNode && lineHasFirst)
        {
            glm::vec3 end = p;
            if (orthoEnabled)
                end = OrthoProjectFromCursor(lineFirstNode, p);

            Entity e;
            e.ID = nextId++;
            // In paper space:
            // - if we're editing through an active viewport, the LINE is model-space => User
            // - otherwise it's a paper-space annotation => Paper
            e.tag = (IsPaperSpace() && !inActiveVpNow) ? EntityTag::PaperUser : EntityTag::User;
            e.type = EntityType::Line;
            e.drawOrder = 0;
            e.screenSpace = false;
            e.layerId = layerTable.CurrentLayerId();
            e.colorByLayer = true;
            e.linetypeByLayer = true;
            e.line.start = lineFirstNode;
            e.line.end = end;
            // Default line color: ByLayer; but for Paper space the default layer is often white.
            // If the layer is white, the entity would be invisible on the white sheet.
            // So for paper-space annotation lines, force black unless the user later changes it.
            if (e.tag == EntityTag::PaperUser)
            {
                e.colorByLayer = false;
                e.line.color = glm::vec4(0, 0, 0, 1);
            }
            else
            {
                e.line.color = glm::vec4(1, 1, 1, 1);
            }
            e.line.thickness = 1.5f;

            entityBook.AddEntity(e);
            entityBook.SortByDrawOrder();
            dirtyPickTree = true;
            dirtyScene = true;

            pendingCommand = PendingCommand::None;
            lineStage = LineStage::None;
            lineHasFirst = false;
            crosshairsMode = CrosshairsMode::GripsSelectionCrosshairs;
            oneShotSnap = SnapMode::None;
            if (lineCmdWnd)
            {
                lineCmdWnd->AppendText(L"\r\n");
                // Return to the base prompt now that the command is complete.
                lineCmdWnd->ShowBasePromptIfIdle();
            }
            lineCmdWnd = nullptr;
            return;
        }
    }

    // Paper-space viewport creation.
// - Drag-create (default): click-drag-release creates viewport.
// - VIEWPORT command (two-click): first click sets corner 1, second click sets corner 2.
    if (IsPaperSpace() && viewportCreateArmed)
    {
        const glm::vec2 pIn = ClientToPaperWorld(mouseClient);

        if (viewportCreateTwoClick)
        {
            if (!viewportCreateHasFirst)
            {
                viewportFirstCornerIn = pIn;
                viewportCurrentCornerIn = pIn;
                viewportCreateHasFirst = true;
                viewportCreateDragging = false;
                return;
            }
            else
            {
                viewportCurrentCornerIn = pIn;
                viewportCreateDragging = false;
                CommitViewportCreate();
                return;
            }
        }
        else
        {
            viewportFirstCornerIn = pIn;
            viewportCurrentCornerIn = pIn;
            viewportCreateHasFirst = true;
            viewportCreateDragging = true;
            return;
        }
    }

    EnsurePickTree();

    // Two-click marquee (explicit Window/Crossing): if we already have the first corner,
    // the *next* click should complete the rectangle even if it lands on geometry.
    // Otherwise dense drawings can swallow the second click via normal hit-testing.
    if (marqueeMode != MarqueeMode::AutoByDrag && marqueeActive && marqueeAwaitSecondClick)
    {
        marqueeEndClient = mouseClient;
        marqueeEndWorld = mouseWorld;

        VKLog::Logf(VKLog::Command, VKLog::Level::Info,
            "MarqueeSecondClick client=(%d,%d) world=(%.3f,%.3f)",
            mouseClient.x, mouseClient.y, mouseWorld.x, mouseWorld.y);

        FinishMarqueeSelect();
        marqueeAwaitSecondClick = false;
        return;
    }

    // Allow selection mutations only within explicit input handlers.
    SelectionMutationScope selMut(*this);


    // Grips-selection: when selection mode is OFF, allow LMB click on a hovered grip target.
    // Disabled while any command is active.
    if (pendingCommand == PendingCommand::None && !selectionMode && gripTargetEntityId != 0)
    {
#ifdef _WIN32
        const bool additive = ((GetKeyState(VK_SHIFT) & 0x8000) != 0) || ((GetKeyState(VK_CONTROL) & 0x8000) != 0);
#else
        const bool additive = false;
#endif

        // If the grip target is a viewport border segment in paper space, select the whole viewport frame.
        if (IsPaperSpace())
        {
            const int vpIndex = FindViewportIndexByBorderEntityId(viewports, (uint32_t)gripTargetEntityId);
            if (vpIndex >= 0)
            {
                selectedViewportIndex = vpIndex;
                const uint32_t base = viewports[(std::size_t)vpIndex].id;
                SelectEntityById(base + 1, additive);
                SelectEntityById(base + 2, true);
                SelectEntityById(base + 3, true);
                SelectEntityById(base + 4, true);
                return;
            }
        }

        SelectEntityById(gripTargetEntityId, additive);
        return;
    }

    // Picker square size is defined in client pixels, converted to world units (interaction-space).
    float pxPerWorldX = 1.0f, pxPerWorldY = 1.0f;
    GetInteractionPixelsPerWorld(pxPerWorldX, pxPerWorldY);
    const float halfSizeX = (0.5f * static_cast<float>(SELECTION_BOX_SIZE_PX)) / std::max(0.0001f, pxPerWorldX);
    const float halfSizeY = (0.5f * static_cast<float>(SELECTION_BOX_SIZE_PX)) / std::max(0.0001f, pxPerWorldY);
    const BoundingBox box(
        mouseWorld.x - halfSizeX, mouseWorld.y - halfSizeY, -1.0f,
        mouseWorld.x + halfSizeX, mouseWorld.y + halfSizeY, 1.0f);

#if _DEBUG
    VKLog::Logf(VKLog::Pick, VKLog::Level::Debug,
        "LMB Down mouseClient=(%d,%d) mouseWorld=(%.3f,%.3f)",
        mouseClient.x, mouseClient.y, mouseWorld.x, mouseWorld.y);
#endif

    const auto hit = pickTree.QueryFirstIntersect(box);
    if (hit.has_value())
    {
        const std::size_t idx = *hit;

        // Paper-space viewport frame selection: click any border segment selects the whole viewport.
        if (IsPaperSpace())
        {
            const auto& ents = entityBook.GetEntities();
            if (idx < ents.size() && ents[idx].tag == EntityTag::Paper)
            {
                const uint32_t hitId = static_cast<uint32_t>(ents[idx].ID);
                const int vpIndex = FindViewportIndexByBorderEntityId(viewports, hitId);
                if (vpIndex >= 0)
                {
#ifdef _WIN32
                    const bool additive = ((GetKeyState(VK_SHIFT) & 0x8000) != 0) || ((GetKeyState(VK_CONTROL) & 0x8000) != 0);
#else
                    const bool additive = false;
#endif

                    // Select all 4 border entities for this viewport.
                    if (!additive)
                        ClearSelection();

                    selectedViewportIndex = vpIndex;

                    const uint32_t base = viewports[(std::size_t)vpIndex].id;
                    SelectEntityById(base + 1, additive);
                    SelectEntityById(base + 2, true);
                    SelectEntityById(base + 3, true);
                    SelectEntityById(base + 4, true);

                    // Prevent hover from immediately undoing selection
                    if (hoveredIndex.has_value() && *hoveredIndex == idx)
                        hoveredIndex.reset();

                    return;
                }
            }
        }

        // Normal single entity select
        selectedViewportIndex = -1;
        if (IsIndexSelected(idx))
        {
            // idx is size_t; selectedIndex is an int UI helper.
            const size_t kMaxInt = (size_t)std::numeric_limits<int>::max();
            selectedIndex = (idx <= kMaxInt) ? (int)idx : -1;
        }
        else
        {
            AddToSelection(idx);
        }

        // Prevent hover from immediately undoing selection
        if (hoveredIndex.has_value() && *hoveredIndex == idx)
            hoveredIndex.reset();

        return;
    }

    selectedViewportIndex = -1;

    // No hit => start a marquee selection rectangle.
    // In interactive W/C modes, use two-click rectangle: first click sets start, second click completes.
    if (marqueeMode != MarqueeMode::AutoByDrag)
    {
        if (!marqueeActive)
        {
            BeginMarquee();

            VKLog::Logf(VKLog::Command, VKLog::Level::Info,
                "MarqueeFirstClick client=(%d,%d) world=(%.3f,%.3f) mode=%s",
                mouseClient.x, mouseClient.y, mouseWorld.x, mouseWorld.y,
                (marqueeMode == MarqueeMode::Crossing ? "Crossing" : "Window"));
            marqueeAwaitSecondClick = true;
            return;
        }

        if (marqueeAwaitSecondClick)
        {
            marqueeEndClient = mouseClient;
            marqueeEndWorld = mouseWorld;
            FinishMarqueeSelect();
            marqueeAwaitSecondClick = false;
            return;
        }
    }

    BeginMarquee();
}

void Application::OnLeftUp(HWND /*hwnd*/)
{
    if (IsPaperSpace() && viewportCreateArmed && viewportCreateHasFirst)
    {
        // Drag-create commits on mouse-up; two-click commits on the second click-down.
        if (!viewportCreateTwoClick)
        {
            CommitViewportCreate();
            return;
        }
    }
    if (!marqueeActive)
        return;

    // Drag-marquee completes on mouse-up; two-click marquee completes on 2nd click-down.
    if (marqueeMode == MarqueeMode::AutoByDrag && !marqueeAwaitSecondClick)
        FinishMarqueeSelect();
}

void Application::UpdateMarqueeDrag(int clientX, int clientY)
{
    if (!marqueeActive)
        return;

    marqueeEndClient = { clientX, clientY };

    // The marquee world-space must match the current interaction-space:
    // - Paper space (no active viewport interaction): paper inches
    // - Paper space (active viewport interaction): model units through the viewport
    // - Model space: model units
    if (IsPaperSpace())
    {
        const glm::vec2 paper = ClientToPaperWorld(marqueeEndClient);
        glm::vec2 w = paper;

        const Viewport* vp = GetActiveViewport();
        if (vp && PaperPointInViewport(*vp, paper))
        {
            if (auto m = ClientToViewportModel(clientX, clientY))
                w = *m;
        }

        marqueeEndWorld = w;
    }
    else
    {
        marqueeEndWorld = ClientToWorld(marqueeEndClient);
    }
}

void Application::ClearSelection()
{
    // Selection changes can happen while commands temporarily disable selection mutation
    // (e.g. while an interactive command is running). We still want marquee selection to
    // be robust, so locally allow mutation for this operation.
    SelectionMutationScope selMut(*this);

    if (!allowSelectionMutation)
        return;

    if (selectedIndices.empty())
        return;

    auto& ents = entityBook.GetEntitiesMutable();
    for (std::size_t idx : selectedIndices)
    {
        if (idx < ents.size())
            ents[idx].selected = false;
    }

    selectedIndices.clear();
    selectedIndex = -1;

    entityBook.Touch("ClearSelection");
    DebugSelectionf("ClearSelection: now=0");
}

// Clear selection and any grip/hover state that depends on it.
// Used when starting certain commands so we begin from a clean slate.
void Application::CancelSelectionAndGrips()
{
    // Cancel marquee selection flow.
    marqueeActive = false;
    marqueeAwaitSecondClick = false;
    marqueeMode = MarqueeMode::AutoByDrag;
    // glm::ivec2 doesn't support brace-assignment on MSVC; use constructor.
    marqueeStartClient = glm::ivec2(0, 0);
    marqueeEndClient   = glm::ivec2(0, 0);

    // Clear selection and any derived render caches.
    ClearSelection();

    // Clear grip + hover.
    ClearGrip();
    hoveredIndex.reset();
}

void Application::ApplySelection(const std::vector<std::size_t>& indices)
{
    SelectionMutationScope selMut(*this);

    if (!allowSelectionMutation)
        return;

    ClearSelection();

    if (indices.empty())
        return;

    auto& ents = entityBook.GetEntitiesMutable();

    selectedIndices.reserve(indices.size());
    for (std::size_t idx : indices)
    {
        if (idx >= ents.size())
            continue;

        Entity& e = ents[idx];
        if (e.tag != EntityTag::Scene && e.tag != EntityTag::User && e.tag != EntityTag::PaperUser)
            continue;
        if (!layerTable.IsLayerSelectable(e.layerId))
            continue;

        if (!e.selected)
        {
            e.selected = true;
            selectedIndices.push_back(idx);
        }
    }

    if (!selectedIndices.empty())
        selectedIndex = (int)selectedIndices[0];

    entityBook.Touch("ApplySelection");

    // Log selected entity ids (cap to keep log readable).
    if (VKLog::Enabled(VKLog::Command, VKLog::Level::Info))
    {
        constexpr std::size_t kMaxIdsToPrint = 64;
        std::string s;
        s.reserve(1024);

        const std::size_t n = selectedIndices.size();
        s += "Selected entity ids (" + std::to_string(n) + "):";

        const std::size_t printN = (n < kMaxIdsToPrint) ? n : kMaxIdsToPrint;
        for (std::size_t i = 0; i < printN; ++i)
        {
            const std::size_t selIdx = selectedIndices[i];
            if (selIdx >= ents.size())
                continue;
            s += (i == 0 ? " " : ", ");
            s += std::to_string(ents[selIdx].ID);
        }
        if (n > printN)
            s += " ...";

        VKLog::Logf(VKLog::Command, VKLog::Level::Info, "%s", s.c_str());
    }



// --- Selection pipeline verification (NO hover spam) ---
if (VKLog::Enabled(VKLog::Selection, VKLog::Level::Info))
{
    size_t verifyFlags = 0;
    size_t selLine = 0, selText = 0, selRect = 0;
    size_t selScene = 0, selUser = 0, selPaperUser = 0;

    const size_t sampleN = std::min<size_t>(selectedIndices.size(), 8);
    std::string sample;
    for (size_t i = 0; i < sampleN; ++i)
    {
        const size_t idx = selectedIndices[i];
        if (idx >= ents.size()) continue;
        if (i) sample += ",";
        sample += std::to_string(ents[idx].ID);
    }

    for (size_t idx : selectedIndices)
    {
        if (idx >= ents.size()) continue;
        const Entity& e = ents[idx];
        if (e.selected) ++verifyFlags;

        switch (e.type)
        {
        case EntityType::Line:      ++selLine; break;
        case EntityType::Text:      ++selText; break;
        case EntityType::SolidRect: ++selRect; break;
        default: break;
        }

        switch (e.tag)
        {
        case EntityTag::Scene:     ++selScene; break;
        case EntityTag::User:      ++selUser; break;
        case EntityTag::PaperUser: ++selPaperUser; break;
        default: break;
        }
    }

    VKLog::Logf(VKLog::Selection, VKLog::Level::Info,
        "[SEL][ApplySelection] indices=%zu flagsSet=%zu types(L=%zu T=%zu R=%zu) tags(Scene=%zu User=%zu PaperUser=%zu) sampleIds=%s",
        selectedIndices.size(), verifyFlags,
        selLine, selText, selRect,
        selScene, selUser, selPaperUser,
        sample.c_str());
}

    DebugSelectionf("ApplySelection: now=%zu", selectedIndices.size());
}

void Application::AddToSelection(std::size_t idx)
{
    SelectionMutationScope selMut(*this);

    if (!allowSelectionMutation)
        return;

    auto& ents = entityBook.GetEntitiesMutable();
    if (idx >= ents.size())
        return;

    Entity& e = ents[idx];
    if (e.tag != EntityTag::Scene && e.tag != EntityTag::User && e.tag != EntityTag::PaperUser)
        return;
    if (!layerTable.IsLayerSelectable(e.layerId))
        return;

    if (e.selected)
        return;

    e.selected = true;
    selectedIndices.push_back(idx);
    if (selectedIndex < 0)
        selectedIndex = (int)idx;

    entityBook.Touch("ApplySelection");
    DebugSelectionf("AddToSelection: now=%zu", selectedIndices.size());
}

bool Application::IsIndexSelected(std::size_t idx) const
{
    if ((int)idx == selectedIndex)
        return true;
    return std::find(selectedIndices.begin(), selectedIndices.end(), idx) != selectedIndices.end();
}

void Application::EraseSelected()
{
    // Allow selection mutations within explicit command actions.
    SelectionMutationScope selMut(*this);

    if (selectedIndices.empty())
        return;

    auto& ents = entityBook.GetEntitiesMutable();

    // Collect stable entity ids to erase.
    std::vector<uint32_t> ids;
    std::vector<uint32_t> sceneIds;
    ids.reserve(selectedIndices.size());
    sceneIds.reserve(selectedIndices.size());
    for (std::size_t idx : selectedIndices)
    {
        if (idx >= ents.size())
            continue;
        const Entity& e = ents[idx];
        // Erase supports model-space user entities, paper-space user entities, and scene geometry.
        if (e.tag != EntityTag::Scene && e.tag != EntityTag::User && e.tag != EntityTag::PaperUser)
            continue;
        if (!layerTable.IsLayerSelectable(e.layerId))
            continue;
        const uint32_t id = static_cast<uint32_t>(e.ID);
        ids.push_back(id);
        if (e.tag == EntityTag::Scene)
            sceneIds.push_back(id);
    }

    if (ids.empty())
        return;

    // Detailed erase audit (selection + entities)
    if (VKLog::Enabled(VKLog::Erase, VKLog::Level::Debug))
    {
        VKLog::Logf(VKLog::Erase, VKLog::Level::Debug, "EraseSelected: selectedCount=%zu", selectedIndices.size());
        for (std::size_t idx : selectedIndices)
        {
            if (idx >= ents.size())
                continue;
            const Entity& e = ents[idx];
            VKLog::LogEntity(VKLog::Erase, VKLog::Level::Debug, "EraseCandidate", e);
        }
    }

    // Restore any temporary selection coloring before we mutate the entity list.
    ClearSelection();

    // Persist scene removals (dragon segments) and remove erased entities.
    std::unordered_set<uint32_t> eraseIds;
    eraseIds.reserve(ids.size());
    for (uint32_t id : ids)
        eraseIds.insert(id);

    // Scene entities are persisted in the erase set so they stay removed across rebuilds.
    for (uint32_t id : sceneIds)
        erasedDragonIds.insert(id);

    entityBook.RemoveIf([&](const Entity& e)
        {
            const uint32_t id = static_cast<uint32_t>(e.ID);
            if (eraseIds.find(id) == eraseIds.end())
                return false;
            return (e.tag == EntityTag::Scene) || (e.tag == EntityTag::User) || (e.tag == EntityTag::PaperUser);
        });

    // Rebuild picking for the new entity layout.
    ClearHover();
    ClearGrip();
    dirtyPickTree = true;

    VKLog::Logf(VKLog::Erase, VKLog::Level::Info, "ERASE removed %zu entity(s)", ids.size());
}


void Application::SelectAllSelectableEntities()
{
    // Allow selection mutations within explicit command actions.
    SelectionMutationScope selMut(*this);

    std::vector<std::size_t> hits;
    const auto& ents = entityBook.GetEntities();
    hits.reserve(ents.size());

    for (std::size_t i = 0; i < ents.size(); ++i)
    {
        const Entity& e = ents[i];
        if (e.tag != EntityTag::Scene && e.tag != EntityTag::User && e.tag != EntityTag::PaperUser)
            continue;
        if (!layerTable.IsLayerSelectable(e.layerId))
            continue;
        if (e.type != EntityType::Line)
            continue;
        hits.push_back(i);
    }

    ApplySelection(hits);
}

void Application::BeginEraseCommand(CmdWindow* cmdWnd)
{
    // Allow selection mutations within explicit command actions.
    SelectionMutationScope selMut(*this);

    pendingCommand = PendingCommand::Erase;
    marqueeMode = MarqueeMode::AutoByDrag;

    VKLog::Logf(VKLog::Command, VKLog::Level::Info, "BeginCommand ERASE");

    // Ensure selection mode is ON (ERASE is a selection-driven command).
    selectionMode = true;

    // Clear transient state + any prior selection.
    CancelSelectionAndGrips();

    if (cmdWnd)
    {
        cmdWnd->Show(true);
        cmdWnd->AppendText(L"Select entities: (W)indow / (C)rossing / ALL  (Enter = finish, ESC/CANCEL = abort)\r\n");
    }

    MarkAllDirty();
}

// ------------------------------------------------------------
// LINE command (interactive)
// ------------------------------------------------------------
static bool TryParseFloats(const std::string& s, std::vector<double>& out)
{
    out.clear();
    std::string t = s;
    // allow commas as separators
    std::replace(t.begin(), t.end(), ',', ' ');
    std::istringstream iss(t);
    double v = 0.0;
    while (iss >> v)
        out.push_back(v);
    return !out.empty();
}

static bool TryParsePoint(const std::string& s, glm::vec3& out)
{
    std::vector<double> vals;
    if (!TryParseFloats(s, vals))
        return false;
    if (vals.size() == 2)
    {
        out = glm::vec3((float)vals[0], (float)vals[1], 0.0f);
        return true;
    }
    if (vals.size() == 3)
    {
        out = glm::vec3((float)vals[0], (float)vals[1], (float)vals[2]);
        return true;
    }
    return false;
}

static bool TryParseBearingDistance(const std::string& s, float& outBearingDeg, float& outDistance)
{
    std::vector<double> vals;
    if (!TryParseFloats(s, vals))
        return false;
    if (vals.size() == 2)
    {
        outBearingDeg = (float)vals[0];
        outDistance = (float)vals[1];
        return true;
    }
    return false;
}

static glm::vec3 OrthoProjectFromCursor(const glm::vec3& origin, const glm::vec3& cursor, float distanceOverride)
{
    const glm::vec2 d(cursor.x - origin.x, cursor.y - origin.y);
    const bool xMajor = std::abs(d.x) >= std::abs(d.y);

    glm::vec3 out = origin;
    if (xMajor)
    {
        const float sign = (d.x >= 0.0f) ? 1.0f : -1.0f;
        const float dist = (distanceOverride >= 0.0f) ? distanceOverride : std::abs(d.x);
        out.x += sign * dist;
    }
    else
    {
        const float sign = (d.y >= 0.0f) ? 1.0f : -1.0f;
        const float dist = (distanceOverride >= 0.0f) ? distanceOverride : std::abs(d.y);
        out.y += sign * dist;
    }
    return out;
}

static glm::vec3 ApplyOneShotSnap(Application::SnapMode snap, const glm::vec3& p)
{
    // NOTE: This is intentionally minimal scaffolding.
    // For now we implement GRID snap only; other snaps are placeholders.
    if (snap == Application::SnapMode::Grid)
    {
        const float step = 25.0f; // matches current world grid minorStep
        auto snap1 = [&](float v) -> float { return std::round(v / step) * step; };
        return glm::vec3(snap1(p.x), snap1(p.y), p.z);
    }
    return p;
}

void Application::BeginLineCommand(CmdWindow* cmdWnd)
{
    pendingCommand = PendingCommand::Line;
    lineStage = LineStage::FirstNode;
    lineHasFirst = false;
    crosshairsMode = CrosshairsMode::PointEntryCrosshairs;
    lineCmdWnd = cmdWnd;

    // LINE is a point-entry command; keep selection mode off.
    selectionMode = false;
    CancelSelectionAndGrips();

    // Hide jig until first point is picked.
    if (auto* jig = FindEntityById(entityBook, cursorLineJigId))
    {
        jig->line.start = glm::vec3(0, 0, 0);
        jig->line.end = glm::vec3(0, 0, 0);
    }

    if (cmdWnd)
    {
        cmdWnd->Show(true);
        cmdWnd->AppendText(L"LINE\r\n");
        cmdWnd->AppendText(L"first node: ");
    }

    VKLog::Logf(VKLog::Command, VKLog::Level::Info, "BeginCommand LINE");
}

bool Application::HandleCmdWindowLine(const std::string& line, CmdWindow* cmdWnd)
{
    if (pendingCommand == PendingCommand::None)
        return false;

    const std::string s = TrimCopy(line);
    const std::string u = ToUpperCopyA(s);

    if (pendingCommand == PendingCommand::Erase)
    {
        // Allow selection mutations within explicit command actions.
    SelectionMutationScope selMut(*this);

        if (u == "W")
        {
            marqueeMode = MarqueeMode::Window;
            VKLog::Logf(VKLog::Command, VKLog::Level::Info, "ERASE option: Window (W)");
            if (cmdWnd) cmdWnd->AppendText(L"Window selection: drag a rectangle in the viewport.\r\n");
            return true;
        }
        if (u == "C")
        {
            marqueeMode = MarqueeMode::Crossing;
            VKLog::Logf(VKLog::Command, VKLog::Level::Info, "ERASE option: Crossing (C)");
            if (cmdWnd) cmdWnd->AppendText(L"Crossing selection: drag a rectangle in the viewport.\r\n");
            return true;
        }
        if (u == "ALL")
        {
            VKLog::Logf(VKLog::Command, VKLog::Level::Info, "ERASE option: ALL");
            SelectAllSelectableEntities();
            const size_t count = selectedIndices.size();
            EraseSelected();

            // Command finished: clear selection/grips/hover artifacts.
            CancelSelectionAndGrips();

            pendingCommand = PendingCommand::None;
            marqueeMode = MarqueeMode::AutoByDrag;
            selectionMode = false;

            if (cmdWnd)
            {
                std::wstring msg = L"ERASE: removed ";
                msg += std::to_wstring(count);
                msg += L" entity(s).\r\n";
                cmdWnd->AppendText(msg);
                // Return to the base prompt now that the command is complete.
                cmdWnd->ShowBasePromptIfIdle();
            }
            return true;
        }
        if (s.empty())
        {
            const size_t count = selectedIndices.size();
            VKLog::Logf(VKLog::Command, VKLog::Level::Info, "ERASE finish (Enter) selectedCount=%zu", count);
            EraseSelected();

            // Command finished: clear selection/grips/hover artifacts.
            CancelSelectionAndGrips();

            pendingCommand = PendingCommand::None;
            marqueeMode = MarqueeMode::AutoByDrag;
            selectionMode = false;

            if (cmdWnd)
            {
                std::wstring msg = L"ERASE: removed ";
                msg += std::to_wstring(count);
                msg += L" entity(s).\r\n";
                cmdWnd->AppendText(msg);
                // Return to the base prompt now that the command is complete.
                cmdWnd->ShowBasePromptIfIdle();
            }
            return true;
        }

        if (u == "CANCEL" || u == "ESC" || u == "ESCAPE")
        {
            VKLog::Logf(VKLog::Command, VKLog::Level::Info, "ERASE canceled");
            CancelSelectionAndGrips();
            pendingCommand = PendingCommand::None;
            marqueeMode = MarqueeMode::AutoByDrag;
            selectionMode = false;

            if (cmdWnd)
            {
                cmdWnd->AppendText(L"*Cancel*\r\n");
                cmdWnd->ShowBasePromptIfIdle();
            }
            return true;
        }

        if (cmdWnd)
        {
            cmdWnd->AppendText(L"Invalid option. Use W, C, ALL, Enter, or CANCEL.\r\n");
            cmdWnd->AppendText(L"Select entities: (W)indow / (C)rossing / ALL  (Enter = finish, ESC/CANCEL = abort)\r\n");
        }
        return true;
    }

    if (pendingCommand == PendingCommand::Line)
    {
        // Common cancel tokens
        if (u == "CANCEL" || u == "ESC" || u == "ESCAPE")
        {
            VKLog::Logf(VKLog::Command, VKLog::Level::Info, "LINE canceled");
            pendingCommand = PendingCommand::None;
            lineStage = LineStage::None;
            lineHasFirst = false;
            crosshairsMode = CrosshairsMode::GripsSelectionCrosshairs;
            oneShotSnap = SnapMode::None;
            lineCmdWnd = nullptr;
                if (cmdWnd) cmdWnd->AppendText(L"*Cancel*\r\n");
            return true;
        }

        if (lineStage == LineStage::FirstNode)
        {
            if (s.empty())
            {
                if (cmdWnd) cmdWnd->AppendText(L"first node: ");
                return true;
            }

            glm::vec3 p;
            if (!TryParsePoint(s, p))
            {
                if (cmdWnd) cmdWnd->AppendText(L"Enter X,Y or X,Y,Z (or left-click a point). Type CANCEL to abort.\r\nfirst node: ");
                return true;
            }

            lineFirstNode = ApplyOneShotSnap(oneShotSnap, p);
            oneShotSnap = SnapMode::None;
            lineHasFirst = true;
            lineStage = LineStage::SecondNode;

            VKLog::Logf(VKLog::Command, VKLog::Level::Info,
                "LINE first node=(%.3f,%.3f,%.3f)",
                lineFirstNode.x, lineFirstNode.y, lineFirstNode.z);

            if (cmdWnd) cmdWnd->AppendText(L"second node: ");
            return true;
        }

        if (lineStage == LineStage::SecondNode)
        {
            if (s.empty())
            {
                // Can't finish without a second point.
                if (cmdWnd) cmdWnd->AppendText(L"*Cancel*\r\n");
                pendingCommand = PendingCommand::None;
                lineStage = LineStage::None;
                lineHasFirst = false;
                crosshairsMode = CrosshairsMode::GripsSelectionCrosshairs;
                oneShotSnap = SnapMode::None;
                lineCmdWnd = nullptr;
                return true;
            }

            glm::vec3 p;
            float bearing = 0.0f, dist = 0.0f;

            bool ok = false;
            if (TryParsePoint(s, p))
            {
                p = ApplyOneShotSnap(oneShotSnap, p);
                oneShotSnap = SnapMode::None;
                ok = true;
            }
            else if (orthoEnabled)
            {
                // Ortho distance-only entry
                std::vector<double> vals;
                if (TryParseFloats(s, vals) && vals.size() == 1)
                {
                    const float d = (float)vals[0];
                    glm::vec3 cursor(mouseWorld.x, mouseWorld.y, 0.0f);
                    cursor = ApplyOneShotSnap(oneShotSnap, cursor);
                    p = OrthoProjectFromCursor(lineFirstNode, cursor, std::abs(d));
                    oneShotSnap = SnapMode::None;
                    ok = true;
                }
            }

            if (!ok && TryParseBearingDistance(s, bearing, dist))
            {
                const float rad = glm::radians(bearing);
                p = lineFirstNode + glm::vec3(std::cos(rad) * dist, std::sin(rad) * dist, 0.0f);
                ok = true;
            }

            if (!ok)
            {
                if (cmdWnd)
                {
                    cmdWnd->AppendText(L"Enter X,Y[,Z] or Bearing Distance. If Ortho is ON (F8), you may enter Distance only.\r\nsecond node: ");
                }
                return true;
            }

            // Commit line
            Entity e;
            e.ID = nextId++;
            e.tag = (IsPaperSpace() && !HasActiveViewport()) ? EntityTag::Paper : EntityTag::User;
            e.type = EntityType::Line;
            e.drawOrder = 0;
            e.screenSpace = false;
            e.layerId = layerTable.CurrentLayerId();
            e.colorByLayer = true;
            e.linetypeByLayer = true;
            e.line.start = lineFirstNode;
            e.line.end = p;
            if (e.tag == EntityTag::PaperUser)
            {
                e.colorByLayer = false;
                e.line.color = glm::vec4(0, 0, 0, 1);
            }
            else
            {
                e.line.color = glm::vec4(1, 1, 1, 1);
            }
            e.line.thickness = 1.5f;

            entityBook.AddEntity(e);
            entityBook.SortByDrawOrder();
            dirtyPickTree = true;
            dirtyScene = true;

            VKLog::LogEntity(VKLog::Command, VKLog::Level::Info, "LINE commit", e);

            // Reset state
            pendingCommand = PendingCommand::None;
            lineStage = LineStage::None;
            lineHasFirst = false;
            crosshairsMode = CrosshairsMode::GripsSelectionCrosshairs;
            oneShotSnap = SnapMode::None;
            lineCmdWnd = nullptr;

            if (cmdWnd) cmdWnd->AppendText(L"\r\n");
            return true;
        }
    }

    return false;
}


void Application::BeginMarquee()
{
    marqueeActive = true;
    marqueeAwaitSecondClick = (marqueeMode != MarqueeMode::AutoByDrag);

    marqueeStartClient = mouseClient;
    marqueeEndClient = mouseClient;

    marqueeStartWorld = mouseWorld;
    marqueeEndWorld = mouseWorld;
}

void Application::FinishMarqueeSelect()
{
    // Allow selection changes while we apply marquee results.
    // Marquee completion can occur from paths that run before OnLeftDown's selection-mutation scope.
    SelectionMutationScope selMut(*this);

    // Determine drag direction (right = bounds/inside, left = crossing).
    const int dx = marqueeEndClient.x - marqueeStartClient.x;
    const int dy = marqueeEndClient.y - marqueeStartClient.y;

    // Tiny drags just cancel.
    if (std::abs(dx) < 2 && std::abs(dy) < 2)
    {
        marqueeActive = false;
        return;
    }

    bool crossing = false;
    if (marqueeMode == MarqueeMode::AutoByDrag)
        crossing = (dx < 0); // drag-left = crossing
    else
        crossing = (marqueeMode == MarqueeMode::Crossing);
    const glm::vec2 a = marqueeStartWorld;
    const glm::vec2 b = marqueeEndWorld;

    // Command-level logging (visible in default Debug Console verbosity).
    VKLog::Logf(VKLog::Command, VKLog::Level::Info,
        "MarqueeCorners mode=%s crossing=%d A=(%.3f,%.3f) B=(%.3f,%.3f)",
        (marqueeMode == MarqueeMode::AutoByDrag ? "Auto" : (marqueeMode == MarqueeMode::Crossing ? "Crossing" : "Window")),
        crossing ? 1 : 0,
        a.x, a.y, b.x, b.y);



    // Selection audit (useful for ERASE + debugging pick/selection mismatches)
    if (VKLog::Enabled(VKLog::Selection, VKLog::Level::Info))
    {
        const bool pickModelThroughViewport = IsViewportInteractionActive();
        const bool pickPaperEntities = IsPaperSpace() && !pickModelThroughViewport;
        VKLog::Logf(VKLog::Selection, VKLog::Level::Info,
            "MarqueeFinish mode=%s crossing=%d clientA=(%d,%d) clientB=(%d,%d) worldA=(%.3f,%.3f) worldB=(%.3f,%.3f) context=%s",
            (marqueeMode == MarqueeMode::AutoByDrag ? "Auto" : (marqueeMode == MarqueeMode::Crossing ? "Crossing" : "Window")),
            crossing ? 1 : 0,
            marqueeStartClient.x, marqueeStartClient.y, marqueeEndClient.x, marqueeEndClient.y,
            a.x, a.y, b.x, b.y,
            pickPaperEntities ? "Paper" : (pickModelThroughViewport ? "ViewportModel" : "Model"));
    }

    const float minX = std::min(a.x, b.x);
    const float maxX = std::max(a.x, b.x);
    const float minY = std::min(a.y, b.y);
    const float maxY = std::max(a.y, b.y);

    std::vector<std::size_t> hits;
    const auto& ents = entityBook.GetEntities();

    const bool pickModelThroughViewport = IsViewportInteractionActive();
    const bool pickPaperEntities = IsPaperSpace() && !pickModelThroughViewport;

    auto tagSelectableInThisContext = [&](EntityTag tag) -> bool
        {
            // Keep marquee selection consistent with picking/editing context.
            // NOTE: ERASE ultimately filters by tag (Scene/User/PaperUser) before deleting.
            if (pickPaperEntities)
                return (tag == EntityTag::PaperUser) || (tag == EntityTag::User);
            return (tag == EntityTag::Scene) || (tag == EntityTag::User);
        };

    for (std::size_t i = 0; i < ents.size(); ++i)
    {
        const Entity& e = ents[i];
        if (!tagSelectableInThisContext(e.tag))
            continue;
        if (!layerTable.IsLayerSelectable(e.layerId))
            continue;
        if (e.type != EntityType::Line)
            continue;

        const glm::vec3 p0 = e.line.start;
        const glm::vec3 p1 = e.line.end;

        const float eMinX = std::min(p0.x, p1.x);
        const float eMaxX = std::max(p0.x, p1.x);
        const float eMinY = std::min(p0.y, p1.y);
        const float eMaxY = std::max(p0.y, p1.y);

        if (crossing)
        {
            const bool intersects =
                !(eMaxX < minX || eMinX > maxX || eMaxY < minY || eMinY > maxY);

            if (intersects)
                hits.push_back(i);
        }
        else
        {
            const bool inside =
                (eMinX >= minX && eMaxX <= maxX && eMinY >= minY && eMaxY <= maxY);

            if (inside)
                hits.push_back(i);
        }
    }
    ApplySelection(hits);

    VKLog::Logf(VKLog::Command, VKLog::Level::Info,
        "MarqueeApplySelection hits=%zu selectedCountNow=%zu",
        hits.size(), selectedIndices.size());

    if (VKLog::Enabled(VKLog::Selection, VKLog::Level::Info))
    {
        const size_t kMaxPrint = 64;
        std::ostringstream oss;
        oss << "Selected entity IDs (" << selectedIndices.size() << "):";
        const size_t n = (selectedIndices.size() < kMaxPrint) ? selectedIndices.size() : kMaxPrint;
        for (size_t k = 0; k < n; ++k)
        {
            const size_t idx = selectedIndices[k];
            if (idx < entityBook.entities.size())
                oss << " " << entityBook.entities[idx].ID;
            else
                oss << " ?";
        }
        if (selectedIndices.size() > kMaxPrint)
            oss << " ...";
        VKLog::Logf(VKLog::Selection, VKLog::Level::Info, "%s", oss.str().c_str());
    }
    if (VKLog::Enabled(VKLog::Selection, VKLog::Level::Info))
        VKLog::Logf(VKLog::Selection, VKLog::Level::Info, "MarqueeHits count=%zu", hits.size());

    marqueeActive = false;
    marqueeAwaitSecondClick = false;
}

void Application::UpdateMarqueeOverlay()
{
    // Hide marquee when not active by collapsing it to cursor center.
    if (!marqueeActive)
    {
        const float cx = static_cast<float>(mouseClient.x);
        const float cy = static_cast<float>(clientHeight - 1 - mouseClient.y);
        const glm::vec3 c(cx, cy, 0.0f);

        for (int i = 0; i < 4; ++i)
        {
            if (auto* l = FindEntityById(entityBook, marqueeBoxId[i]))
            {
                l->screenSpace = true;
                l->line.start = c;
                l->line.end = c;
            }
        }
        return;
    }

    // Use client coords, but convert to renderer's Y-up screen space.
    const float sx = static_cast<float>(marqueeStartClient.x);
    const float sy = static_cast<float>(clientHeight - 1 - marqueeStartClient.y);
    const float ex = static_cast<float>(marqueeEndClient.x);
    const float ey = static_cast<float>(clientHeight - 1 - marqueeEndClient.y);

    const float x0 = std::min(sx, ex);
    const float x1 = std::max(sx, ex);
    const float y0 = std::min(sy, ey);
    const float y1 = std::max(sy, ey);

    const glm::vec3 r00(x0, y0, 0.0f);
    const glm::vec3 r10(x1, y0, 0.0f);
    const glm::vec3 r11(x1, y1, 0.0f);
    const glm::vec3 r01(x0, y1, 0.0f);

    if (auto* b0 = FindEntityById(entityBook, marqueeBoxId[0])) { b0->screenSpace = true; b0->line.start = r00; b0->line.end = r10; }
    if (auto* b1 = FindEntityById(entityBook, marqueeBoxId[1])) { b1->screenSpace = true; b1->line.start = r10; b1->line.end = r11; }
    if (auto* b2 = FindEntityById(entityBook, marqueeBoxId[2])) { b2->screenSpace = true; b2->line.start = r11; b2->line.end = r01; }
    if (auto* b3 = FindEntityById(entityBook, marqueeBoxId[3])) { b3->screenSpace = true; b3->line.start = r01; b3->line.end = r00; }
}


void Application::UpdateViewportCreateOverlay()
{
    // Hide jig when not actively picking a viewport rectangle.
    if (!IsPaperSpace() || !viewportCreateArmed || !viewportCreateHasFirst)
    {
        const float cx = static_cast<float>(mouseClient.x);
        const float cy = static_cast<float>(clientHeight - 1 - mouseClient.y);
        const glm::vec3 c(cx, cy, 0.0f);

        for (int i = 0; i < 4; ++i)
        {
            if (auto* l = FindEntityById(entityBook, viewportCreateBoxId[i]))
            {
                l->screenSpace = true;
                l->line.start = c;
                l->line.end = c;
            }
        }
        return;
    }

    // Convert paper-space corners (inches, Y-up world) to client pixels (top-left origin),
    // then to renderer screen-space (Y-up bottom-left origin).
    const glm::vec2 p0c = WorldToClient(viewportFirstCornerIn);
    const glm::vec2 p1c = WorldToClient(viewportCurrentCornerIn);

    const float sx = static_cast<float>(p0c.x);
    const float sy = static_cast<float>(clientHeight - 1 - p0c.y);
    const float ex = static_cast<float>(p1c.x);
    const float ey = static_cast<float>(clientHeight - 1 - p1c.y);

    const float x0 = std::min(sx, ex);
    const float x1 = std::max(sx, ex);
    const float y0 = std::min(sy, ey);
    const float y1 = std::max(sy, ey);

    const glm::vec3 r00(x0, y0, 0.0f);
    const glm::vec3 r10(x1, y0, 0.0f);
    const glm::vec3 r11(x1, y1, 0.0f);
    const glm::vec3 r01(x0, y1, 0.0f);

    const glm::vec4 col = glm::vec4(0, 0, 0, 1); // black jig outline in paper space

    if (auto* b0 = FindEntityById(entityBook, viewportCreateBoxId[0])) { b0->screenSpace = true; b0->line.start = r00; b0->line.end = r10; b0->line.color = col; }
    if (auto* b1 = FindEntityById(entityBook, viewportCreateBoxId[1])) { b1->screenSpace = true; b1->line.start = r10; b1->line.end = r11; b1->line.color = col; }
    if (auto* b2 = FindEntityById(entityBook, viewportCreateBoxId[2])) { b2->screenSpace = true; b2->line.start = r11; b2->line.end = r01; b2->line.color = col; }
    if (auto* b3 = FindEntityById(entityBook, viewportCreateBoxId[3])) { b3->screenSpace = true; b3->line.start = r01; b3->line.end = r00; b3->line.color = col; }
}


void Application::EnsurePickTree()
{
    if (!dirtyPickTree)
        return;

    BuildPickTree();
    dirtyPickTree = false;
}

void Application::BuildPickTree()
{
    pickTree.Clear();

    const auto& ents = entityBook.GetEntities();
    std::vector<std::pair<BoundingBox, std::size_t>> items;
    items.reserve(ents.size());

    // Small pad so thin lines are still hittable. Uses selection box size.
float pxPerWorldX = 1.0f, pxPerWorldY = 1.0f;
GetInteractionPixelsPerWorld(pxPerWorldX, pxPerWorldY);
const float padX = (0.5f * static_cast<float>(SELECTION_BOX_SIZE_PX)) / std::max(0.0001f, pxPerWorldX);
const float padY = (0.5f * static_cast<float>(SELECTION_BOX_SIZE_PX)) / std::max(0.0001f, pxPerWorldY);

const bool pickModelThroughViewport = IsViewportInteractionActive();
const bool pickPaperEntities = IsPaperSpace() && !pickModelThroughViewport;

for (std::size_t i = 0; i < ents.size(); ++i)
{
    const Entity& e = ents[i];

    // In paper space we normally pick paper entities. When an active viewport is engaged
    // (cursor inside it), pick model-space entities instead (edit-through-viewport).
    if (pickPaperEntities)
    {
        // Paper picking includes paper annotations.
        if (e.tag != EntityTag::Paper && e.tag != EntityTag::User && e.tag != EntityTag::PaperUser)
            continue;
    }
    else
    {
        if (e.tag != EntityTag::Scene && e.tag != EntityTag::User)
            continue;
    }
if (!layerTable.IsLayerSelectable(e.layerId))
            continue;

        if (e.type != EntityType::Line)
            continue;

        const glm::vec3 a = e.line.start;
        const glm::vec3 b = e.line.end;

        const float minX = std::min(a.x, b.x) - padX;
        const float minY = std::min(a.y, b.y) - padY;
        const float maxX = std::max(a.x, b.x) + padX;
        const float maxY = std::max(a.y, b.y) + padY;

        items.emplace_back(BoundingBox(minX, minY, -1.0f, maxX, maxY, 1.0f), i);
    }

    if (!items.empty())
        pickTree.Build(items);
}

void Application::ClearHover()
{
    if (!hoveredIndex.has_value())
        return;

    const std::size_t idx = *hoveredIndex;

    auto& ents = entityBook.GetEntitiesMutable();
    if (idx < ents.size() && ents[idx].type == EntityType::Line)
    {
        // We never apply hover highlight to selected entities, so it's safe to always restore.
        ents[idx].colorByLayer = hoveredPrevColorByLayer;
        ents[idx].line.color = hoveredPrevColor;
    }

    hoveredIndex.reset();

    // Hover affects render output.
    entityBook.Touch("Hover");
}

void Application::UpdateHover()
{
    EnsurePickTree();

    // Clear previous hover (restores color).
    ClearHover();

    // Query a small box around mouse, sized from SELECTION_BOX_SIZE_PX.
    float pxPerWorldX = 1.0f, pxPerWorldY = 1.0f;
    GetInteractionPixelsPerWorld(pxPerWorldX, pxPerWorldY);

    const float halfSizeX = (0.5f * static_cast<float>(SELECTION_BOX_SIZE_PX)) / std::max(0.0001f, pxPerWorldX);
    const float halfSizeY = (0.5f * static_cast<float>(SELECTION_BOX_SIZE_PX)) / std::max(0.0001f, pxPerWorldY);

    const BoundingBox box(
        mouseWorld.x - halfSizeX, mouseWorld.y - halfSizeY, -1.0f,
        mouseWorld.x + halfSizeX, mouseWorld.y + halfSizeY, 1.0f);

    const auto hit = pickTree.QueryFirstIntersect(box);
    if (!hit.has_value())
        return;

    const std::size_t idx = *hit;

    auto& ents = entityBook.GetEntitiesMutable();
    if (idx >= ents.size())
        return;

    Entity& e = ents[idx];

    // Don't hover-highlight something that's already selected.
    if (e.selected)
        return;

    if (e.type != EntityType::Line)
        return;

    hoveredPrevColor = e.line.color;
    hoveredPrevColorByLayer = e.colorByLayer;

    // Force explicit color so hover highlight is visible even when entity is ByLayer.
    e.colorByLayer = false;
    e.line.color = glm::vec4(1, 1, 0, 1); // hover highlight (yellow)

    hoveredIndex = idx;

    // Hover affects render output.
    entityBook.Touch("Hover");
}

// ------------------------------------------------------------
// Cursor entities (screen space)
// ------------------------------------------------------------
void Application::EnsureCursorEntities()
{
    if (cursorEntitiesValid)
        return;

    const glm::vec4 white(1, 1, 1, 1);
    const int order = 900;

    // Crosshair: 6 lines (we only use 2, but keep array stable)
    for (int i = 0; i < 6; ++i)
    {
        cursorCrossId[i] = nextId++;
        entityBook.AddEntity(MakeLine(cursorCrossId[i], EntityTag::Cursor, order,
            glm::vec3(0, 0, 0), glm::vec3(0, 0, 0),
            white, 1.0f, true));
    }

    // Box: 4 lines (always drawn; used as selection box OR crosshair center box)
    for (int i = 0; i < 4; ++i)
    {
        cursorBoxId[i] = nextId++;
        entityBook.AddEntity(MakeLine(cursorBoxId[i], EntityTag::Cursor, order,
            glm::vec3(0, 0, 0), glm::vec3(0, 0, 0),
            white, 1.0f, true));
    }

    // Marquee selection rectangle: 4 lines (only shown while dragging)
    for (int i = 0; i < 4; ++i)
    {
        marqueeBoxId[i] = nextId++;
        entityBook.AddEntity(MakeLine(marqueeBoxId[i], EntityTag::Cursor, order,
            glm::vec3(0, 0, 0), glm::vec3(0, 0, 0),
            white, 1.0f, true));
    }

    // Viewport-create jig rectangle: 4 lines (shown while VIEWPORT is picking corners)
    for (int i = 0; i < 4; ++i)
    {
        viewportCreateBoxId[i] = nextId++;
        entityBook.AddEntity(MakeLine(viewportCreateBoxId[i], EntityTag::Cursor, order,
            glm::vec3(0, 0, 0), glm::vec3(0, 0, 0),
            white, 1.0f, true));
    }

    // LINE jig (world-space, temporary)
    cursorLineJigId = nextId++;
    entityBook.AddEntity(MakeLine(cursorLineJigId, EntityTag::Cursor, order,
        glm::vec3(0, 0, 0), glm::vec3(0, 0, 0),
        white, 1.0f, false));

    cursorEntitiesValid = true;

    // Reorders vector => pickTree indices become invalid.
    entityBook.SortByDrawOrder();
    dirtyPickTree = true;
}

void Application::UpdateCursorEntities()
{
    // Cursor overlay is expressed in client-space pixels.
    // Our renderer’s screen-space path expects Y-up (origin bottom-left),
    // while Win32 mouse coords are Y-down (origin top-left), so flip Y here.
    const float cx = static_cast<float>(mouseClient.x);
    const float cy = static_cast<float>(clientHeight - 1 - mouseClient.y);

    // A small box centered at the cursor. Size is defined in client pixels.
    const float boxHalf = 0.5f * static_cast<float>(SELECTION_BOX_SIZE_PX);

    // Default cursor/crosshair color by space; if we are in an active viewport,
    // we will switch to the viewport color below so it stays visible over the
    // (typically dark) viewport background.
    glm::vec4 cursorColor = IsPaperSpace() ? g_renderStyle.paperCrosshairs : g_renderStyle.modelCrosshairs;

    const glm::vec3 c(cx, cy, 0.0f);
    const glm::vec3 b00(cx - boxHalf, cy - boxHalf, 0.0f);
    const glm::vec3 b10(cx + boxHalf, cy - boxHalf, 0.0f);
    const glm::vec3 b11(cx + boxHalf, cy + boxHalf, 0.0f);
    const glm::vec3 b01(cx - boxHalf, cy + boxHalf, 0.0f);

    const bool viewportRectPick = IsPaperSpace() && viewportCreateArmed;
    const bool commandRectSelect = ((pendingCommand != PendingCommand::None) && (marqueeMode != MarqueeMode::AutoByDrag)) || viewportRectPick;
    const bool showPointEntryCrosshairs = (crosshairsMode == CrosshairsMode::PointEntryCrosshairs);

    // Crosshair lines:
    // - normal mode: when selection system is inactive
    // - command rectangle selection (e.g. ERASE + W/C): show crosshairs so the user knows to drag a rectangle
    if (!selectionMode || commandRectSelect)
    {
        const float w = static_cast<float>(clientWidth);
        const float h = static_cast<float>(clientHeight);

        float x0 = 0.0f, x1 = w;
        float y0 = 0.0f, y1 = h;

        if (IsPaperSpace() && HasActiveViewport())
{
    const Viewport* vp = GetActiveViewport();
    if (vp)
    {
        const glm::vec2 paperIn = ClientToPaperWorld(mouseClient);
        const bool insideVp = PaperPointInViewport(*vp, paperIn);

        if (insideVp)
            cursorColor = g_renderStyle.viewportCrosshairs;

        const glm::vec2 mn(std::min(vp->p0In.x, vp->p1In.x), std::min(vp->p0In.y, vp->p1In.y));
        const glm::vec2 mx(std::max(vp->p0In.x, vp->p1In.x), std::max(vp->p0In.y, vp->p1In.y));

        const glm::vec2 mnClient = WorldToClient(mn);
        const glm::vec2 mxClient = WorldToClient(mx);

        // Convert paper (Y-down) to overlay coordinates (Y-up)
        const float topClient = std::min(mnClient.y, mxClient.y);
        const float botClient = std::max(mnClient.y, mxClient.y);

        x0 = std::min(mnClient.x, mxClient.x);
        x1 = std::max(mnClient.x, mxClient.x);
        y0 = (float)clientHeight - 1.0f - botClient;
        y1 = (float)clientHeight - 1.0f - topClient;

        // When a viewport is active, the crosshair should only exist within its rectangle.
        // If the cursor is outside the active viewport, hide the crosshair & center box.
        if (!insideVp)
        {
            for (int i = 0; i < 6; ++i)
            {
                if (auto* l = FindEntityById(entityBook, cursorCrossId[i]))
                {
                    l->screenSpace = true;
                    l->line.start = c;
                    l->line.end = c;
                    l->line.color = cursorColor;
                }
            }
            for (int i = 0; i < 4; ++i)
            {
                if (auto* b = FindEntityById(entityBook, cursorBoxId[i]))
                {
                    b->screenSpace = true;
                    b->line.start = c;
                    b->line.end = c;
                    b->line.color = cursorColor;
                }
            }

            // Still allow marquee/viewport-create overlays to render.
            UpdateMarqueeOverlay();
            UpdateViewportCreateOverlay();
            return;
        }
    }
}



        const glm::vec3 h0(x0, cy, 0.0f);
        const glm::vec3 h1(x1, cy, 0.0f);
        const glm::vec3 v0(cx, y0, 0.0f);
        const glm::vec3 v1(cx, y1, 0.0f);

        // Use first two cross entities; collapse the rest.
        if (auto* l0 = FindEntityById(entityBook, cursorCrossId[0])) { l0->screenSpace = true; l0->line.start = h0; l0->line.end = h1; l0->line.color = cursorColor; }
        if (auto* l1 = FindEntityById(entityBook, cursorCrossId[1])) { l1->screenSpace = true; l1->line.start = v0; l1->line.end = v1; l1->line.color = cursorColor; }

        for (int i = 2; i < 6; ++i)
        {
            if (auto* l = FindEntityById(entityBook, cursorCrossId[i]))
            {
                l->screenSpace = true;
                l->line.start = c;
                l->line.end = c;
                l->line.color = cursorColor;
            }
        }
    }
    else
    {
        // Selection active: no crosshair lines.
        for (int i = 0; i < 6; ++i)
        {
            if (auto* l = FindEntityById(entityBook, cursorCrossId[i]))
            {
                l->screenSpace = true;
                l->line.start = c;
                l->line.end = c;
                l->line.color = cursorColor;
            }
        }
    }

    // Center box:
    // - GripsSelectionCrosshairs: show it
    // - PointEntryCrosshairs: hide it (no small square at the crosshair origin)
    // - command rectangle selection: hide it (keeps the "drag rectangle" look)
    if (!commandRectSelect && !showPointEntryCrosshairs)
    {
        if (auto* b0 = FindEntityById(entityBook, cursorBoxId[0])) { b0->screenSpace = true; b0->line.start = b00; b0->line.end = b10; b0->line.color = cursorColor; }
        if (auto* b1 = FindEntityById(entityBook, cursorBoxId[1])) { b1->screenSpace = true; b1->line.start = b10; b1->line.end = b11; b1->line.color = cursorColor; }
        if (auto* b2 = FindEntityById(entityBook, cursorBoxId[2])) { b2->screenSpace = true; b2->line.start = b11; b2->line.end = b01; b2->line.color = cursorColor; }
        if (auto* b3 = FindEntityById(entityBook, cursorBoxId[3])) { b3->screenSpace = true; b3->line.start = b01; b3->line.end = b00; b3->line.color = cursorColor; }
    }
    else
    {
        // Collapse box lines to a point to effectively hide it.
        for (int i = 0; i < 4; ++i)
        {
            if (auto* b = FindEntityById(entityBook, cursorBoxId[i]))
            {
                b->screenSpace = true;
                b->line.start = c;
                b->line.end = c;
                b->line.color = cursorColor;
            }
        }
    }

    // Optional marquee rectangle (screen space)
    UpdateMarqueeOverlay();

    // Optional viewport-create jig rectangle (screen space)
    UpdateViewportCreateOverlay();
}

void Application::UpdateLineJig()
{
    if (pendingCommand != PendingCommand::Line || lineStage != LineStage::SecondNode || !lineHasFirst)
    {
        if (auto* jig = FindEntityById(entityBook, cursorLineJigId))
        {
            // collapse to hide
            jig->screenSpace = false;
            jig->line.start = glm::vec3(0, 0, 0);
            jig->line.end = glm::vec3(0, 0, 0);
        }
        return;
    }

    glm::vec3 cur(mouseWorld.x, mouseWorld.y, 0.0f);
    cur = ApplyOneShotSnap(oneShotSnap, cur);

    glm::vec3 end = cur;
    if (orthoEnabled)
        end = OrthoProjectFromCursor(lineFirstNode, cur);

    if (auto* jig = FindEntityById(entityBook, cursorLineJigId))
    {
        jig->screenSpace = false;
        jig->line.start = lineFirstNode;
        jig->line.end = end;
        jig->line.thickness = 1.0f;
        jig->line.color = IsPaperSpace() ? glm::vec4(0, 0, 0, 1) : glm::vec4(1, 1, 1, 1);
    }
}






// ------------------------------------------------------------
// Grip-selection (screen-space solid rects)
// ------------------------------------------------------------
void Application::EnsureGripEntities(std::size_t requiredCount)
{
    // Create (or expand) the pool of screen-space grip rectangles.
    // If we've already created enough, nothing to do.
    if (gripEntitiesValid && gripRectIds.size() >= requiredCount)
        return;

    const int order = 905; // slightly above cursor box

    while (gripRectIds.size() < requiredCount)
    {
        const std::size_t id = nextId++;
        gripRectIds.push_back(id);

        Entity e;
        e.ID = id;
        e.tag = EntityTag::Cursor;
        e.type = EntityType::SolidRect;
        e.drawOrder = order;
        e.screenSpace = true;

        e.solidRect.min = glm::vec3(0, 0, 0);
        e.solidRect.max = glm::vec3(0, 0, 0);
        e.solidRect.color = glm::vec4(0.2f, 0.5f, 1.0f, 1.0f); // blue

        entityBook.AddEntity(e);
    }

    gripEntitiesValid = true;
    entityBook.SortByDrawOrder();
}

void Application::ClearGrip()
{
    gripTargetEntityId = 0;
    gripA_Model = glm::vec2(0, 0);
    gripB_Model = glm::vec2(0, 0);

    // Hide the on-screen grip rectangles immediately (screen space SolidRect entities).
    auto HideRect = [&](std::size_t id)
    {
        if (auto* e = FindEntityById(entityBook, id))
        {
            e->screenSpace = true;
            e->solidRect.min = glm::vec3(0, 0, 0);
            e->solidRect.max = glm::vec3(0, 0, 0);
        }
    };

    for (std::size_t id : gripRectIds)
        HideRect(id);

    // Force refresh of cached grip entities + renderer
    gripEntitiesValid = false;
    dirtyScene = true;
}

void Application::SetGripTarget(std::size_t entityId, const glm::vec2& aModel, const glm::vec2& bModel)
{
    gripTargetEntityId = entityId;
    gripA_Model = aModel;
    gripB_Model = bModel;
}

static float DistPointSeg2(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b)
{
    const glm::vec2 ab = b - a;
    const float ab2 = glm::dot(ab, ab);
    if (ab2 <= 1e-8f) return glm::dot(p - a, p - a);

    float t = glm::dot(p - a, ab) / ab2;
    t = std::max(0.0f, std::min(1.0f, t));
    const glm::vec2 q = a + t * ab;
    const glm::vec2 d = p - q;
    return glm::dot(d, d);
}

void Application::UpdateGripHover()
{
    if (selectionMode || marqueeActive)
    {
        ClearGrip();
        return;
    }

    glm::vec2 queryPos{};
    bool queryInModelSpace = false;

    if (IsPaperSpace())
    {
        const auto optModel = ClientToViewportModel(mouseClient.x, mouseClient.y);
        if (!optModel.has_value())
        {
            ClearGrip();
            return;
        }
        queryPos = optModel.value();
        queryInModelSpace = true;
    }
    else
    {
        queryPos = mouseWorld;
        queryInModelSpace = false;
    }

    const float pxHalf = 0.5f * (float)SELECTION_BOX_SIZE_PX;

float pxPerWorldX = 1.0f, pxPerWorldY = 1.0f;
GetInteractionPixelsPerWorld(pxPerWorldX, pxPerWorldY);

// Use the larger (more conservative) world radius so grips remain hittable.
const float pxPerWorld = std::max(pxPerWorldX, pxPerWorldY);
const float rWorld = (pxPerWorld > 0.0f) ? (pxHalf / pxPerWorld) : 0.0f;
const float r2 = rWorld * rWorld;

    // Find nearest segment in either the active model-space scene or the viewport's model view.
    const auto& ents = entityBook.GetEntities();
    float bestD2 = 1e30f;
    std::size_t bestId = 0;
    glm::vec2 bestA{}, bestB{};

    if (queryInModelSpace)
    {
        const auto& segs = GetDemoModelSegments2D();
        for (std::size_t i = 0; i < segs.size(); ++i)
        {
            const glm::vec2 a = segs[i].first;
            const glm::vec2 b = segs[i].second;
            const float d2 = DistPointSeg2(queryPos, a, b);
            if (d2 < bestD2)
            {
                bestD2 = d2;
                bestId = 10000 + i;
                bestA = a;
                bestB = b;
            }
        }
    }
    else
    {
        for (const Entity& e : ents)
        {
            if (e.screenSpace) continue;
            if (e.type != EntityType::Line) continue;
            if (e.tag != EntityTag::Scene) continue;
            if (!layerTable.IsLayerSelectable(e.layerId)) continue;

            const glm::vec2 a(e.line.start.x, e.line.start.y);
            const glm::vec2 b(e.line.end.x, e.line.end.y);

            const float d2 = DistPointSeg2(queryPos, a, b);
            if (d2 < bestD2)
            {
                bestD2 = d2;
                bestId = e.ID;
                bestA = a;
                bestB = b;
            }
        }
    }

    if (bestId != 0 && bestD2 <= r2)
    {
        SetGripTarget(bestId, bestA, bestB);
    }
    else
    {
        ClearGrip();
    }
}

void Application::UpdateGripEntities()
{
    // Grips are shown only when one or more entities are selected (persistent selection),
    // and only when the app is idle (no active/pending command).
    //
    // IMPORTANT:
    // Selection truth is EntityBook::SelectedIds() (cached list maintained by EntityBook).
    // Do NOT rely on Application::selectedIndices here; it can drift if any call site
    // toggles Entity::selected via EntityBook APIs.
    const auto& selIds = entityBook.SelectedIds();
    const bool show = (pendingCommand == PendingCommand::None) && !selIds.empty();
    const float vpH = (float)clientHeight;

    const float size = (float)GRIP_SIZE_PX; // px
    const float half = size * 0.5f;

    auto HideRect = [&](std::size_t id)
    {
        if (auto* e = FindEntityById(entityBook, id))
        {
            e->screenSpace = true;
            e->solidRect.min = glm::vec3(0, 0, 0);
            e->solidRect.max = glm::vec3(0, 0, 0);
        }
    };

    if (!show)
    {
        for (std::size_t id : gripRectIds) HideRect(id);
        return;
    }

    // 2 grips per selected line (start + end)
    EnsureGripEntities(selIds.size() * 2);

    auto SetRect = [&](std::size_t id, const glm::vec2& cUp)
    {
        if (auto* e = FindEntityById(entityBook, id))
        {
            e->screenSpace = true;
            e->solidRect.min = glm::vec3(cUp.x - half, cUp.y - half, 0.0f);
            e->solidRect.max = glm::vec3(cUp.x + half, cUp.y + half, 0.0f);
        }
    };

    std::size_t out = 0;

    for (std::size_t entId : selIds)
    {
        const Entity* sel = FindEntityById(entityBook, entId);
        if (!sel) continue;

        // For now, only Line grips (2 endpoints). Expand for other entity types later.
        if (sel->type != EntityType::Line)
            continue;

        const glm::vec2 aModel(sel->line.start.x, sel->line.start.y);
        const glm::vec2 bModel(sel->line.end.x, sel->line.end.y);

        glm::vec2 aClient(0, 0), bClient(0, 0);
        if (IsPaperSpace())
        {
            const Viewport* vp = GetActiveViewport();
            if (vp)
            {
                aClient = ViewportModelToClient(*vp, aModel);
                bClient = ViewportModelToClient(*vp, bModel);
            }
            else
            {
                aClient = WorldToClient(aModel);
                bClient = WorldToClient(bModel);
            }
        }
        else
        {
            aClient = WorldToClient(aModel);
            bClient = WorldToClient(bModel);
        }

        // Convert client Y-down to overlay Y-up coords (screen-space entities expect Y-up).
        const glm::vec2 aUp(aClient.x, vpH - 1.0f - aClient.y);
        const glm::vec2 bUp(bClient.x, vpH - 1.0f - bClient.y);

        if (out + 1 < gripRectIds.size())
        {
            SetRect(gripRectIds[out++], aUp);
            SetRect(gripRectIds[out++], bUp);
        }
    }

    // Hide any remaining grip rects (e.g., non-line selected entities).
    for (std::size_t i = out; i < gripRectIds.size(); ++i)
        HideRect(gripRectIds[i]);
}
// ------------------------------------------------------------
// Debug HUD text (screen space)
// ------------------------------------------------------------
void Application::EnsureHudEntities()
{
    if (hudEntitiesValid)
        return;

    // Multi-line projection info (upper-left)
    hudOrthoId = nextId++;
    entityBook.AddEntity(MakeText(hudOrthoId, EntityTag::Hud, 960,
        "",
        glm::vec3(16.0f, 16.0f, 0.0f),
        420.0f, 140.0f,
        false,
        TextHAlign::Left,
        0.8f,
        glm::vec4(1, 1, 1, 1),
        1.0f,
        true,
        true,
        glm::vec4(0, 0, 0, 0.75f),
        8.0f));

    // Cursor position (lower-right)
    hudCursorId = nextId++;
    entityBook.AddEntity(MakeText(hudCursorId, EntityTag::Hud, 960,
        "",
        glm::vec3(16.0f, 16.0f, 0.0f),
        320.0f, 70.0f,
        false,
        TextHAlign::Right,
        0.8f,
        glm::vec4(1, 1, 1, 1),
        1.0f,
        true,
        true,
        glm::vec4(0, 0, 0, 0.75f),
        8.0f));

    // Bottom-left status/help line.
    hudStatusId = nextId++;
    entityBook.AddEntity(MakeText(hudStatusId, EntityTag::Hud, 950,
        "",
        glm::vec3(16.0f, 24.0f, 0.0f),
        1200.0f, 60.0f,
        false,
        TextHAlign::Left,
        1.0f,
        glm::vec4(1, 1, 1, 1),
        1.0f,
        true));

    hudEntitiesValid = true;

    entityBook.SortByDrawOrder();
}

void Application::UpdateHudEntities()
{
    // Projection bounds in active space units.
    const glm::vec2 w00 = ClientToWorld(glm::ivec2(0, 0));
    const glm::vec2 w11 = ClientToWorld(glm::ivec2(clientWidth, clientHeight));

    const float left = std::min(w00.x, w11.x);
    const float right = std::max(w00.x, w11.x);
    const float top = std::min(w00.y, w11.y);
    const float bottom = std::max(w00.y, w11.y);

    const glm::vec2 center = ClientToWorld(glm::ivec2(clientWidth / 2, clientHeight / 2));

    const char* spaceName = IsPaperSpace() ? "Paper" : "World";

    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "Ortho (%s)\n"
        "  L: %.3f   R: %.3f\n"
        "  T: %.3f   B: %.3f\n"
        "Center: (%.3f, %.3f)",
        spaceName,
        left, right,
        top, bottom,
        center.x, center.y);

    if (auto* t = FindEntityById(entityBook, hudOrthoId))
    {
        t->type = EntityType::Text;
        t->screenSpace = true;

        const float margin = 16.0f;
        t->text.position = glm::vec3(margin, (float)clientHeight - t->text.boxHeight - margin, 0.0f);
        t->text.text = buf;
    }

    // Cursor world/paper position (active space)
    char buf2[256];
    std::snprintf(buf2, sizeof(buf2),
        "Cursor (%s)\n(%.3f, %.3f)",
        spaceName,
        mouseWorld.x, mouseWorld.y);

    if (auto* t2 = FindEntityById(entityBook, hudCursorId))
    {
        t2->type = EntityType::Text;
        t2->screenSpace = true;

        const float margin = 16.0f;
        const float boxW = t2->text.boxWidth;
        t2->text.position = glm::vec3((float)clientWidth - boxW - margin, margin, 0.0f);
        t2->text.text = buf2;
        t2->text.hAlign = TextHAlign::Right;
    }

    // Bottom-left status/help line (avoid overlapping multiple HUD text blocks).
    if (auto* ts = FindEntityById(entityBook, hudStatusId))
    {
        ts->type = EntityType::Text;
        ts->screenSpace = true;

        const float margin = 16.0f;
        ts->text.position = glm::vec3(margin, margin + 8.0f, 0.0f);
        ts->text.hAlign = TextHAlign::Left;

        char sbuf[512];
        if (IsPaperSpace())
        {
            std::snprintf(sbuf, sizeof(sbuf),
                "Paper Space (Ctrl+Shift+P)  |  Export PDF: Ctrl+Shift+E\n"
                "%s",
                selectionMode ? "Selection: ON (LMB pick)" : "Selection: OFF (Crosshair)");
        }
        else
        {
            std::snprintf(sbuf, sizeof(sbuf),
                "%s",
                selectionMode ? "Selection: ON (LMB pick)" : "Selection: OFF (Crosshair)");
        }
        ts->text.text = sbuf;
    }
}

// ------------------------------------------------------------
// Demo scene/grid (simple, safe defaults)
// ------------------------------------------------------------
void Application::RebuildScene()
{
    // Paper space should not show the model-space demo (dragon). Paper space
    // content will be page-annotation entities and (later) viewports.
    if (IsPaperSpace())
    {
        // Draw viewport borders (paper-space entities)
        for (const auto& vp : viewports)
        {
            const glm::vec2 mn(std::min(vp.p0In.x, vp.p1In.x), std::min(vp.p0In.y, vp.p1In.y));
            const glm::vec2 mx(std::max(vp.p0In.x, vp.p1In.x), std::max(vp.p0In.y, vp.p1In.y));

            const bool isActive = vp.active;

// Active viewport highlight:
// - Blue when active & unlocked
// - Amber when active & locked (view is protected)
const glm::vec4 col = isActive
    ? (vp.locked ? glm::vec4(1.0f, 0.65f, 0.15f, 1.0f) : glm::vec4(0.2f, 0.5f, 1.0f, 1.0f))
    : glm::vec4(0, 0, 0, 1);
const float borderThickness = isActive ? 2.5f : 1.0f;
const glm::vec3 a(mn.x, mn.y, 0.0f);
            const glm::vec3 b(mx.x, mn.y, 0.0f);
            const glm::vec3 c(mx.x, mx.y, 0.0f);
            const glm::vec3 d(mn.x, mx.y, 0.0f);

            {
                Entity e1 = MakeLine(vp.id + 1, EntityTag::Paper, 120, a, b, col, borderThickness, false);
                Entity e2 = MakeLine(vp.id + 2, EntityTag::Paper, 120, b, c, col, borderThickness, false);
                Entity e3 = MakeLine(vp.id + 3, EntityTag::Paper, 120, c, d, col, borderThickness, false);
                Entity e4 = MakeLine(vp.id + 4, EntityTag::Paper, 120, d, a, col, borderThickness, false);
                const uint32_t lid = (vp.borderLayerId != LayerTable::kInvalidLayerId) ? vp.borderLayerId : layerTable.CurrentLayerId();
                e1.layerId = lid; e2.layerId = lid; e3.layerId = lid; e4.layerId = lid;
                // When active, force a highlight color (ignore layer color).
                // When inactive, honor the layer's color so borders behave like normal geometry.
                e1.colorByLayer = e2.colorByLayer = e3.colorByLayer = e4.colorByLayer = !isActive;
                e1.linetypeByLayer = e2.linetypeByLayer = e3.linetypeByLayer = e4.linetypeByLayer = true;
                entityBook.AddEntity(e1);
                entityBook.AddEntity(e2);
                entityBook.AddEntity(e3);
                entityBook.AddEntity(e4);
// Draw an "ACTIVE" label so it's obvious we are editing through this viewport.
if (isActive)
{
    const std::string label = vp.locked ? "ACTIVE VIEWPORT (LOCKED)" : "ACTIVE VIEWPORT";
    const glm::vec3 p(mn.x + 0.08f, mn.y + 0.10f, 0.0f);
    Entity t = MakeText((uint32_t)nextId++, EntityTag::Paper, 121,
        label,
        p,
        4.0f, 0.25f,
        false,
        TextHAlign::Left,
        0.50f,
        col,
        1.0f,
        false);
    t.layerId = (vp.borderLayerId != LayerTable::kInvalidLayerId) ? vp.borderLayerId : layerTable.CurrentLayerId();
    t.colorByLayer = false;
    t.linetypeByLayer = true;
    entityBook.AddEntity(t);
}

            }
        }

    }

    const int dragonIterations = 12; // 4096 segments
    const glm::vec3 dragonOriginWorld(0.0f, 0.0f, 0.0f); // TRUE world origin

    // Deterministic random colors (stable between rebuilds)
    std::mt19937 rng(1337u);
    std::uniform_real_distribution<float> dist(0.15f, 1.0f);
    auto RandColor = [&]()
        {
            return glm::vec4(dist(rng), dist(rng), dist(rng), 1.0f);
        };

    DragonCurve curve;
    const auto segs = curve.Build(dragonIterations, dragonOriginWorld);

    const int drawOrder = 100;
    const float thickness = 2.0f;

    // Stable ids for dragon segments so commands like ERASE can persist
    // across scene rebuilds (pan/zoom/etc.).
    constexpr uint32_t DRAGON_ID_BASE = 10000;

    for (std::size_t i = 0; i < segs.size(); ++i)
    {
        const uint32_t segId = DRAGON_ID_BASE + (uint32_t)i;
        if (erasedDragonIds.find(segId) != erasedDragonIds.end())
            continue;

        const auto& s = segs[i];
        {
            Entity e = MakeLine(
                segId,
                EntityTag::Scene,
                drawOrder,
                s.a,
                s.b,
                RandColor(),
                thickness,
                false); // false = not HUD → world space
            e.layerId = layerTable.CurrentLayerId();
            e.colorByLayer = false;      // keep per-segment random colors
            e.linetypeByLayer = true;
            entityBook.AddEntity(e);
        }
    }

    // Keep nextId above our reserved dragon id range.
    nextId = std::max(nextId, DRAGON_ID_BASE + (uint32_t)segs.size() + 1u);

    // Bottom-left HUD status/help is updated every frame in UpdateHudEntities().
}



void Application::RebuildGrid()
{
    // In paper space, draw the page chrome instead of the world grid.
    if (IsPaperSpace())
    {
        // Page bounds (inches, origin top-left, y down)
        const float W = page.widthIn;
        const float H = page.heightIn;

        if (VKLog::Enabled(VKLog::Page, VKLog::Level::Info))
        {
            const float px0 = page.marginLeftIn;
            const float py0 = page.marginTopIn;
            const float px1 = W - page.marginRightIn;
            const float py1 = H - page.marginBottomIn;
            VKLog::Logf(VKLog::Page, VKLog::Level::Info,
                "PaperPage W=%.3f H=%.3f margins(L=%.3f T=%.3f R=%.3f B=%.3f) pageRect[(0,0)-(%.3f,%.3f)] printable[(%.3f,%.3f)-(%.3f,%.3f)]",
                W, H,
                page.marginLeftIn, page.marginTopIn, page.marginRightIn, page.marginBottomIn,
                W, H,
                px0, py0, px1, py1);
        }

        const glm::vec4 pageBorder(0.0f, 0.0f, 0.0f, 1.0f);
        const glm::vec4 printableBorder(0.25f, 0.25f, 0.25f, 1.0f);

        auto addRect = [&](float x0, float y0, float x1, float y1, const glm::vec4& c, float lw)
        {
            entityBook.AddEntity(MakeLine(nextId++, EntityTag::Grid, -100,
                glm::vec3(x0, y0, 0.0f), glm::vec3(x1, y0, 0.0f), c, lw, false));
            entityBook.AddEntity(MakeLine(nextId++, EntityTag::Grid, -100,
                glm::vec3(x1, y0, 0.0f), glm::vec3(x1, y1, 0.0f), c, lw, false));
            entityBook.AddEntity(MakeLine(nextId++, EntityTag::Grid, -100,
                glm::vec3(x1, y1, 0.0f), glm::vec3(x0, y1, 0.0f), c, lw, false));
            entityBook.AddEntity(MakeLine(nextId++, EntityTag::Grid, -100,
                glm::vec3(x0, y1, 0.0f), glm::vec3(x0, y0, 0.0f), c, lw, false));
        };

        // Outer page rectangle
        addRect(0.0f, 0.0f, W, H, pageBorder, 2.0f);

        // Printable area
        const float px0 = page.marginLeftIn;
        const float py0 = page.marginTopIn;
        const float px1 = W - page.marginRightIn;
        const float py1 = H - page.marginBottomIn;
        addRect(px0, py0, px1, py1, printableBorder, 1.5f);

        return;
    }

    if (!gridEnabled)
        return;

    // World-space grid (zooms & pans with camera)

    // Standard grid colors
    const glm::vec4 minor(0.22f, 0.22f, 0.22f, 1.0f);
    const glm::vec4 major(0.32f, 0.32f, 0.32f, 1.0f);

    // Origin axes (faded)
    const glm::vec4 xAxisColor(0.2f, 0.8f, 0.2f, 1.0f); // X == 0 → green
    const glm::vec4 yAxisColor(0.8f, 0.2f, 0.2f, 1.0f); // Y == 0 → red

    const int minorStep = 25;
    const int majorStep = 100;

    const float safeZoom = std::max(0.0001f, modelZoom);
    const float invZoom = 1.0f / safeZoom;

    // Visible world rect
    const float worldL = modelPan.x;
    const float worldT = modelPan.y;
    const float worldR = modelPan.x + (float)clientWidth * invZoom;
    const float worldB = modelPan.y + (float)clientHeight * invZoom;

    // Overscan to avoid edge gaps
    const float overscanScreens = 1.0f;
    const float padX = (float)clientWidth * invZoom * overscanScreens;
    const float padY = (float)clientHeight * invZoom * overscanScreens;

    const float L = worldL - padX;
    const float R = worldR + padX;
    const float T = worldT - padY;
    const float B = worldB + padY;

    auto floorToStep = [](float v, int step) -> int
        {
            return (int)std::floor(v / (float)step) * step;
        };
    auto ceilToStep = [](float v, int step) -> int
        {
            return (int)std::ceil(v / (float)step) * step;
        };

    const int x0 = floorToStep(L, minorStep);
    const int x1 = ceilToStep(R, minorStep);
    const int y0 = floorToStep(T, minorStep);
    const int y1 = ceilToStep(B, minorStep);

    // ------------------------------------------------------------
    // Vertical grid lines (constant X)
    // ------------------------------------------------------------
    for (int x = x0; x <= x1; x += minorStep)
    {
        glm::vec4 color = minor;

        if (x == 0)
        {
            color = xAxisColor;   // X axis
        }
        else if ((x % majorStep) == 0)
        {
            color = major;
        }

        entityBook.AddEntity(MakeLine(nextId++, EntityTag::Grid, 0,
            glm::vec3((float)x, (float)y0, 0.0f),
            glm::vec3((float)x, (float)y1, 0.0f),
            color, 1.5f, false));
    }

    // ------------------------------------------------------------
    // Horizontal grid lines (constant Y)
    // ------------------------------------------------------------
    for (int y = y0; y <= y1; y += minorStep)
    {
        glm::vec4 color = minor;

        if (y == 0)
        {
            color = yAxisColor;   // Y axis
        }
        else if ((y % majorStep) == 0)
        {
            color = major;
        }

        entityBook.AddEntity(MakeLine(nextId++, EntityTag::Grid, 0,
            glm::vec3((float)x0, (float)y, 0.0f),
            glm::vec3((float)x1, (float)y, 0.0f),
            color, 1.5f, false));
    }

    (void)wipeoutEnabled;
}







bool Application::HasSelection() const
{
    return !selectedIndices.empty();
}

std::size_t Application::GetSelectedCount() const
{
    return selectedIndices.size();
}

uint32_t Application::GetSelectionLayerIdCommon() const
{
    if (selectedIndices.empty())
        return LayerTable::kInvalidLayerId;

    const auto& ents = entityBook.GetEntities();
    uint32_t lid = LayerTable::kInvalidLayerId;

    for (std::size_t idx : selectedIndices)
    {
        if (idx >= ents.size()) continue;
        const Entity& e = ents[idx];
        if (e.tag != EntityTag::Scene) continue;
        if (lid == LayerTable::kInvalidLayerId)
            lid = e.layerId;
        else if (e.layerId != lid)
            return LayerTable::kInvalidLayerId;
    }

    return lid;
}

void Application::SetSelectionLayer(uint32_t layerId)
{
    auto& ents = entityBook.GetEntitiesMutable();
    for (std::size_t idx : selectedIndices)
    {
        if (idx >= ents.size()) continue;
        Entity& e = ents[idx];
        if (e.tag != EntityTag::Scene) continue;
        e.layerId = layerId;

        // If "ByLayer", immediately adopt the target layer defaults.
        if (e.colorByLayer)
        {
            if (auto* L = layerTable.Find(layerId))
            {
                if (e.type == EntityType::Line) e.line.color = L->defaultColor;
                if (e.type == EntityType::Text) e.text.color = L->defaultColor;
            }
        }
        if (e.linetypeByLayer)
        {
            if (auto* L = layerTable.Find(layerId))
                e.linetypeOverride = L->defaultLinetype;
        }
    }

    dirtyPickTree = true;
}

bool Application::GetSelectionColorCommon(bool& outByLayer, glm::vec4& outColor) const
{
    if (selectedIndices.empty())
        return false;

    const auto& ents = entityBook.GetEntities();
    bool first = true;
    bool byLayer = true;
    glm::vec4 col(1,1,1,1);

    for (std::size_t idx : selectedIndices)
    {
        if (idx >= ents.size()) continue;
        const Entity& e = ents[idx];
        if (e.tag != EntityTag::Scene) continue;
        if (e.type != EntityType::Line && e.type != EntityType::Text) continue;

        const bool eByLayer = e.colorByLayer;
        const glm::vec4 eCol = (e.type == EntityType::Line) ? e.line.color : e.text.color;

        if (first)
        {
            first = false;
            byLayer = eByLayer;
            col = eCol;
        }
        else
        {
            if (byLayer != eByLayer)
                return false;
            if (col != eCol)
                return false;
        }
    }

    if (first)
        return false;

    outByLayer = byLayer;
    outColor = col;
    return true;
}

void Application::SetSelectionColorByLayer()
{
    auto& ents = entityBook.GetEntitiesMutable();
    for (std::size_t idx : selectedIndices)
    {
        if (idx >= ents.size()) continue;
        Entity& e = ents[idx];
        if (e.tag != EntityTag::Scene) continue;

        e.colorByLayer = true;

        const LayerTable::Layer* L = layerTable.Find(e.layerId);
        const glm::vec4 c = L ? L->defaultColor : glm::vec4(1,1,1,1);

        if (e.type == EntityType::Line) e.line.color = c;
        if (e.type == EntityType::Text) e.text.color = c;
    }
}

void Application::SetSelectionColorCustom(const glm::vec4& rgba)
{
    auto& ents = entityBook.GetEntitiesMutable();
    for (std::size_t idx : selectedIndices)
    {
        if (idx >= ents.size()) continue;
        Entity& e = ents[idx];
        if (e.tag != EntityTag::Scene) continue;

        e.colorByLayer = false;

        if (e.type == EntityType::Line) e.line.color = rgba;
        if (e.type == EntityType::Text) e.text.color = rgba;
    }
}


std::vector<std::pair<EntityType, std::size_t>> Application::GetSelectionTypeCounts() const
{
    std::size_t nLine = 0, nText = 0, nSolidRect = 0;

    const auto& ents = entityBook.GetEntities();
    for (std::size_t idx : selectedIndices)
    {
        if (idx >= ents.size()) continue;
        const Entity& e = ents[idx];
        if (e.tag != EntityTag::Scene) continue;

        switch (e.type)
        {
        case EntityType::Line:      ++nLine; break;
        case EntityType::Text:      ++nText; break;
        case EntityType::SolidRect: ++nSolidRect; break;
        }
    }

    std::vector<std::pair<EntityType, std::size_t>> out;
    if (nLine)      out.push_back({ EntityType::Line, nLine });
    if (nText)      out.push_back({ EntityType::Text, nText });
    if (nSolidRect) out.push_back({ EntityType::SolidRect, nSolidRect });
    return out;
}

std::vector<std::size_t> Application::GetSelectedIndicesByType(std::optional<EntityType> filterType) const
{
    if (!filterType.has_value())
        return selectedIndices;

    const auto& ents = entityBook.GetEntities();
    std::vector<std::size_t> out;
    out.reserve(selectedIndices.size());

    for (std::size_t idx : selectedIndices)
    {
        if (idx >= ents.size()) continue;
        const Entity& e = ents[idx];
        if (e.tag != EntityTag::Scene) continue;
        if (e.type == *filterType)
            out.push_back(idx);
    }
    return out;
}

bool Application::SelectEntityById(std::size_t entityId, bool additive)
{
    if (!allowSelectionMutation)
    {
        DebugSelectionf("BLOCKED SelectEntityById id=%zu additive=%d", entityId, additive ? 1 : 0);
        return false;
    }

    const auto& ents = entityBook.GetEntities();
    for (std::size_t i = 0; i < ents.size(); ++i)
    {
        const Entity& e = ents[i];
        // Viewport borders live in paper-space (EntityTag::Paper) and must be selectable.
        if (e.tag != EntityTag::Scene && e.tag != EntityTag::User && e.tag != EntityTag::Paper) continue;
        if (e.ID == entityId)
        {
            if (additive)
                AddToSelection(i);
            else
                ApplySelection({ i });
            return true;
        }
    }
    return false;
}

void Application::CancelInteractions()
{
    // CancelInteractions can be triggered by ESC outside of selection input handlers.
    // Allow selection mutation here so ClearSelection() actually clears the highlight.
    const bool prevAllow = allowSelectionMutation;
    allowSelectionMutation = true;

    marqueeActive = false;
    UpdateMarqueeOverlay(); // collapse marquee lines to cursor point
    UpdateViewportCreateOverlay(); // collapse viewport jig lines to cursor point

    ClearHover();
    ClearGrip();
    ClearSelection();

    // Cancel any interactive command
    if (pendingCommand != PendingCommand::None)
    {
        pendingCommand = PendingCommand::None;
        marqueeMode = MarqueeMode::AutoByDrag;
        selectionMode = false;

        // LINE-specific
        lineStage = LineStage::None;
        lineHasFirst = false;
        crosshairsMode = CrosshairsMode::GripsSelectionCrosshairs;
        oneShotSnap = SnapMode::None;
        lineCmdWnd = nullptr;
    }

    MarkAllDirty();

    allowSelectionMutation = prevAllow;
}

void Application::ApplyLayerDefaultsToEntities()
{
    // Update all ByLayer entities in the current space's EntityBook.
    auto& ents = entityBook.GetEntitiesMutable();
    for (auto& e : ents)
    {
        if (e.tag != EntityTag::Scene && e.tag != EntityTag::User)
            continue;

        const LayerTable::Layer* L = layerTable.Find(e.layerId);
        if (!L) continue;

        if (e.colorByLayer)
        {
            if (e.type == EntityType::Line) e.line.color = L->defaultColor;
            if (e.type == EntityType::Text) e.text.color = L->defaultColor;
        }

        if (e.linetypeByLayer)
        {
            e.linetypeOverride = L->defaultLinetype;
        }
    }

    dirtyPickTree = true;
}


// ============================================================
// Python scripting (optional)
// ============================================================

void Application::ScriptAddLine(float x0, float y0, float z0,
                                float x1, float y1, float z1,
                                float r, float g, float b, float a,
                                float thickness, int drawOrder)
{
    Entity e;
    e.ID = nextId++;
    e.tag = EntityTag::User;
    e.type = EntityType::Line;
    e.drawOrder = drawOrder;
    e.screenSpace = false;

    e.line.start = glm::vec3(x0, y0, z0);
    e.line.end = glm::vec3(x1, y1, z1);
    e.line.color = glm::vec4(r, g, b, a);
    e.line.thickness = thickness;

    // Put script-created entities on the current layer.
    e.layerId = layerTable.CurrentLayerId();
    e.colorByLayer = false;
    e.linetypeByLayer = true;

    entityBook.AddEntity(e);
    entityBook.SortByDrawOrder();
    dirtyPickTree = true;
}


void Application::ScriptAddText(const std::string& text,
                                float x, float y, float z,
                                float scale,
                                float r, float g, float b, float a,
                                int drawOrder)
{
    // Minimal text entity creation: free-positioned, no wrap, left aligned.
    Entity e;
    e.ID = nextId++;
    e.tag = EntityTag::User;
    e.type = EntityType::Text;
    e.drawOrder = drawOrder;
    e.screenSpace = false;

    e.text.text = text;
    e.text.position = glm::vec3(x, y, z);
    e.text.boxWidth = 0.0f;
    e.text.boxHeight = 0.0f;
    e.text.wordWrapEnabled = false;
    e.text.hAlign = TextHAlign::Left;
    e.text.scale = scale;
    e.text.color = glm::vec4(r, g, b, a);
    e.text.strokeWidth = 1.0f;
    e.text.font = g_hersheyFont;

    e.layerId = layerTable.CurrentLayerId();
    e.colorByLayer = false;
    e.linetypeByLayer = true;

    entityBook.AddEntity(e);
    entityBook.SortByDrawOrder();
    dirtyPickTree = true;
}


bool Application::RunPythonScript(const std::string& scriptPath, CmdWindow* cmdWnd)
{
    return pyRunner.RunFile(this, scriptPath, cmdWnd);
}

// ------------------------------------------------------------
// File I/O (JSON)
// ------------------------------------------------------------

static bool HasJsonExtension(const std::string& path)
{
    auto lower = path;
    for (auto& c : lower) c = (char)tolower((unsigned char)c);
    return (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".json");
}

static bool ShowSaveAsDialog(HWND owner, std::string& outPath)
{
#ifdef _WIN32
    char fileBuf[MAX_PATH] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter =
        "VectorKernel JSON (*.json)\0*.json\0"
        "All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = "json";

    if (!GetSaveFileNameA(&ofn))
        return false;

    outPath = fileBuf;
    if (!HasJsonExtension(outPath))
        outPath += ".json";
    return true;
#else
    (void)owner;
    (void)outPath;
    return false;
#endif
}

static bool SaveCurrentSceneEntitiesJson(const std::string& path, const EntityBook& book)
{
    std::vector<Entity> sceneOnly;
    sceneOnly.reserve(book.GetEntities().size());
    for (const auto& e : book.GetEntities())
    {
        // Only persist actual drawing content.
        // Grid/Cursor/Hud are regenerated every frame.
        if (e.tag != EntityTag::Scene && e.tag != EntityTag::User)
            continue;
        sceneOnly.push_back(e);
    }
    return SceneJsonIO::SaveEntitiesToFile(path, sceneOnly, 1);
}

bool Application::Save()
{
    if (currentFilePath.empty())
        return SaveAs();

    return SaveCurrentSceneEntitiesJson(currentFilePath, entityBook);
}

bool Application::SaveAs()
{
    std::string path;
    if (!ShowSaveAsDialog(hwndMain, path))
        return false;

    currentFilePath = path;
    return SaveCurrentSceneEntitiesJson(currentFilePath, entityBook);
}
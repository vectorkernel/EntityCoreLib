
// main.cpp
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include "glad.h"

#include <chrono>
#include <algorithm>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp> // glm::ortho

#include "Application.h"
#include "LayerTable.h"
#include "DragonCurve.h"
#include "CommandManager.h"
#include "CmdWindow.h"
#include "PropertiesWindow.h"
#include "LayersWindow.h"
#include "Logger.h"
#include "LogSink.h"

#include "PDFPlotter.h"
#include "RenderStyle.h"

// ADD: renderer headers
#include "StatefulVectorRenderer.h"
#include "RenderLoopRenderer.h"
#include "RenderContext.h"
#include "LinePass.h"

#include "ShaderLibrary.h"

static Application g_app;

// Commands + shortcuts
static CommandManager g_commands;

// Standalone RichEdit command window
static CmdWindow g_cmdWnd;

// Adapter: route RenderCoreLib log output into the command window.
struct CmdWindowLogSink : public ILogSink
{
    CmdWindow* wnd = nullptr;
    void AddLine(std::string_view text) override
    {
        if (!wnd) return;
        wnd->AppendTextUtf8(std::string(text));
    }
};

static CmdWindowLogSink g_cmdWndSink;

// Floating Properties window (Ctrl+1)
static PropertiesWindow g_propWnd;

// Floating Layers window (Ctrl+2)
static LayersWindow g_layersWnd;

// ADD: global renderer instance
static StatefulVectorRenderer g_paperRenderer;
static StatefulVectorRenderer g_modelRenderer;
static RenderLoopRenderer g_overlayRenderer;

// ------------------------------------------------------------
// Stateful renderer redraw gating
// ------------------------------------------------------------
static bool     g_needRedraw = true;           // set by input/events/commands
static bool     g_forceRendererRebuild = true; // forces stateful renderer cache rebuild
static uint64_t g_lastEntityBookRevision = 0;

// ------------------------------------------------------------
// Selection highlight diagnostics
// ------------------------------------------------------------
static bool IsSelectionHighlightColor(const glm::vec4& c)
{
    const float eps = 1e-4f;
    return (fabsf(c.r - 1.0f) < eps) && (fabsf(c.g - 1.0f) < eps) && (fabsf(c.b - 0.0f) < eps) && (fabsf(c.a - 1.0f) < eps);
}

static size_t CountHighlightedLines(const EntityBook& book)
{
    size_t count = 0;
    const auto& ents = book.GetEntities();
    for (const Entity& e : ents)
    {
        if (e.type == EntityType::Line && !e.colorByLayer && IsSelectionHighlightColor(e.line.color))
            ++count;
    }
    return count;
}

static void RequestRedraw(bool forceRebuild = false)
{
    g_needRedraw = true;
    if (forceRebuild)
        g_forceRendererRebuild = true;
}

// Debug: visualize the exact OpenGL scissor rectangles used for viewport clipping.
static bool g_drawViewportClipDebug = true;
// Optional inset in *client* pixels applied before converting to framebuffer pixels.
// Keep at 0 while diagnosing scissor/border alignment.
static int  g_viewportScissorInsetPx = 0;

struct DebugClipRectTL
{
    // Client pixels, top-left origin (Y-down)
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};


// Extract viewport dimensions from glm::ortho(0, w, h, 0, -1, 1) (Y-down).
static void ExtractViewportWH_FromOrthoYDown(const glm::mat4& proj, float& outW, float& outH)
{
    const float m00 = proj[0][0];
    const float m11 = proj[1][1];

    outW = (m00 != 0.0f) ? (2.0f / m00) : 1.0f;
    outH = (m11 != 0.0f) ? (-2.0f / m11) : 1.0f;
}


static const char* EntityTypeName(EntityType t)
{
    switch (t)
    {
    case EntityType::Line:      return "Lines";
    case EntityType::Text:      return "Text";
    case EntityType::SolidRect: return "SolidRect";
    }
    return "Unknown";
}

static std::string WideToUtf8(const std::wstring& ws)
{
    if (ws.empty()) return {};
    const int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) return {};
    std::string out(sizeNeeded, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), out.data(), sizeNeeded, nullptr, nullptr);
    return out;
}

// Alt+P: prompt for a .py file and run it using the embedded runner.
static bool OpenAndRunPythonScript(HWND owner)
{
    wchar_t fileBuf[MAX_PATH] = L"";

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"Python scripts (*.py)\0*.py\0All files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (!GetOpenFileNameW(&ofn))
        return false;

    g_cmdWnd.Show(true);
    const std::string pathUtf8 = WideToUtf8(fileBuf);
    g_app.RunPythonScript(pathUtf8, &g_cmdWnd);
    return true;
}

// ------------------------------------------------------------
// OpenGL init
// ------------------------------------------------------------
static void InitOpenGL(HWND hwnd)
{
    HDC hdc = GetDC(hwnd);

    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pf = ChoosePixelFormat(hdc, &pfd);
    SetPixelFormat(hdc, pf, &pfd);

    HGLRC rc = wglCreateContext(hdc);
    wglMakeCurrent(hdc, rc);

    if (!gladLoadGL())
    {
        MessageBoxA(hwnd, "Failed to load OpenGL via glad.", "Error", MB_OK | MB_ICONERROR);
        std::exit(1);
    }

    // ADD: init renderer once GL is ready
    g_paperRenderer.Init();
    g_modelRenderer.Init();
    g_overlayRenderer.Init();

    // Enable alpha blending (HUD backgrounds / wipeouts)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    ReleaseDC(hwnd, hdc);
}

// ------------------------------------------------------------
// Resize hook
// ------------------------------------------------------------
static void OnResize(int w, int h)
{
    g_app.OnResize(w, h);
    glViewport(0, 0, w, h);
}

// ------------------------------------------------------------
// Render
// ------------------------------------------------------------
static void RenderFrame(HWND hwnd)
{
    if (g_app.IsPaperSpace())
    {
        // Paper-space background:
        // - Clear whole window to gray.
        // - Draw the page as a world-space SolidRect using the same camera matrices
        //   as the page borders. This avoids pixel-snapped scissor rounding that can
        //   make borders appear to "drift" relative to the white page when panning/zooming.
        // We temporarily disable scissoring so the gray clear covers the entire window.
        // Restore the previous state immediately after the clear so later passes that
        // rely on scissoring (e.g. UI / clipped overlays) behave normally.
        const GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(g_renderStyle.paperBackground.r, g_renderStyle.paperBackground.g, g_renderStyle.paperBackground.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (scissorWasEnabled)
            glEnable(GL_SCISSOR_TEST);
    }
    else
    {
        glClearColor(g_renderStyle.modelBackground.r, g_renderStyle.modelBackground.g, g_renderStyle.modelBackground.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    // Hot-swap shader programs (file-backed programs will recompile when modified).
    // Inline programs can still be replaced by re-registering them in ShaderLibrary.
    ShaderLibrary::Get().ReloadChanged();

    // ADD: draw EntityBook through renderer each frame
    RenderContext ctx;
    ctx.projection = g_app.GetProjectionMatrix();
    ctx.view = g_app.GetViewMatrix();
    ctx.model = g_app.GetModelMatrix();

    // Ensure paper/model passes never "leak" entities across spaces.
    // In MODEL space we still want the grid, but we do NOT want paper-space
    // chrome/annotations (Paper/PaperUser).
    auto TAG = [](EntityTag t) { return 1u << (uint32_t)t; };
    if (g_app.IsPaperSpace())
        g_paperRenderer.SetTagMask(TAG(EntityTag::Grid) | TAG(EntityTag::Paper) | TAG(EntityTag::PaperUser));
    else
        g_paperRenderer.SetTagMask(TAG(EntityTag::Grid));

    // Paper page "white sheet" as real geometry (world-space) so it stays perfectly
    // locked to the page borders under pan/zoom.
    if (g_app.IsPaperSpace())
    {
        Entity pageRect{};
        pageRect.ID = 0; // not persisted; immediate-only
        pageRect.type = EntityType::SolidRect;
        pageRect.tag = EntityTag::Grid;
        pageRect.drawOrder = -1000;
        pageRect.screenSpace = false;
        pageRect.solidRect.min = glm::vec3(0.0f, 0.0f, -0.5f);
        pageRect.solidRect.max = glm::vec3(g_app.GetPageWidthInches(), g_app.GetPageHeightInches(), -0.5f);
        pageRect.solidRect.color = glm::vec4(1, 1, 1, 1);

        g_overlayRenderer.BeginFrame();
        g_overlayRenderer.Submit(pageRect);
        g_overlayRenderer.Draw(ctx);

        // MODEL space: selection overlay must be drawn here (no viewports exist).
        if (!g_app.IsPaperSpace())
            g_modelRenderer.DrawSelectionOverlay(ctx);
    }

    // Cached passes: only rebuild when the EntityBook changes (structurally),
    // or when explicitly requested (REGEN).
    {
        const uint64_t rev = g_app.GetEntityBook().GetRevision();
        if (g_forceRendererRebuild || rev != g_lastEntityBookRevision)
        {
            g_lastEntityBookRevision = rev;
            g_forceRendererRebuild = false;
            g_paperRenderer.MarkDirty();
            g_modelRenderer.MarkDirty();

            const EntityBook& book = g_app.GetEntityBook();
            const size_t hl = CountHighlightedLines(book);
            VKLog::Logf(VKLog::Core, VKLog::Level::Trace,
                "[RENDER] Rebuild caches. rev=%llu reason=%s highlightedLines=%zu",
                (unsigned long long)rev,
                book.GetLastTouchReason(),
                hl);
        }
    }

    // Paper pass: page borders, viewport frames, etc.
    g_paperRenderer.Redraw(ctx);

    // Model pass:
    // In MODEL space, render the model scene (Scene/User) to the full window.
    // In PAPER space, the model scene is drawn only through viewports (scissored) below.
    if (!g_app.IsPaperSpace())
    {
        g_modelRenderer.Redraw(ctx);
    }

    // Viewport creation draft (paper space): draw a live rectangle while dragging.
    if (g_app.IsPaperSpace() && g_app.IsViewportCreateArmed() && g_app.HasViewportCreateDraft())
    {
        const glm::vec2 p0 = g_app.GetViewportCreateP0In();
        const glm::vec2 p1 = g_app.GetViewportCreateP1In();

        const glm::vec2 mn(std::min(p0.x, p1.x), std::min(p0.y, p1.y));
        const glm::vec2 mx(std::max(p0.x, p1.x), std::max(p0.y, p1.y));

        const glm::vec3 a(mn.x, mn.y, 0.0f);
        const glm::vec3 b(mx.x, mn.y, 0.0f);
        const glm::vec3 c(mx.x, mx.y, 0.0f);
        const glm::vec3 d(mn.x, mx.y, 0.0f);

        Entity e;
        e.type = EntityType::Line;
        e.tag = EntityTag::Hud;
        e.screenSpace = false;
        e.drawOrder = 100000; // on top
        e.layerId = g_app.GetLayerTable().CurrentLayerId();
        e.colorByLayer = false;
        e.linetypeByLayer = true;
        e.line.color = glm::vec4(1, 1, 1, 1);
        e.line.thickness = 1.0f;

        g_overlayRenderer.BeginFrame();
        e.ID = 0xFFF00001; e.line.start = a; e.line.end = b; g_overlayRenderer.Submit(e);
        e.ID = 0xFFF00002; e.line.start = b; e.line.end = c; g_overlayRenderer.Submit(e);
        e.ID = 0xFFF00003; e.line.start = c; e.line.end = d; g_overlayRenderer.Submit(e);
        e.ID = 0xFFF00004; e.line.start = d; e.line.end = a; g_overlayRenderer.Submit(e);
        g_overlayRenderer.Draw(ctx);
    }

    // Paper-space viewports: draw MODEL entities through each viewport camera, clipped.
    // Debug clip rects for this frame (client px, top-left origin). Used to draw scissor overlay.
    std::vector<DebugClipRectTL> frameViewportClipRects;
    DebugClipRectTL frameActiveViewportClipRect;
    bool hasFrameActiveViewportClipRect = false;

    if (g_app.IsPaperSpace())
    {
        // Use GL viewport (framebuffer) dimensions for scissor math.
        // This avoids drift on DPI-scaled systems.
        // glScissor operates in framebuffer pixels; our viewport corners are in client pixels.
        // Convert client->framebuffer using the ratio derived from the ortho projection.
        float clientW = 1.0f, clientH = 1.0f;
        ExtractViewportWH_FromOrthoYDown(ctx.projection, clientW, clientH);

        GLint glvp[4];
        glGetIntegerv(GL_VIEWPORT, glvp);
        const int   fbX0 = glvp[0];
        const int   fbY0 = glvp[1];
        const float fbW = (float)glvp[2];
        const float fbH = (float)glvp[3];

        const float sx = (clientW > 0.0f) ? (fbW / clientW) : 1.0f;
        const float sy = (clientH > 0.0f) ? (fbH / clientH) : 1.0f;

        for (const auto& vp : g_app.GetViewports())
        {
            if (!vp.contentsVisible)
                continue;

            // Viewport rect in client pixels (top-left origin)
            const glm::vec2 mn(std::min(vp.p0In.x, vp.p1In.x), std::min(vp.p0In.y, vp.p1In.y));
            const glm::vec2 mx(std::max(vp.p0In.x, vp.p1In.x), std::max(vp.p0In.y, vp.p1In.y));

            const glm::vec2 tl = g_app.PaperToClientPx(mn);
            const glm::vec2 br = g_app.PaperToClientPx(mx);

            const float x0 = std::min(tl.x, br.x);
            const float x1 = std::max(tl.x, br.x);
            const float y0 = std::min(tl.y, br.y);
            const float y1 = std::max(tl.y, br.y);

            // Convert client edges -> framebuffer edges, then snap symmetrically.
            // NOTE: glScissor expects framebuffer pixels with bottom-left origin.
            const float inset = (float)g_viewportScissorInsetPx;
            const float x0i = x0 + inset;
            const float x1i = x1 - inset;
            const float y0i = y0 + inset;
            const float y1i = y1 - inset;

            if (g_drawViewportClipDebug)
            {
                DebugClipRectTL r; r.x0 = x0i; r.y0 = y0i; r.x1 = x1i; r.y1 = y1i;
                frameViewportClipRects.push_back(r);
            }

            const float leftF = x0i * sx;
            const float rightF = x1i * sx;
            const float topF = y0i * sy;
            const float bottomF = y1i * sy;

            const int left = (int)std::floor(leftF);
            const int right = (int)std::ceil(rightF);
            const int top = (int)std::floor(topF);
            const int bottom = (int)std::ceil(bottomF);

            const int scX = fbX0 + left;
            const int scW = std::max(0, right - left);
            const int scY = fbY0 + std::max(0, (int)fbH - bottom); // bottom-left origin for glScissor
            const int scH = std::max(0, bottom - top);

            if (scW <= 0 || scH <= 0)
                continue;

            glEnable(GL_SCISSOR_TEST);
            glScissor(scX, scY, scW, scH);

            // --------------------------------------------------------------------
            // NEW: draw a dark "model window" background inside the viewport scissor
            // so white model-space lines are visible immediately in paper-space.
            // (Draw using paper-space ctx because vp.p0In/p1In are paper inches.)
            // --------------------------------------------------------------------
            {
                const glm::vec2 mnIn(std::min(vp.p0In.x, vp.p1In.x), std::min(vp.p0In.y, vp.p1In.y));
                const glm::vec2 mxIn(std::max(vp.p0In.x, vp.p1In.x), std::max(vp.p0In.y, vp.p1In.y));

                Entity vpBg{};
                vpBg.ID = 0; // immediate-only
                vpBg.type = EntityType::SolidRect;
                vpBg.tag = EntityTag::Grid;
                vpBg.drawOrder = -900;   // above white page (-1000), below everything else
                vpBg.screenSpace = false;

                // Slightly above the page rect Z (-0.5f) so it covers the white sheet.
                vpBg.solidRect.min = glm::vec3(mnIn.x, mnIn.y, -0.49f);
                vpBg.solidRect.max = glm::vec3(mxIn.x, mxIn.y, -0.49f);

                // Viewport background (separate from paper + model space)
                vpBg.solidRect.color = g_renderStyle.viewportBackground;

                g_overlayRenderer.BeginFrame();
                g_overlayRenderer.Submit(vpBg);
                g_overlayRenderer.Draw(ctx);
            }

            RenderContext vpCtx = ctx;
            vpCtx.model = glm::mat4(1.0f);
            vpCtx.view = g_app.GetViewportViewMatrix(vp);

            g_modelRenderer.Redraw(vpCtx);

            // World-space cursor overlays (e.g. LINE jig) should draw through the ACTIVE viewport,
            // clipped to the viewport rectangle.
            if (vp.active)
            {
                g_overlayRenderer.BeginFrame();
                const auto& ents = g_app.GetEntityBook().GetEntities();
                for (const Entity& e : ents)
                {
                    if (e.screenSpace) continue;
                    if (e.tag != EntityTag::Cursor) continue;
                    g_overlayRenderer.Submit(e);
                }
                g_overlayRenderer.Draw(vpCtx);
                // Selection overlay should be drawn last so it is never overwritten.
                g_modelRenderer.DrawSelectionOverlay(vpCtx);
            }
            else
            {
                // No cursor overlay in this viewport; still draw selection overlay on top.
                g_modelRenderer.DrawSelectionOverlay(vpCtx);
            }

            glDisable(GL_SCISSOR_TEST);
        }
    }

    // World-space cursor overlays when NOT drawing through an active viewport.
    // - Model space: draw over the whole window.
    // - Paper space with no active viewport: draw in paper coordinates.
    if (!g_app.IsPaperSpace() || !g_app.HasActiveViewport())
    {
        g_overlayRenderer.BeginFrame();
        const auto& ents = g_app.GetEntityBook().GetEntities();
        for (const Entity& e : ents)
        {
            if (e.screenSpace) continue;
            if (e.tag != EntityTag::Cursor) continue;
            g_overlayRenderer.Submit(e);
        }
        g_overlayRenderer.Draw(ctx);
    }

    // HUD / overlays: immediate mode
    float w = 1.0f, h = 1.0f;
    ExtractViewportWH_FromOrthoYDown(ctx.projection, w, h);

    RenderContext hudCtx = ctx;
    hudCtx.model = glm::mat4(1.0f);
    hudCtx.view = glm::mat4(1.0f);
    hudCtx.projection = glm::ortho(0.0f, w, 0.0f, h, -1.0f, 1.0f);

    // HUD (unclipped)
    g_overlayRenderer.BeginFrame();
    {
        const auto& entities = g_app.GetEntityBook().GetEntities();
        for (const Entity& e : entities)
        {
            if (!e.screenSpace) continue;
            if (e.tag == EntityTag::Cursor) continue;
            g_overlayRenderer.Submit(e);
        }

        // Debug: draw the exact scissor rectangles used for viewport clipping.
        // Red = per-viewport model scissor; Cyan = active viewport scissor for cursor/grips.
        if (g_drawViewportClipDebug)
        {
            auto submitRectTL = [&](const DebugClipRectTL& r, const glm::vec4& color, int baseId)
                {
                    // Convert client top-left (Y-down) -> HUD bottom-left (Y-up)
                    const float x0 = r.x0;
                    const float x1 = r.x1;
                    const float yTop = h - r.y0;
                    const float yBot = h - r.y1;

                    Entity e;
                    e.type = EntityType::Line;
                    e.tag = EntityTag::Hud;
                    e.screenSpace = true;
                    e.drawOrder = 250000;
                    e.layerId = g_app.GetLayerTable().CurrentLayerId();
                    e.colorByLayer = false;
                    e.linetypeByLayer = true;
                    e.line.color = color;
                    e.line.thickness = 1.0f;

                    e.ID = baseId + 0;
                    e.line.start = glm::vec3(x0, yBot, 0.0f);
                    e.line.end = glm::vec3(x1, yBot, 0.0f);
                    g_overlayRenderer.Submit(e);

                    e.ID = baseId + 1;
                    e.line.start = glm::vec3(x1, yBot, 0.0f);
                    e.line.end = glm::vec3(x1, yTop, 0.0f);
                    g_overlayRenderer.Submit(e);

                    e.ID = baseId + 2;
                    e.line.start = glm::vec3(x1, yTop, 0.0f);
                    e.line.end = glm::vec3(x0, yTop, 0.0f);
                    g_overlayRenderer.Submit(e);

                    e.ID = baseId + 3;
                    e.line.start = glm::vec3(x0, yTop, 0.0f);
                    e.line.end = glm::vec3(x0, yBot, 0.0f);
                    g_overlayRenderer.Submit(e);
                };

            int id = 0x7FF00000;
            for (const auto& r : frameViewportClipRects)
            {
                submitRectTL(r, glm::vec4(1, 0, 0, 1), id);
                id += 8;
            }

            if (hasFrameActiveViewportClipRect)
                submitRectTL(frameActiveViewportClipRect, glm::vec4(0, 1, 1, 1), 0x7FF10000);
        }
    }
    g_overlayRenderer.Draw(hudCtx);

    // Cursor / grips (optionally clipped to active viewport when in paper space)
    bool clipCursor = false;
    int scX = 0, scY = 0, scW = 0, scH = 0;

    if (g_app.IsPaperSpace() && g_app.HasActiveViewport())
    {
        const auto* vp = g_app.GetActiveViewport();
        if (vp)
        {
            const glm::vec2 mn(std::min(vp->p0In.x, vp->p1In.x), std::min(vp->p0In.y, vp->p1In.y));
            const glm::vec2 mx(std::max(vp->p0In.x, vp->p1In.x), std::max(vp->p0In.y, vp->p1In.y));

            const glm::vec2 tl = g_app.PaperToClientPx(mn);
            const glm::vec2 br = g_app.PaperToClientPx(mx);

            const float x0 = std::min(tl.x, br.x);
            const float x1 = std::max(tl.x, br.x);
            const float y0 = std::min(tl.y, br.y);
            const float y1 = std::max(tl.y, br.y);

            // DPI-correct cursor/grips clipping: convert client pixels -> framebuffer pixels.
            float clientW = 1.0f, clientH = 1.0f;
            ExtractViewportWH_FromOrthoYDown(ctx.projection, clientW, clientH);

            GLint glvp[4];
            glGetIntegerv(GL_VIEWPORT, glvp);
            const int   fbX0 = glvp[0];
            const int   fbY0 = glvp[1];
            const float fbW = (float)glvp[2];
            const float fbH = (float)glvp[3];

            const float sx = (clientW > 0.0f) ? (fbW / clientW) : 1.0f;
            const float sy = (clientH > 0.0f) ? (fbH / clientH) : 1.0f;

            const float inset = (float)g_viewportScissorInsetPx;
            const float x0i = x0 + inset;
            const float x1i = x1 - inset;
            const float y0i = y0 + inset;
            const float y1i = y1 - inset;

            if (g_drawViewportClipDebug)
            {
                frameActiveViewportClipRect.x0 = x0i;
                frameActiveViewportClipRect.y0 = y0i;
                frameActiveViewportClipRect.x1 = x1i;
                frameActiveViewportClipRect.y1 = y1i;
                hasFrameActiveViewportClipRect = true;
            }

            const float leftF = x0i * sx;
            const float rightF = x1i * sx;
            const float topF = y0i * sy;
            const float bottomF = y1i * sy;

            const int left = (int)std::floor(leftF);
            const int right = (int)std::ceil(rightF);
            const int top = (int)std::floor(topF);
            const int bottom = (int)std::ceil(bottomF);

            scX = fbX0 + left;
            scW = std::max(0, right - left);
            scY = fbY0 + std::max(0, (int)fbH - bottom);
            scH = std::max(0, bottom - top);

            clipCursor = (scW > 0 && scH > 0);
        }
    }

    if (clipCursor)
    {
        glEnable(GL_SCISSOR_TEST);
        glScissor(scX, scY, scW, scH);
    }

    g_overlayRenderer.BeginFrame();
    {
        const auto& entities = g_app.GetEntityBook().GetEntities();
        for (const Entity& e : entities)
        {
            if (!e.screenSpace) continue;
            if (e.tag != EntityTag::Cursor) continue;
            g_overlayRenderer.Submit(e);
        }
    }
    g_overlayRenderer.Draw(hudCtx);

    if (clipCursor)
        glDisable(GL_SCISSOR_TEST);

    // Update Properties window contents (if visible)
    if (g_propWnd.IsVisible())
    {
        std::vector<std::pair<std::string, std::string>> rows;

        std::vector<int> selVps;
        if (g_app.IsPaperSpace() && g_app.GetSelectedViewports(selVps))
        {
            const std::size_t qty = selVps.size();
            g_propWnd.SetTypeFilterItems(std::vector<std::pair<std::string, int>>{ { "Viewport (" + std::to_string((unsigned long long)qty) + " )", -1 } });

            // Compute common/shared viewport properties across the selection.
            const Application::Viewport* first = g_app.GetViewportByIndex(selVps[0]);
            if (first)
            {
                uint32_t commonLayerId = first->borderLayerId;
                float commonScale = first->modelUnitsPerPaperUnit;
                float commonZoom = first->modelZoom;
                bool commonLocked = first->locked;

                bool layerMixed = false, scaleMixed = false, zoomMixed = false, lockedMixed = false;
                int activeCount = 0;

                for (int vpIdx : selVps)
                {
                    const auto* vp = g_app.GetViewportByIndex(vpIdx);
                    if (!vp) continue;
                    if (vp->active) activeCount++;

                    if (vp->borderLayerId != commonLayerId) layerMixed = true;
                    if (std::abs(vp->modelUnitsPerPaperUnit - commonScale) > 1e-6f) scaleMixed = true;
                    if (std::abs(vp->modelZoom - commonZoom) > 1e-6f) zoomMixed = true;
                    if (vp->locked != commonLocked) lockedMixed = true;
                }

                rows.push_back({ "Type", "Viewport" });
                rows.push_back({ "Qty", std::to_string((unsigned long long)qty) });

                // Layer / ByLayer style for the border
                if (!layerMixed)
                {
                    const auto* L = g_app.GetLayerTable().Find(commonLayerId);
                    rows.push_back({ "Layer", L ? L->name : std::to_string(commonLayerId) });
                    rows.push_back({ "Color", "ByLayer" });
                    rows.push_back({ "Linetype", "ByLayer" });
                }
                else
                {
                    rows.push_back({ "Layer", "Mixed" });
                    rows.push_back({ "Color", "ByLayer" });
                    rows.push_back({ "Linetype", "ByLayer" });
                }

                // Scale: model units per paper inch
                if (!scaleMixed)
                {
                    rows.push_back({ "Scale (model per 1 paper unit)", std::to_string(commonScale) });
                    if (commonScale > 0.000001f)
                        rows.push_back({ "Paper units per model unit", std::to_string(1.0f / commonScale) });
                }
                else
                {
                    rows.push_back({ "Scale (model per 1 paper unit)", "Mixed" });
                }

                // Viewport camera
                if (!zoomMixed)
                    rows.push_back({ "Zoom", std::to_string(commonZoom) });
                else
                    rows.push_back({ "Zoom", "Mixed" });

                if (!lockedMixed)
                    rows.push_back({ "Locked", commonLocked ? "Yes" : "No" });
                else
                    rows.push_back({ "Locked", "Mixed" });

                rows.push_back({ "Active (count)", std::to_string((unsigned long long)activeCount) });

                // If exactly one viewport is selected, include ids + bounds for convenience.
                if (qty == 1)
                {
                    rows.push_back({ "Id", std::to_string(first->id) });
                    rows.push_back({ "P0 (paper)", "(" + std::to_string(first->p0In.x) + ", " + std::to_string(first->p0In.y) + ")" });
                    rows.push_back({ "P1 (paper)", "(" + std::to_string(first->p1In.x) + ", " + std::to_string(first->p1In.y) + ")" });
                }
            }
        }
        else if (g_app.IsPaperSpace() && g_app.HasActiveViewport())
        {
            const auto* vp = g_app.GetActiveViewport();
            if (vp)
            {
                g_propWnd.SetTypeFilterItems(std::vector<std::pair<std::string, int>>{ { "Viewport (active)", -1 } });
                rows.push_back({ "Type", "Viewport" });
                rows.push_back({ "Id", std::to_string(vp->id) });
                const auto* L = g_app.GetLayerTable().Find(vp->borderLayerId);
                rows.push_back({ "Layer", L ? L->name : std::to_string(vp->borderLayerId) });
                rows.push_back({ "Color", "ByLayer" });
                rows.push_back({ "Linetype", "ByLayer" });
                rows.push_back({ "Scale (model per 1 paper unit)", std::to_string(vp->modelUnitsPerPaperUnit) });
                rows.push_back({ "Zoom", std::to_string(vp->modelZoom) });
                rows.push_back({ "Locked", vp->locked ? "Yes" : "No" });
                rows.push_back({ "Active", "Yes" });
            }
        }
        else if (g_app.HasSelection())
        {
            // Build filter items for mixed selections (Properties window combo box).
            const auto typeCounts = g_app.GetSelectionTypeCounts();
            const std::size_t total = g_app.GetSelectedCount();

            std::vector<std::pair<std::string, int>> filterItems;
            if (typeCounts.size() > 1)
            {
                filterItems.push_back({ "Mixed (" + std::to_string((unsigned long long)total) + ")", -1 });
                for (const auto& kv : typeCounts)
                {
                    filterItems.push_back({
                        std::string(EntityTypeName(kv.first)) + " (" + std::to_string((unsigned long long)kv.second) + ")",
                        (int)kv.first
                        });
                }
            }
            else if (typeCounts.size() == 1)
            {
                filterItems.push_back({
                    std::string(EntityTypeName(typeCounts[0].first)) + " (" + std::to_string((unsigned long long)total) + ")",
                    (int)typeCounts[0].first
                    });
            }
            else
            {
                filterItems.push_back({ "Selection (" + std::to_string((unsigned long long)total) + ")", -1 });
            }

            g_propWnd.SetTypeFilterItems(filterItems);

            const int filterTag = g_propWnd.GetTypeFilterTag();
            const std::optional<EntityType> filterType = (filterTag >= 0) ? std::optional<EntityType>((EntityType)filterTag) : std::nullopt;

            const std::vector<std::size_t> sel = g_app.GetSelectedIndicesByType(filterType);

            // Shared-only properties across the current view selection.
            if (filterType.has_value())
                rows.push_back({ "Type", EntityTypeName(*filterType) });
            else if (typeCounts.size() > 1)
                rows.push_back({ "Type", "Mixed" });
            else if (typeCounts.size() == 1)
                rows.push_back({ "Type", EntityTypeName(typeCounts[0].first) });
            else
                rows.push_back({ "Type", "Selection" });

            rows.push_back({ "Count", std::to_string((unsigned long long)sel.size()) });

            const auto& ents = g_app.GetEntityBook().GetEntities();

            // Layer common check
            uint32_t commonLayer = LayerTable::kInvalidLayerId;
            bool layerFirst = true;
            for (std::size_t idx : sel)
            {
                if (idx >= ents.size()) continue;
                const Entity& e = ents[idx];
                if (e.tag != EntityTag::Scene) continue;

                if (layerFirst)
                {
                    layerFirst = false;
                    commonLayer = e.layerId;
                }
                else if (e.layerId != commonLayer)
                {
                    commonLayer = LayerTable::kInvalidLayerId;
                    break;
                }
            }

            if (!layerFirst && commonLayer != LayerTable::kInvalidLayerId)
            {
                const auto* L = g_app.GetLayerTable().Find(commonLayer);
                rows.push_back({ "Layer", L ? L->name : std::to_string(commonLayer) });
            }
            else
            {
                rows.push_back({ "Layer", "(mixed)" });
            }

            // Color common check (Line/Text only)
            bool colorFirst = true;
            bool commonByLayer = true;
            glm::vec4 commonCol(1, 1, 1, 1);
            bool colorMixed = false;

            for (std::size_t idx : sel)
            {
                if (idx >= ents.size()) continue;
                const Entity& e = ents[idx];
                if (e.tag != EntityTag::Scene) continue;

                if (e.type != EntityType::Line && e.type != EntityType::Text)
                {
                    colorMixed = true;
                    break;
                }

                const bool eByLayer = e.colorByLayer;
                const glm::vec4 eCol = (e.type == EntityType::Line) ? e.line.color : e.text.color;

                if (colorFirst)
                {
                    colorFirst = false;
                    commonByLayer = eByLayer;
                    commonCol = eCol;
                }
                else
                {
                    if (commonByLayer != eByLayer || commonCol != eCol)
                    {
                        colorMixed = true;
                        break;
                    }
                }
            }

            if (!colorFirst && !colorMixed)
            {
                if (commonByLayer)
                    rows.push_back({ "Color", "ByLayer" });
                else
                {
                    char buf[32];
                    COLORREF cr = RGB((int)(commonCol.r * 255.0f + 0.5f), (int)(commonCol.g * 255.0f + 0.5f), (int)(commonCol.b * 255.0f + 0.5f));
                    sprintf_s(buf, "#%02X%02X%02X", (unsigned)GetRValue(cr), (unsigned)GetGValue(cr), (unsigned)GetBValue(cr));
                    rows.push_back({ "Color", buf });
                }
            }
            else
            {
                rows.push_back({ "Color", "(mixed)" });
            }

            // If narrowed to a single Line entity, show extra detail.
            if (sel.size() == 1)
            {
                const std::size_t idx = sel[0];
                if (idx < ents.size())
                {
                    const Entity& e = ents[idx];
                    rows.push_back({ "Id", std::to_string((unsigned long long)e.ID) });

                    if (e.type == EntityType::Line)
                    {
                        const glm::vec3 a = e.line.start;
                        const glm::vec3 b = e.line.end;
                        const float dx = b.x - a.x;
                        const float dy = b.y - a.y;
                        const float len = std::sqrt(dx * dx + dy * dy);

                        char bufA[128], bufB[128], bufL[64];
                        sprintf_s(bufA, "(%.3f, %.3f)", a.x, a.y);
                        sprintf_s(bufB, "(%.3f, %.3f)", b.x, b.y);
                        sprintf_s(bufL, "%.3f", len);

                        rows.push_back({ "Start", bufA });
                        rows.push_back({ "End", bufB });
                        rows.push_back({ "Length", bufL });
                    }
                }
            }
        }
        else if (g_app.GetGripTargetEntityId() != 0)
        {
            rows.push_back({ "Type", "Entity (grip hover)" });
            rows.push_back({ "Id", std::to_string((unsigned long long)g_app.GetGripTargetEntityId()) });
        }
        else
        {
            rows.push_back({ "Selection", "(none)" });
        }

        g_propWnd.SetProperties(rows);
    }

    // FIX: don't leak DC handles; GetDC must be released
    HDC hdc = GetDC(hwnd);
    SwapBuffers(hdc);
    ReleaseDC(hwnd, hdc);
}


// ------------------------------------------------------------
// Win32 Proc
// ------------------------------------------------------------

// ------------------------------------------------------------
// Cursor hide/show for client area (robust)
// ------------------------------------------------------------
static bool g_cursorHidden = false;
static bool g_trackingMouseLeave = false;

static void HideSystemCursor()
{
    if (g_cursorHidden)
        return;

    while (ShowCursor(FALSE) >= 0) {}
    g_cursorHidden = true;
}

static void ShowSystemCursor()
{
    if (!g_cursorHidden)
        return;

    while (ShowCursor(TRUE) < 0) {}
    g_cursorHidden = false;
}

static void BeginTrackMouseLeave(HWND hwnd)
{
    if (g_trackingMouseLeave)
        return;

    TRACKMOUSEEVENT tme{};
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = hwnd;

    if (TrackMouseEvent(&tme))
        g_trackingMouseLeave = true;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        InitOpenGL(hwnd);
        return 0;

    case WM_SIZE:
        OnResize((int)LOWORD(lParam), (int)HIWORD(lParam));
        RequestRedraw(true);
        return 0;

    case WM_SETCURSOR:
    {
        if (LOWORD(lParam) == HTCLIENT)
        {
			// If a paper-space viewport is active, show the Win32 arrow cursor when the mouse
			// is outside the viewport rectangle; otherwise hide it and use our crosshairs.
			if (g_app.IsPaperSpace() && g_app.HasActiveViewport() && !g_app.MouseInActiveViewport())
			{
				ShowSystemCursor();
				SetCursor(LoadCursor(NULL, IDC_ARROW));
				return TRUE;
			}

			HideSystemCursor();
			SetCursor(NULL);
			return TRUE;
        }
        else
        {
            ShowSystemCursor();
        }
        break;
    }

    case WM_MOUSEMOVE:
    {
		// Decide cursor policy based on whether the active viewport contains the mouse.
		// We refresh the cached mouse world coords here so WM_SETCURSOR has up-to-date info.
        BeginTrackMouseLeave(hwnd);

        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        g_app.SetMouseClient(x, y);
		g_app.RefreshMouseWorldCache();
		if (g_app.IsPaperSpace() && g_app.HasActiveViewport() && !g_app.MouseInActiveViewport())
			ShowSystemCursor();
		else
			HideSystemCursor();
        if (g_app.IsMousePanning())
            g_app.UpdateMousePan(x, y);
        if (g_app.IsMarqueeSelecting())
            g_app.UpdateMarqueeDrag(x, y);

        // Viewport creation:
        // - drag-create updates while dragging
        // - VIEWPORT command (two-click) updates continuously after the first click
        if (g_app.IsViewportCreateArmed())
            g_app.UpdateViewportCreateDrag();

        RequestRedraw();
        return 0;
    }

    case WM_MOUSELEAVE:
        g_trackingMouseLeave = false;
        ShowSystemCursor();
        return 0;

    case WM_NCMOUSEMOVE:
        g_trackingMouseLeave = false;
        ShowSystemCursor();
        break;

    case WM_KILLFOCUS:
    case WM_ACTIVATEAPP:
        if (msg == WM_KILLFOCUS || wParam == FALSE)
        {
            g_trackingMouseLeave = false;
            ShowSystemCursor();
        }
        break;

    case WM_MOUSEWHEEL:
    {
        POINT p;
        p.x = GET_X_LPARAM(lParam);
        p.y = GET_Y_LPARAM(lParam);
        ScreenToClient(hwnd, &p);

        const int wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        const float step = 1.10f;
        const float zoomFactor = (wheelDelta > 0) ? step : (1.0f / step);

        g_app.ZoomAtClient(p.x, p.y, zoomFactor);
        RequestRedraw();
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        SetCapture(hwnd);
        g_app.SetMouseClient(x, y);
        g_app.OnLeftDown(hwnd);
        RequestRedraw();
        return 0;
    }


    case WM_LBUTTONDBLCLK:
    {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        g_app.SetMouseClient(x, y);
        g_app.OnLeftDoubleClick(hwnd);
        RequestRedraw();
        return 0;
    }

    case WM_LBUTTONUP:
        ReleaseCapture();
        g_app.OnLeftUp(hwnd);
        RequestRedraw();
        return 0;

    case WM_RBUTTONDOWN:
    {
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        // Shift + RMB: one-shot OSNAP context menu (scaffolding)
        if (shift)
        {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1001, L"None");
            AppendMenuW(menu, MF_STRING, 1002, L"Endpoint");
            AppendMenuW(menu, MF_STRING, 1003, L"Nearpoint");
            AppendMenuW(menu, MF_STRING, 1004, L"Intersection");
            AppendMenuW(menu, MF_STRING, 1005, L"Perpendicular");
            AppendMenuW(menu, MF_STRING, 1006, L"Centerpoint");
            AppendMenuW(menu, MF_STRING, 1007, L"Insertionpoint");
            AppendMenuW(menu, MF_STRING, 1008, L"Tangent");
            AppendMenuW(menu, MF_STRING, 1009, L"Grid");

            POINT pt;
            GetCursorPos(&pt);
            const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);

            switch (cmd)
            {
            case 1001: g_app.SetOneShotSnap(Application::SnapMode::None); break;
            case 1002: g_app.SetOneShotSnap(Application::SnapMode::Endpoint); break;
            case 1003: g_app.SetOneShotSnap(Application::SnapMode::Nearpoint); break;
            case 1004: g_app.SetOneShotSnap(Application::SnapMode::Intersection); break;
            case 1005: g_app.SetOneShotSnap(Application::SnapMode::Perpendicular); break;
            case 1006: g_app.SetOneShotSnap(Application::SnapMode::Centerpoint); break;
            case 1007: g_app.SetOneShotSnap(Application::SnapMode::Insertionpoint); break;
            case 1008: g_app.SetOneShotSnap(Application::SnapMode::Tangent); break;
            case 1009: g_app.SetOneShotSnap(Application::SnapMode::Grid); break;
            default: break;
            }

            return 0;
        }

        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        SetCapture(hwnd);
        g_app.BeginMousePan(x, y);
        return 0;
    }

    case WM_RBUTTONUP:
        ReleaseCapture();
        g_app.EndMousePan();
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    {
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        if (wParam == VK_F2) {
            g_cmdWnd.Show(!g_cmdWnd.IsVisible());
            return 0;
        }

        if (wParam == VK_F8)
        {
            g_app.ToggleOrtho();
            if (g_cmdWnd.IsVisible())
            {
                g_cmdWnd.AppendText(g_app.IsOrthoEnabled() ? L"Ortho: ON\r\n" : L"Ortho: OFF\r\n");
            }
            return 0;
        }

        // Alt+P: Run a Python script (embedded).
        if (!ctrl && alt && !shift && (wParam == 'P' || wParam == 'p'))
        {
            OpenAndRunPythonScript(hwnd);
            return 0;
        }

        // Enter in the client area: if an interactive command is active (e.g., ERASE), finish it.
        if (wParam == VK_RETURN)
        {
            if (g_app.HasPendingCommand())
            {
                g_app.HandleCmdWindowLine(std::string(), &g_cmdWnd); // empty line == finish
                g_cmdWnd.FocusInput();
                return 0;
            }
        }

        // While an interactive command is active (e.g., ERASE), allow W/C from the viewport
// to switch Window/Crossing without typing into the command window.
        if (g_app.HasPendingCommand() && !ctrl && !alt && !shift)
        {
            if (wParam == 'W' || wParam == 'w')
            {
                g_app.HandleCmdWindowLine("W", &g_cmdWnd);
                RequestRedraw();
                return 0;
            }
            if (wParam == 'C' || wParam == 'c')
            {
                g_app.HandleCmdWindowLine("C", &g_cmdWnd);
                RequestRedraw();
                return 0;
            }
        }

        // Command system gets first right-of-way.
        if (g_commands.HandleKeyDown((UINT)wParam, ctrl, alt, shift))
            return 0;

        if (wParam == VK_ESCAPE)
        {
            g_app.CancelInteractions();
            return 0;
        }

        
        // Properties window
        if (ctrl && !alt && !shift && wParam == '1')
        {
            g_propWnd.Toggle();
            return 0;
        }

        // Layers window
        if (ctrl && !alt && !shift && wParam == '2')
        {
            g_layersWnd.Toggle();
            return 0;
        }

switch (wParam)
        {
                case 'V':
            if (!ctrl && !alt && !shift && g_app.IsPaperSpace()) { g_app.BeginViewportCreate(); return 0; }
            break;
        case 'L':
            if (!ctrl && !alt && !shift && g_app.IsPaperSpace() && g_app.HasActiveViewport()) { g_app.ToggleActiveViewportLock(); return 0; }
            break;

case 'S': g_app.ToggleSelectionMode(); return 0;
        case 'G': g_app.ToggleGrid();          return 0;

        case 'E':
            if (ctrl && !alt) {                // optional: block Alt+Ctrl+E
                g_app.BeginEraseCommand(&g_cmdWnd);   // <-- interactive erase (turns hover on)
                RequestRedraw();
                return 0;
            }
            break;

        case VK_LEFT:  g_app.PanByPixels(-40, 0); return 0;
        case VK_RIGHT: g_app.PanByPixels(40, 0);  return 0;
        case VK_UP:    g_app.PanByPixels(0, -40); return 0;
        case VK_DOWN:  g_app.PanByPixels(0, 40);  return 0;
        }
        break;
    }

    case WM_DESTROY:
        ShowSystemCursor();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// ------------------------------------------------------------
// Entry (ANSI)
// ------------------------------------------------------------
int WINAPI WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE,
    _In_ LPSTR,
    _In_ int nCmdShow)
{
    // Ensure a console is visible even when running as
    // a Windows subsystem app (useful for debug prints).
#if _DEBUG
    AllocConsole();
    FILE* __fp_out = nullptr;
    freopen_s(&__fp_out, "CONOUT$", "w", stdout);
    FILE* __fp_err = nullptr;
    freopen_s(&__fp_err, "CONOUT$", "w", stderr);
#endif

    const char* className = "VDrawDemo13WndClass";

    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = className;
    RegisterClassExA(&wc);

    const int width = 1280;
    const int height = 720;

    HWND hwnd = CreateWindowExA(
        0,
        className,
        "VectorKernel-Starter",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        width, height,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    if (!hwnd)
        return 0;

    g_app.Init(hwnd, width, height);

    // ------------------------------------------------------------
    // Commands + shortcuts
    // ------------------------------------------------------------
    // First command: ERASE
    // Default shortcut: Delete (customizable via shortcuts.ini)
    g_commands.Register(
        "ERASE",
        "Erase entities (interactive when typed in the Command window)",
        KeyChord{ false, false, false, VK_DELETE },
        []()
        {
            g_app.BeginEraseCommand(&g_cmdWnd);
            RequestRedraw();
        });

    // LINE (interactive point-entry command; type LINE in the Command window)
    g_commands.Register(
        "LINE",
        "Draw a line (interactive point entry: first node / second node)",
        KeyChord{ false, false, false, 0 },
        []() { /* interactive when typed; see CmdWindow callback */ });
    // Help / command list (F1)
    g_commands.Register(
        "HELP",
        "List available commands",
        KeyChord{ false, false, false, VK_F1 },
        []()
        {
            g_cmdWnd.Show(true);
            g_cmdWnd.AppendText(L"Commands:\r\n");
            for (const auto& kv : g_commands.GetCommandList())
            {
                std::wstring w;
                w += L"  ";
                w += std::wstring(kv.first.begin(), kv.first.end());
                w += L"  -  ";
                w += std::wstring(kv.second.begin(), kv.second.end());
                w += L"\r\n";
                g_cmdWnd.AppendText(w);
            }
        });



    // Toggle paper space (Ctrl+Shift+P)
    g_commands.Register(
        "TOGGLE_PAPERSPACE",
        "Toggle paper space mode (page preview for plotting)",
        KeyChord{ true, false, true, 'P' },
        []() { g_app.TogglePaperSpaceMode(); });

    // Quick PDF export (Ctrl+Shift+E) -> writes plot.pdf in the working directory.
    // (You can later replace this with a Save As... dialog.)
    g_commands.Register(
        "EXPORT_PDF",
        "Export current paper-space drawing to plot.pdf",
        KeyChord{ true, false, true, 'E' },
        []()
        {
            if (!g_app.IsPaperSpace())
                return;
            const auto& ents = g_app.GetEntityBook().GetEntities();
            // Convert the app-level page settings to the render-core PageSettings.
            // (Keep RenderCoreLib free of Application namespace/types.)
            const auto& appPage = g_app.GetPageSettings();
            PageSettings page;
            page.widthIn = appPage.widthIn;
            page.heightIn = appPage.heightIn;
            page.marginLeftIn = appPage.marginLeftIn;
            page.marginRightIn = appPage.marginRightIn;
            page.marginTopIn = appPage.marginTopIn;
            page.marginBottomIn = appPage.marginBottomIn;
            page.dpiX = appPage.dpiX;
            page.dpiY = appPage.dpiY;
            PDFPlotter::Write("plot.pdf", ents, page);
        });


    // Save drawing (Ctrl+S)
    g_commands.Register(
        "SAVE",
        "Save drawing to JSON (uses last Save As... path)",
        KeyChord{ true, false, false, 'S' },
        []() { g_app.Save(); });

    // Save drawing as... (Ctrl+Shift+S)
    g_commands.Register(
        "SAVEAS",
        "Save drawing to JSON (prompt for filename)",
        KeyChord{ true, false, true, 'S' },
        []() { g_app.SaveAs(); });

    // Force a repaint (useful when running the stateful renderer without a render loop).
    g_commands.Register(
        "REDRAW",
        "Repaint the current view",
        KeyChord{ false, false, false, 0 },
        []() { RequestRedraw(false); });

    // Force a full rebuild + repaint of cached geometry.
    g_commands.Register(
        "REGEN",
        "Regenerate cached geometry and redraw",
        KeyChord{ false, false, false, 0 },
        []() { RequestRedraw(true); });


    // Layers dialog (Ctrl+2)
    g_commands.Register(
        "LAYER",
        "Open the Layers dialog",
        KeyChord{ true, false, false, '2' },
        []() { g_layersWnd.Toggle(); });

    // Load user overrides (or create a default file on first run).
    g_commands.LoadOrCreateBindingsFile("shortcuts.ini");

    // ------------------------------------------------------------
    // Standalone RichEdit command window (modeless tool window)
    // ------------------------------------------------------------
    g_cmdWnd.Create(hInstance, hwnd, CW_USEDEFAULT, CW_USEDEFAULT, 720, 240);
    g_cmdWndSink.wnd = &g_cmdWnd;
    VKLog::SetSink(&g_cmdWndSink);
    g_cmdWnd.SetIdleQuery([]() { return g_app.IsCmdLineIdle(); });
    g_propWnd.Create(hInstance, hwnd, CW_USEDEFAULT, CW_USEDEFAULT, 380, 520);
    g_propWnd.SetHost(&g_app);
    g_layersWnd.Create(hInstance, hwnd, &g_app, CW_USEDEFAULT, CW_USEDEFAULT, 560, 420);
    g_cmdWnd.SetExecuteCallback([](const std::string& line) {
        // LOG ... (runtime log routing / verbosity / category toggles)
        if (VKLog::HandleLogCommandLine(line, &g_cmdWndSink))
        {
            RequestRedraw();
            return;
        }

        // If an interactive command is active, feed lines into the Application.
        if (g_app.HasPendingCommand())
        {
            g_app.HandleCmdWindowLine(line, &g_cmdWnd);
            return;
        }

        // Start interactive ERASE when typed.
        {
            std::string s = line;
            // cheap trim
            auto notsp = [](unsigned char c) { return !std::isspace(c); };
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), notsp));
            s.erase(std::find_if(s.rbegin(), s.rend(), notsp).base(), s.end());

            std::string u = s;
            std::transform(u.begin(), u.end(), u.begin(), [](unsigned char c) { return (char)std::toupper(c); });
            if (u == "ERASE")
            {
                g_app.BeginEraseCommand(&g_cmdWnd);
                RequestRedraw();
                return;
            }
            if (u == "LINE")
            {
                g_app.BeginLineCommand(&g_cmdWnd);
                RequestRedraw();
                return;
            }
            if (u == "VIEWPORT" || u == "VP")
            {
                g_app.BeginViewportCommand(&g_cmdWnd);
                RequestRedraw();
                return;
            }
        }

        // Otherwise, normal command execution by name (first token).
        if (!g_commands.ExecuteLine(line))
            g_cmdWnd.AppendText(L"Unknown command. Type HELP for a list.\r\n");
        else
            RequestRedraw();
    });


    // Connect renderers to app's EntityBook after app is initialized
    g_paperRenderer.SetEntityBook(&g_app.GetEntityBook());
    g_paperRenderer.SetLayerTable(&g_app.GetLayerTable());

    g_modelRenderer.SetEntityBook(&g_app.GetEntityBook());
    g_modelRenderer.SetLayerTable(&g_app.GetLayerTable());

    auto TAG = [](EntityTag t) { return 1u << (uint32_t)t; };
    // Paper pass: page chrome + viewport frames
    g_paperRenderer.SetTagMask(TAG(EntityTag::Grid) | TAG(EntityTag::Paper) | TAG(EntityTag::PaperUser));
    // Model pass: model entities (scene + user)
    g_modelRenderer.SetTagMask(TAG(EntityTag::Scene) | TAG(EntityTag::User));

    auto last = std::chrono::high_resolution_clock::now();

    MSG msg{};
    while (true)
    {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                return 0;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        // If nothing requested a redraw, sleep until the next message.
        // This keeps the stateful renderer from burning CPU/GPU in a tight loop.
        if (!g_needRedraw)
        {
            WaitMessage();
            continue;
        }
        g_needRedraw = false;

        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<float> dt = now - last;
        last = now;

        g_app.Update(dt.count());
        RenderFrame(hwnd);
    }
}




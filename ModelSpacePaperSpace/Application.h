// Application.h
#pragma once

#include <glm/glm.hpp>

#include "EntityBook.h"
#include "LayerTable.h"
#include "RGeometryTree.h" // BoundingBox + RGeometryTree

#include <optional>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstddef>
#include <cstdint>
#include <string>

#include "PythonScriptRunner.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "PageSettings.h"
// -----------------------------------------------------------------------------
// UI tuning (client-space pixels)
// -----------------------------------------------------------------------------
// Size (in client pixels) of the selection/pick box drawn at the cursor.
// This is also used as the hover/pick query box size (converted to world units).
#ifndef SELECTION_BOX_SIZE_PX
#define SELECTION_BOX_SIZE_PX 12
#endif

// Size (in client pixels) of the blue selection grips
#ifndef GRIP_SIZE_PX
#define GRIP_SIZE_PX 12
#endif

class Application
{
public:
    // -------------------------------------------------------------------------
    // FUNCTIONAL REQUIREMENTS: MODEL SPACE, PAPER SPACE, AND VIEWPORTS
    //
    // This application has TWO distinct drawing spaces:
    //   - Model Space (MS): the main world/model database.
    //   - Paper Space (PS): the page/layout database (units are inches).
    //
    // Viewports are Paper Space objects that *display* Model Space inside a
    // paper-space rectangle. A viewport is not a second copy of geometry.
    //
    // Invariants (do not regress):
    //   1) Entity ownership is explicit:
    //        - MS entities are tagged EntityTag::User or EntityTag::Scene.
    //        - PS entities are tagged EntityTag::Paper / EntityTag::PaperUser.
    //      An entity MUST NOT implicitly appear in the other space.
    //
    //   2) Creation commands add entities ONLY to the active edit target:
    //        - PS + NOT editing through an active viewport => create PaperUser.
    //        - PS + editing through an active viewport     => create User (model).
    //        - MS mode                                     => create User/Scene.
    //
    //   3) Picking / hover / window selection is space-aware and must use the
    //      same coordinate space + transform stack as rendering:
    //        - PS (no active viewport interaction) => pick PS entities.
    //        - Active viewport interaction         => pick MS entities (clipped).
    //        - MS mode                              => pick MS entities.
    //
    //   4) ERASE deletes from the owning database (based on entity tag).
    //
    //   5) Every command MUST end by returning the command line to an idle
    //      prompt state: clear modal state, then show a clean "Command: " prompt.
    // -------------------------------------------------------------------------
    enum class SpaceMode
    {
        Model,
        Paper
    };

    struct PageSettings
    {
        // Defaults: US Letter in inches
        float widthIn = 8.5f;
        float heightIn = 11.0f;

        // Printable margins (inches)
        float marginLeftIn = 0.25f;
        float marginRightIn = 0.25f;
        float marginTopIn = 0.25f;
        float marginBottomIn = 0.25f;

        // Preview/output resolution intent (pixels per inch). X/Y can differ.
        float dpiX = 300.0f;
        float dpiY = 300.0f;
    };


    struct Viewport
    {
        uint32_t id = 0;              // stable id (also used as base for border entities)
        glm::vec2 p0In{};             // first corner (paper units: inches)
        glm::vec2 p1In{};             // second corner (paper units: inches)

        // Scale definition: modelUnitsPerPaperUnit (paper unit = 1 inch in our current paper-space system).
        // Example: 10 means 1 inch in paper space shows 10 model units.
        float modelUnitsPerPaperUnit = 10.0f;

        // Viewport camera (model space)
        glm::vec2 modelCenter{};      // model-space point at the center of the viewport
        float modelZoom = 1.0f;       // magnification within the viewport

        bool locked = false;
        bool active = false;

        // Visibility
        bool contentsVisible = true;  // draw model-space content through this viewport

        // Border styling/visibility via layer controls
        uint32_t borderLayerId = 0;   // LayerTable layer id for the border/frame
    };

    Application();

    void Init(HWND hwnd, int windowWidth, int windowHeight);
    void OnResize(int w, int h);
    void Update(float deltaTime);

    // Matrices (world)
    const glm::mat4& GetProjectionMatrix() const { return projection; }
    const glm::mat4& GetViewMatrix() const { return view; }
    glm::mat4 GetViewportViewMatrix(const Viewport& vp) const;
    const glm::mat4& GetModelMatrix() const { return model; }

    // Paper settings accessors (inches)
    float GetPageWidthInches() const { return page.widthIn; }
    float GetPageHeightInches() const { return page.heightIn; }

    // Layers (global across model + paper spaces)
    LayerTable& GetLayerTable() { return layerTable; }
    const LayerTable& GetLayerTable() const { return layerTable; }
    uint32_t CurrentLayerId() const { return layerTable.CurrentLayerId(); }

    // Selected entity properties (used by Properties window)
    bool HasSelection() const;
    std::size_t GetSelectedCount() const;
    // Returns LayerTable::kInvalidLayerId if selection is empty or mixed.
    uint32_t GetSelectionLayerIdCommon() const;
    void SetSelectionLayer(uint32_t layerId);
    // Color: returns true if all selected share same explicit color and same colorByLayer flag.
    bool GetSelectionColorCommon(bool& outByLayer, glm::vec4& outColor) const;
    void SetSelectionColorByLayer();
    void SetSelectionColorCustom(const glm::vec4& rgba);

    // Selection inspection helpers (used by Properties window filter)
    // Returns the selected entity indices (stable for current scene).
    const std::vector<std::size_t>& GetSelectedIndices() const { return selectedIndices; }

    // Returns counts per EntityType within the current selection.
    std::vector<std::pair<EntityType, std::size_t>> GetSelectionTypeCounts() const;

    // Returns selected indices filtered by entity type. If filterType is std::nullopt, returns full selection.
    std::vector<std::size_t> GetSelectedIndicesByType(std::optional<EntityType> filterType) const;

    // Select entity by stable entity id (Entity::id). Returns false if not found.
    bool SelectEntityById(std::size_t entityId, bool additive = false);

    // Cancel transient UI state (marquee drag, hover, grip hover) and clear selection.
    void CancelInteractions();

    // Called after changing layer defaults (color/linetype) so ByLayer entities update.
    void ApplyLayerDefaultsToEntities();

    // Input
    void SetMouseClient(int x, int y) { mouseClient = { x, y }; }
    // Update cached mouse world coordinates immediately (used by WM_MOUSEMOVE cursor policy)
    void RefreshMouseWorldCache();

    // Right-button drag panning
    void BeginMousePan(int clientX, int clientY);
    void UpdateMousePan(int clientX, int clientY);
    void EndMousePan();
    bool IsMousePanning() const { return mousePanning; }

    // Camera controls
    void ZoomAtClient(int cx, int cy, float zoomFactor);
    void PanByPixels(int dx, int dy);

    // Modes
    void ToggleSelectionMode();
    void ToggleGrid();
    void ToggleWipeout();

    // Paper space
    void TogglePaperSpaceMode();
    bool IsPaperSpace() const { return spaceMode == SpaceMode::Paper; }
    const PageSettings& GetPageSettings() const { return page; }
    RECT GetPageClientRect() const; // {left,top,right,bottom} in client px (top-left origin)

    // Click handlers
    void OnLeftClick();
    void OnLeftClick(HWND hwnd);

    void OnLeftDoubleClick(HWND hwnd);

    // Viewports (paper space)
    void BeginViewportCreate();
    void BeginViewportCommand(class CmdWindow* cmdWnd);
    void ToggleActiveViewportLock();
    bool IsViewportCreateDragging() const { return viewportCreateDragging; }
    bool HasViewportCreateDraft() const { return viewportCreateArmed && viewportCreateHasFirst; }
    glm::vec2 GetViewportCreateP0In() const { return viewportFirstCornerIn; }
    glm::vec2 GetViewportCreateP1In() const { return viewportCurrentCornerIn; }

    void UpdateViewportCreateDrag();   // call on mouse move while creating
    void CommitViewportCreate();       // call on LMB up
    void CancelViewportCreate();       // ESC cancels
bool IsViewportCreateArmed() const { return viewportCreateArmed; }

    bool HasActiveViewport() const { return activeViewportIndex >= 0; }
    const Viewport* GetActiveViewport() const;
    const std::vector<Viewport>& GetViewports() const { return viewports; }

    // Cursor/viewport interaction
    // When a viewport is active in paper space, this reports whether the mouse is currently
    // inside that viewport's client-rect.
    bool MouseInActiveViewport() const { return mouseInActiveViewport; }
    
    // Viewport selection helpers (paper space)
    // If the current selection corresponds to a single viewport frame (4 border segments),
    // returns true and sets outViewportIndex.
    bool IsViewportSelection(int& outViewportIndex) const;
    const Viewport* GetSelectedViewport() const;
    int GetSelectedViewportIndex() const { return selectedViewportIndex; }
    // Multi-viewport selection: if the selection consists ONLY of complete viewport frames
    // (all 4 border segments per viewport), returns true and fills outViewportIndices
    // with the distinct viewport indices (ordered ascending).
    bool GetSelectedViewports(std::vector<int>& outViewportIndices) const;

    // Access a viewport by index (paper-space). Returns nullptr if out of range.
    const Viewport* GetViewportByIndex(int index) const;
    void ClearSelectedViewport() { selectedViewportIndex = -1; }

    // Modify selected viewport properties
    void SetSelectedViewportLocked(bool locked);
    void SetSelectedViewportModelUnitsPerPaperUnit(float muPerPaper);

    glm::vec2 ViewportModelToClientPx(const Viewport& vp, const glm::vec2& modelPt) const;
    glm::vec2 PaperToClientPx(const glm::vec2& paperIn) const;

    // Grip-selection (works even when selection mode is OFF)
    std::size_t GetGripTargetEntityId() const { return gripTargetEntityId; }

    // New: marquee (rectangle) selection when click hits empty space.
    void OnLeftDown(HWND hwnd);
    void OnLeftUp(HWND hwnd);
    void UpdateMarqueeDrag(int clientX, int clientY);
    bool IsMarqueeSelecting() const { return marqueeActive; }


    // ESC: clear selection + grips + hover and cancel marquee
    void CancelSelectionAndGrips();

    EntityBook& GetEntityBook() { return entityBook; }

    // File I/O (JSON via JsonCpp)
    // SAVE  : writes current drawing to the last Save As... path (or prompts if none).
    // SAVEAS: prompts for a path and writes the current drawing.
    bool Save();
    bool SaveAs();
    const std::string& GetCurrentFilePath() const { return currentFilePath; }
    void SetCurrentFilePath(const std::string& path) { currentFilePath = path; }

    // Commands
    // ERASE: remove currently selected dragon curve segments.
    void EraseSelected();

    // Interactive command flow (Command Window)
    enum class MarqueeMode { AutoByDrag, Window, Crossing };
    enum class PendingCommand { None, Erase, Line };

    // Cursor crosshair styling
    // - GripsSelectionCrosshairs: crosshair lines + small center square ("grips" selection)
    // - PointEntryCrosshairs    : crosshair lines only (used for point entry during commands)
    enum class CrosshairsMode { GripsSelectionCrosshairs, PointEntryCrosshairs };

    // Ortho (F8)
    void ToggleOrtho();
    bool IsOrthoEnabled() const { return orthoEnabled; }

    // LINE command (interactive)
    void BeginLineCommand(class CmdWindow* cmdWnd);

    // Snaps (initial scaffolding)
    enum class SnapMode
    {
        None,
        Endpoint,
        Nearpoint,
        Intersection,
        Perpendicular,
        Centerpoint,
        Insertionpoint,
        Tangent,
        Grid
    };
    void SetOneShotSnap(SnapMode mode);
    SnapMode GetOneShotSnap() const { return oneShotSnap; }

    void BeginEraseCommand(class CmdWindow* cmdWnd);
    bool HandleCmdWindowLine(const std::string& line, class CmdWindow* cmdWnd);
    bool HasPendingCommand() const { return pendingCommand != PendingCommand::None; }

    // ------------------------------------------------------------
    // Python scripting (optional)
    // ------------------------------------------------------------
    // Runs a Python script file inside the application (if VK_ENABLE_PYTHON is enabled).
    // Output/errors are appended to the CmdWindow.
    bool RunPythonScript(const std::string& scriptPath, class CmdWindow* cmdWnd);

    // Script-facing helpers: add persistent entities that will remain across scene rebuilds.
    void ScriptAddLine(float x0, float y0, float z0,
                       float x1, float y1, float z1,
                       float r, float g, float b, float a,
                       float thickness, int drawOrder);

    void ScriptAddText(const std::string& text,
                       float x, float y, float z,
                       float scale,
                       float r, float g, float b, float a,
                       int drawOrder);

    // True when the app is ready for a new top-level command to be entered.
    // (Used by CmdWindow to decide when to show the base "Command:" prompt.)
    // NOTE: KEEP THIS IN THE PUBLIC SECTION (it is intentionally part of the UI contract).
    bool IsCmdLineIdle() const;

private:
    // Scene lifecycle
    void MarkAllDirty();
    void RebuildScene();
    void RebuildGrid();

    // Cursor overlay
    void EnsureCursorEntities();
    void UpdateCursorEntities();

    // LINE jig
    void UpdateLineJig();
    void EnsureHudEntities();
    void UpdateHudEntities();

    // Picking / hover
    void EnsurePickTree();
    void BuildPickTree();
    void UpdateHover();
    void ClearHover();

    // Selection helpers
    void ClearSelection();
    void ApplySelection(const std::vector<std::size_t>& indices);
    void AddToSelection(std::size_t idx);
    bool IsIndexSelected(std::size_t idx) const;

    // Persistently erased dragon segments (by stable entity id).
    // This keeps erasures across scene rebuilds caused by pan/zoom/etc.
    std::unordered_set<uint32_t> erasedDragonIds;

    // Embedded Python scripting (optional).
    PythonScriptRunner pyRunner;

    // Marquee selection
    void BeginMarquee();
    void FinishMarqueeSelect();
    void UpdateMarqueeOverlay();
    void UpdateViewportCreateOverlay();

    glm::vec2 WorldToClient(const glm::vec2& world) const;

    // Viewport helpers (paper space)
    int  FindViewportAtPaperPoint(const glm::vec2& paperIn) const; // returns index or -1
    bool PaperPointInViewport(const Viewport& vp, const glm::vec2& paperIn) const;
    glm::vec2 PaperViewportCenter(const Viewport& vp) const;
    glm::vec2 ClientToPaperWorld(const glm::ivec2& client) const;
    std::optional<glm::vec2> ClientToViewportModel(int cx, int cy) const;

    bool IsViewportInteractionActive() const; // paper space + active viewport + cursor inside it
    void GetInteractionPixelsPerWorld(float& outPxPerWorldX, float& outPxPerWorldY) const;

    // Map a model-space point to client pixels (top-left origin) through a viewport.
    glm::vec2 ViewportModelToClient(const Viewport& vp, const glm::vec2& modelPt) const;

    void SetActiveViewport(int index);

    // Grip-selection helpers
    void EnsureGripEntities(std::size_t requiredCount);
    void UpdateGripEntities();
    void ClearGrip();
    void UpdateGripHover();
    void SetGripTarget(std::size_t entityId, const glm::vec2& aModel, const glm::vec2& bModel);

    // Mouse helpers
    void OnMouseMove();
    glm::vec2 ClientToWorld(const glm::ivec2& client) const;
    glm::vec2 ClientToWorld(int cx, int cy) const { return ClientToWorld(glm::ivec2(cx, cy)); }

    // Active camera parameters (depend on space mode)
    glm::vec2& ActivePan();
    const glm::vec2& ActivePan() const;
    float& ActiveZoom();
    float ActiveZoom() const;
    glm::vec2 ActiveUnitsToPixels() const;

private:
    EntityBook entityBook{};

    // Window handle for modal dialogs (Save As...)
    HWND hwndMain = nullptr;

    // Last saved/selected drawing path for Save()
    std::string currentFilePath;

    // Paper-space viewports
    std::vector<Viewport> viewports;
    int activeViewportIndex = -1;
    int selectedViewportIndex = -1; // viewport frame selection corresponds to this viewport index (paper)

    // Viewport creation state (paper space)
    bool viewportCreateArmed = false;
    bool viewportCreateTwoClick = false; // true when started from VIEWPORT command (two-click)
    bool viewportCreateHasFirst = false;
    glm::vec2 viewportFirstCornerIn{};
    bool viewportCreateDragging = false;
    glm::vec2 viewportCurrentCornerIn{};

    // When VIEWPORT is started from the Command Window, we keep a sink so we can
    // report completion (and other feedback) back to the command log.
    class CmdWindow* viewportCreateCmdWnd = nullptr;

    // Grip-selection state
    bool gripEntitiesValid = false;
    std::size_t gripTargetEntityId = 0;
    glm::vec2 gripA_Model{};
    glm::vec2 gripB_Model{};
    std::vector<std::size_t> gripRectIds; // dynamic pool of small blue rectangles (screen space)

    int clientWidth = 1;
    int clientHeight = 1;

    // Camera
    // Model-space (existing behavior)
    glm::vec2 modelPan{ 0.0f, 0.0f };
    float modelZoom = 1.0f;

    // Paper-space (world units == inches)
    glm::vec2 paperPanIn{ 0.0f, 0.0f };
    float paperZoom = 1.0f;

    SpaceMode spaceMode = SpaceMode::Model;
    PageSettings page{};

    void UpdateCameraMatrices();
    // Input state
glm::ivec2 mouseClient{ 0, 0 };

// mouseWorld is the current interaction-space cursor position:
// - Model space: model units
// - Paper space: inches
// - Paper space + active viewport + cursor inside viewport: MODEL units (editing through viewport)
glm::vec2 mouseWorld{ 0.0f, 0.0f };

// Cached cursor positions in both coordinate systems (updated every frame).
glm::vec2 mouseWorldPaper{ 0.0f, 0.0f }; // inches (paper)
glm::vec2 mouseWorldModel{ 0.0f, 0.0f }; // model units (through active viewport)
bool mouseInActiveViewport = false;

    // Right-button drag panning state
    bool mousePanning = false;
    glm::ivec2 mousePanLastClient{ 0, 0 };

    // Marquee drag (LMB in selection mode when click hits empty space)
    bool marqueeActive = false;
    bool marqueeAwaitSecondClick = false; // W/C modes: first click sets start, second click finishes
    glm::ivec2 marqueeStartClient{ 0,0 };
    glm::ivec2 marqueeEndClient{ 0,0 };
    glm::vec2 marqueeStartWorld{ 0.0f,0.0f };
    glm::vec2 marqueeEndWorld{ 0.0f,0.0f };

    // Interactive command selection state
    MarqueeMode marqueeMode = MarqueeMode::AutoByDrag;
    PendingCommand pendingCommand = PendingCommand::None;

    // Crosshair style (see CrosshairsMode)
    CrosshairsMode crosshairsMode = CrosshairsMode::GripsSelectionCrosshairs;

    // Ortho mode
    bool orthoEnabled = false;

    // Snap system scaffolding
    SnapMode oneShotSnap = SnapMode::None;

    // LINE command state
    enum class LineStage { None, FirstNode, SecondNode };
    LineStage lineStage = LineStage::None;
    glm::vec3 lineFirstNode{ 0,0,0 };
    glm::vec3 lineSecondNode{ 0,0,0 };
    bool lineHasFirst = false;

    // Optional: command window for interactive prompts
    class CmdWindow* lineCmdWnd = nullptr;

    // Cursor entities for line jig
    uint32_t cursorLineJigId = 0;

    void SelectAllSelectableEntities();

    // Selection
    // Multi-selection support for marquee.
    std::vector<std::size_t> selectedIndices;
    int selectedIndex = -1; // first selected (or -1)

    // Safeguard: selection should only mutate during explicit input events (click/marquee).
    bool allowSelectionMutation = false;

    // RAII scope used by input handlers to temporarily allow selection mutations.
    // This keeps selection edits tightly scoped to user actions and prevents
    // accidental modifications during rendering/other code paths.
    struct SelectionMutationScope
    {
        Application& app;
        bool prev;
        explicit SelectionMutationScope(Application& a)
            : app(a), prev(a.allowSelectionMutation)
        {
            app.allowSelectionMutation = true;
        }
        ~SelectionMutationScope()
        {
            app.allowSelectionMutation = prev;
        }
        SelectionMutationScope(const SelectionMutationScope&) = delete;
        SelectionMutationScope& operator=(const SelectionMutationScope&) = delete;
    };

    bool debugSelection = true; // prints to stdout in _DEBUG builds
    void DebugSelectionf(const char* fmt, ...) const;

    // Modes
    bool selectionMode = false;
    bool gridEnabled = true;
    bool wipeoutEnabled = true;

    // Dirty flags
    bool dirtyScene = true;
    bool dirtyPickTree = true;

    // Picking structure
    RGeometryTree pickTree;

    // Global layer table (shared by model space + paper space)
    LayerTable layerTable;

    std::optional<std::size_t> hoveredIndex;
    glm::vec4 hoveredPrevColor{ 1,1,1,1 };
    bool hoveredPrevColorByLayer = true;

    // Cursor entity ids (screen space)
    bool cursorEntitiesValid = false;
    uint32_t cursorCrossId[6]{ 0,0,0,0,0,0 };
    uint32_t cursorBoxId[4]{ 0,0,0,0 };
    uint32_t marqueeBoxId[4]{ 0,0,0,0 };
    uint32_t viewportCreateBoxId[4]{ 0,0,0,0 };


    // Debug HUD entity ids (screen space)
    bool hudEntitiesValid = false;
    uint32_t hudOrthoId = 0;
    uint32_t hudCursorId = 0;
    uint32_t hudStatusId = 0;


    // Entity IDs
    uint32_t nextId = 1;

    // Matrices
    glm::mat4 projection{ 1.0f };
    glm::mat4 view{ 1.0f };
    glm::mat4 model{ 1.0f };
};


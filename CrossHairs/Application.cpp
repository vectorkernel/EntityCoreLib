#include "Application.h"
#include <windows.h>
#include <iostream>
#include <cmath>
#include <sstream>
#include <unordered_set>
#include <string>
#include <filesystem>

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

void Application::OnMouseMoveClient(int x, int y)
{
    // Crosshairs expects Win32 client coords (origin top-left, Y down)
    m_crosshairs.SetMouseClient(x, y);
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

void Application::Update(float /*inDeltaTime*/)
{
    // Grid is static for this first test.
}

void Application::Render(float /*inAspectRatio*/)
{
    // Get current client size from GL viewport.
    GLint vp[4]{};
    glGetIntegerv(GL_VIEWPORT, vp);
    const int w = vp[2];
    const int h = vp[3];

    // If size changed (or first frame), rebuild grid in the requested coordinate system.
    if (w != m_lastClientW || h != m_lastClientH)
    {
        m_lastClientW = w;
        m_lastClientH = h;

        BuildGrid(w, h);
        UploadLinesToGpu();

        std::cout << "Viewport " << w << "x" << h << " -> rebuilt grid\n";
    }

    // Feed viewport into the pan/zoom controller and use its MVP
    m_pz.SetViewport(w, h);
    m_crosshairs.SetViewport(w, h);
    glm::mat4 mvp = m_pz.GetMVP();

    glUseProgram(m_program);
    glUniformMatrix4fv(m_uMvp, 1, GL_FALSE, &mvp[0][0]);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));

    glDrawArrays(GL_LINES, 0, (GLsizei)m_vertices.size());


    // -------------------------
// CROSSHAIRS PASS (screen-space)
// -------------------------
    {
        // Pixel-space MVP: origin bottom-left, X right, Y up
        glm::mat4 overlayMvp = glm::ortho(0.0f, (float)w, 0.0f, (float)h, -1.0f, +1.0f);

        // Build crosshair line segments (LineEntity) in pixel coords (Y-up)
        std::vector<LineEntity> cursorLines;
        m_crosshairs.BuildLines(cursorLines);

        // Convert to our Vertex list
        m_cursorVertices.clear();
        m_cursorVertices.reserve(cursorLines.size() * 2);

        for (const auto& L : cursorLines)
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

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));

        glDrawArrays(GL_LINES, 0, (GLsizei)m_cursorVertices.size());
    }


    // -------------------------
    // HUD PASS (screen-space)
    // -------------------------
    if (m_hudFont)
    {
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
            << " | world(0,0)->screen(" << (int)sx << "," << (int)sy << ")";


        // 3) Convert Hershey text -> LineEntity list
        TextEntity t;
        t.font = m_hudFont;              // <-- key line
        t.wordWrapEnabled = false;
        t.hAlign = TextHAlign::Left;

        // Builder uses boxHeight to compute baseline/top padding.
        // Give it a sane HUD "text box".
        t.boxWidth = (float)w;    // large enough so it won't wrap
        t.boxHeight = 40.0f;       // IMPORTANT: without this, baseline calc can go off-screen

        // Y-up pixel coordinates: put box near the top-left
        t.position = { 10.0f, (float)h - 10.0f - t.boxHeight, 0.0f };
        t.scale = 1.0f;
        t.strokeWidth = 1.0f;
        t.color = { 1, 1, 0, 1 }; // yellow-ish for visibility (change if you want)

        std::vector<LineEntity> lines;
        t.text = oss.str();
        if (t.text.empty())
            t.text = "HUD ONLINE";
        if (t.text.empty())
            LogOnce("hud_text_empty", "HUD: t.text was empty before BuildLines()");
        if (!t.font)
        {
            LogOnce("hud_no_font", "HUD: No font (t.font is NULL) -> skipping Hershey HUD.");
            // fall through to fallback marker below
        }
        else if (t.text.empty())
        {
            LogOnce("hud_no_text", "HUD: t.text is empty -> skipping Hershey HUD.");
        }
        else
        {
            HersheyTextBuilder::BuildLines(t, lines);
        }

        static bool printedEmpty = false;
        if (lines.empty() && !printedEmpty)
        {
            printedEmpty = true;
            std::cout << "HUD: BuildLines produced 0 segments.\n";
        }

        static bool printedOk = false;
        if (!lines.empty() && !printedOk)
        {
            printedOk = true;
            std::cout << "HUD: BuildLines produced " << lines.size() << " segments.\n";
        }

        static bool dumped = false;
        if (!dumped)
        {
            dumped = true;
            std::cout
                << "HUD dump:\n"
                << "  text = '" << t.text << "'\n"
                << "  text.len = " << t.text.size() << "\n"
                << "  font ptr = " << (void*)t.font << "\n"
                << "  pos = (" << t.position.x << "," << t.position.y << "," << t.position.z << ")\n"
                << "  box = (" << t.boxWidth << " x " << t.boxHeight << ")\n"
                << "  scale = " << t.scale << "  stroke = " << t.strokeWidth << "\n";
        }

        // 4) Convert LineEntity -> your Vertex list (two vertices per line)
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

        // 6) Set HUD projection: pixel coordinates with TOP-LEFT origin
        glm::mat4 hudMvp = glm::ortho(0.0f, (float)w, 0.0f, (float)h, -1.0f, +1.0f);

        glUseProgram(m_program);
        glUniformMatrix4fv(m_uMvp, 1, GL_FALSE, &hudMvp[0][0]);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));

        glDrawArrays(GL_LINES, 0, (GLsizei)m_hudVertices.size());
    }


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
        e.id = m_nextId++;
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

    m_book->SortByDrawOrder();
}

void Application::UploadLinesToGpu()
{
    if (!m_book) return;

    m_vertices.clear();
    m_vertices.reserve(m_book->entities.size() * 2);

    for (const auto& e : m_book->entities)
    {
        if (e.type != EntityType::Line)
            continue;

        m_vertices.push_back({ e.line.p0, e.line.color });
        m_vertices.push_back({ e.line.p1, e.line.color });
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(m_vertices.size() * sizeof(Vertex)),
                 m_vertices.data(),
                 GL_STATIC_DRAW);

    std::cout << "Uploaded " << (m_vertices.size() / 2) << " line segments\n";
}

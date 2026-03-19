#include "Application.h"

#include <iostream>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp> // glm::ortho

// Use RenderCoreLib helper instead of duplicating shader compile/link.
#include "createShaderProgram.h" // from RenderCoreLib (adjust include if needed)

Application::Application() = default;

Application::~Application()
{
    // In case Shutdown wasn't called.
    Shutdown();
}

void Application::LogSink(EntityCore::LogLevel level,
    const char* category,
    const char* event,
    std::string_view message)
{
    const char* lvl = "TRACE";
    switch (level)
    {
    case EntityCore::LogLevel::Trace: lvl = "TRACE"; break;
    case EntityCore::LogLevel::Info:  lvl = "INFO "; break;
    case EntityCore::LogLevel::Warn:  lvl = "WARN "; break;
    case EntityCore::LogLevel::Error: lvl = "ERROR"; break;
    default: break;
    }

    const char* cat = category ? category : "(null)";
    const char* evt = event ? event : "(null)";

    std::cout << "[" << lvl << "] " << cat << "::" << evt;
    if (!message.empty())
        std::cout << " | " << message;
    std::cout << "\n";
}

void Application::Initialize()
{
    EntityCore::SetLogSink(&Application::LogSink);
    std::cout << "Application::Initialize\n";

    // Create EntityBook via factory so you get capacity + init logs
    auto created = CreateEntityBook(8192); // pick a capacity you want
    if (!created)
    {
        std::cout << "FATAL: EntityBook creation failed\n";
        return;
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

    // ✅ Use RenderCoreLib shader helper (no duplicate Compile/Link in Application)
    m_program = createShaderProgram(std::string(vsSrc), std::string(fsSrc));
    if (m_program == 0)
    {
        std::cout << "FATAL: createShaderProgram failed\n";
        return;
    }

    m_uMvp = glGetUniformLocation(m_program, "uMvp");

    glGenBuffers(1, &m_vbo);

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
    }

    glViewport(0, 0, w, h);
    glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(m_program);

    // Ortho projection in "pixel-ish" world units centered at origin:
    // x: [-w/2, +w/2], y: [-h/2, +h/2]
    glm::mat4 proj = glm::ortho(
        -0.5f * (float)w, 0.5f * (float)w,
        -0.5f * (float)h, 0.5f * (float)h,
        -1.0f, 1.0f);

    glUniformMatrix4fv(m_uMvp, 1, GL_FALSE, &proj[0][0]);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));

    glDrawArrays(GL_LINES, 0, (GLsizei)m_vertices.size());
}

void Application::Shutdown()
{
    // Avoid double shutdown from dtor
    if (m_program == 0 && m_vbo == 0 && !m_book)
        return;

    std::cout << "Application::Shutdown\n";

    EntityCore::SetLogSink(nullptr);

    if (m_vbo)
    {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }

    if (m_program)
    {
        glDeleteProgram(m_program);
        m_program = 0;
    }

    m_vertices.clear();

    if (m_book)
    {
        m_book->Clear();
        m_book.reset();
    }
}

void Application::BuildGrid(int clientW, int clientH)
{
    if (!m_book) return;

    m_book->Clear();

    const float halfW = 0.5f * (float)clientW;
    const float halfH = 0.5f * (float)clientH;

    const float spacing = 50.0f;

    auto addLine = [&](glm::vec3 a, glm::vec3 b, glm::vec4 color)
        {
            Entity e;
            e.ID = m_nextId++;
            e.type = EntityType::Line;
            e.tag = EntityTag::Scene;
            e.drawOrder = 0;
            e.screenSpace = false;
            e.visible = true;
            e.pickable = false;
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

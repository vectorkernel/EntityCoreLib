
#include "pch.h"
#include <string>
#include <iostream>
#include <unordered_map>

#include "GLLine.h"
#include "ShaderLibrary.h"
#include "ShaderProgram.h"

GLuint GLLine::shaderProgram_ = 0;

static const char* kLineBlockName = "LineBlock";

// Inline GLSL for the wide-line shader (based on the provided LineBlock.* files).
// We keep it inline (as requested) but register it in ShaderLibrary so it can
// be swapped/replaced at runtime via the new hot-swap system.
static const std::string kLineBlockVS = R"(
#version 330 core
layout (location = 0) in vec3 aPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

// Geometry stage expands each GL_LINES segment into a camera-facing quad.
// The uniform "lineWidth" is specified in pixels.
static const std::string kLineBlockGS = R"(
#version 330 core
layout (lines) in;
layout (triangle_strip, max_vertices = 4) out;

uniform float lineWidth;   // in pixels
uniform vec2 viewportPx;   // (w,h) in pixels

vec2 PixelToNdc(float px)
{
    // NDC spans [-1,1] => 2 units.
    // One pixel in NDC is 2/viewport.
    vec2 denom = max(viewportPx, vec2(1.0));
    return vec2(px) * (2.0 / denom);
}

void Emit(vec4 clipPos, vec2 offsetNdc)
{
    vec4 p = clipPos;
    // Convert NDC offset back into clip space by multiplying by W.
    p.xy += offsetNdc * clipPos.w;
    gl_Position = p;
    EmitVertex();
}

void main()
{
    vec4 p0 = gl_in[0].gl_Position;
    vec4 p1 = gl_in[1].gl_Position;

    if (p0.w == 0.0 || p1.w == 0.0)
        return;

    vec2 ndc0 = p0.xy / p0.w;
    vec2 ndc1 = p1.xy / p1.w;

    vec2 dir = ndc1 - ndc0;
    float len = length(dir);
    if (len < 1e-6)
        return;

    dir /= len;
    vec2 normal = vec2(-dir.y, dir.x);

    // Half-width in NDC.
    vec2 offset = normal * PixelToNdc(lineWidth * 0.5);

    // Triangle strip: (p0 - off), (p0 + off), (p1 - off), (p1 + off)
    Emit(p0, -offset);
    Emit(p0,  offset);
    Emit(p1, -offset);
    Emit(p1,  offset);
    EndPrimitive();
}
)";

static const std::string kLineBlockFS = R"(
#version 330 core
out vec4 FragColor;
uniform vec4 color;
void main()
{
    FragColor = color;
}
)";

std::shared_ptr<ShaderProgram> GLLine::GetLineBlockProgram()
{
    auto& lib = ShaderLibrary::Get();
    if (auto existing = lib.Find(kLineBlockName))
        return existing;

    std::unordered_map<GLenum, std::string> stages;
    stages[GL_VERTEX_SHADER] = kLineBlockVS;
    stages[GL_GEOMETRY_SHADER] = kLineBlockGS;
    stages[GL_FRAGMENT_SHADER] = kLineBlockFS;
    return lib.CreateFromSources(kLineBlockName, stages);
}

GLuint GLLine::GetShaderProgram() {
    // Prefer the new hot-swap system.
    auto prog = GetLineBlockProgram();
    shaderProgram_ = prog ? prog->Id() : 0;
    return shaderProgram_;
}

void GLLine::DeleteShaderProgram() {
    // ShaderLibrary owns the program lifetime now; keep legacy behavior as a no-op.
    shaderProgram_ = 0;
}

#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <glad/glad.h>

#include <memory>

class ShaderProgram;

class GLLine {
public:
    GLLine(const std::vector<glm::vec3>& vertices, const glm::vec4& color, float width)
        : vertices_(vertices), color_(color), width_(width), vertexCount_(static_cast<GLsizei>(vertices.size())) {
    }

    void SetColor(const glm::vec4& color) { color_ = color; }
    void SetWidth(float width) { width_ = width; }
    void SetVboOffset(GLsizei offset) { vboOffset_ = offset; }

    const std::vector<glm::vec3>& GetVertices() const { return vertices_; }
    const glm::vec4& GetColor() const { return color_; }
    float GetWidth() const { return width_; }
    GLsizei GetVboOffset() const { return vboOffset_; }
    GLsizei GetVertexCount() const { return vertexCount_; }

    // Returns the shared line shader program used for wide-line rendering.
    // Registered in ShaderLibrary under the name "LineBlock".
    static std::shared_ptr<ShaderProgram> GetLineBlockProgram();

    // Legacy API (kept for compatibility; returns program id or 0).
    static GLuint GetShaderProgram();
    static void DeleteShaderProgram();

private:
    std::vector<glm::vec3> vertices_;
    glm::vec4 color_;
    float width_;
    GLsizei vboOffset_ = 0;
    GLsizei vertexCount_;
    static GLuint shaderProgram_;
};

#pragma once
#include <glad/glad.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>

class ShaderProgram
{
public:
    ShaderProgram() = default;
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    ShaderProgram(ShaderProgram&& other) noexcept;
    ShaderProgram& operator=(ShaderProgram&& other) noexcept;

    // Build from in-memory GLSL strings
    bool BuildFromSources(const std::unordered_map<GLenum, std::string>& stageSources,
                          std::string* outErrorLog = nullptr);

    // Build from files (stores file paths for hot-reload)
    bool BuildFromFiles(const std::unordered_map<GLenum, std::filesystem::path>& stageFiles,
                        std::string* outErrorLog = nullptr);

    // Recompile if any watched file has changed. Keeps old program if compile/link fails.
    bool ReloadIfChanged(std::string* outErrorLog = nullptr);

    GLuint Id() const { return program_; }
    bool IsValid() const { return program_ != 0; }

private:
    static bool ReadFile(const std::filesystem::path& p, std::string& out, std::string* outError);
    static GLuint CompileStage(GLenum type, const std::string& src, std::string* outError);
    static GLuint LinkProgram(const std::vector<GLuint>& stages, std::string* outError);

    void Destroy();

private:
    GLuint program_ = 0;

    // For hot reload:
    std::unordered_map<GLenum, std::filesystem::path> stageFiles_;
    std::unordered_map<GLenum, std::filesystem::file_time_type> stageTimes_;
};

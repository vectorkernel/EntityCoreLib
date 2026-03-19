#include "pch.h"
#include "ShaderProgram.h"
#include <sstream>
#include <fstream>

ShaderProgram::~ShaderProgram() { Destroy(); }

ShaderProgram::ShaderProgram(ShaderProgram&& other) noexcept
{
    program_ = other.program_;
    stageFiles_ = std::move(other.stageFiles_);
    stageTimes_ = std::move(other.stageTimes_);
    other.program_ = 0;
}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& other) noexcept
{
    if (this == &other) return *this;
    Destroy();
    program_ = other.program_;
    stageFiles_ = std::move(other.stageFiles_);
    stageTimes_ = std::move(other.stageTimes_);
    other.program_ = 0;
    return *this;
}

void ShaderProgram::Destroy()
{
    if (program_ != 0)
    {
        glDeleteProgram(program_);
        program_ = 0;
    }
}

bool ShaderProgram::ReadFile(const std::filesystem::path& p, std::string& out, std::string* outError)
{
    std::ifstream f(p, std::ios::in | std::ios::binary);
    if (!f)
    {
        if (outError) *outError = "Failed to open shader file: " + p.string();
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

GLuint ShaderProgram::CompileStage(GLenum type, const std::string& src, std::string* outError)
{
    GLuint s = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(s, 1, &c, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        if (outError)
        {
            *outError += std::string("Shader compile failed (") + std::to_string((int)type) + "):\n" + log + "\n";
        }
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint ShaderProgram::LinkProgram(const std::vector<GLuint>& stages, std::string* outError)
{
    GLuint p = glCreateProgram();
    for (GLuint s : stages) glAttachShader(p, s);
    glLinkProgram(p);

    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[4096];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        if (outError) *outError += std::string("Program link failed:\n") + log + "\n";
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

bool ShaderProgram::BuildFromSources(const std::unordered_map<GLenum, std::string>& stageSources,
                                    std::string* outErrorLog)
{
    std::string err;
    std::vector<GLuint> stages;
    stages.reserve(stageSources.size());

    for (auto& kv : stageSources)
    {
        GLuint s = CompileStage(kv.first, kv.second, &err);
        if (!s)
        {
            if (outErrorLog) *outErrorLog = err;
            for (GLuint st : stages) glDeleteShader(st);
            return false;
        }
        stages.push_back(s);
    }

    GLuint newProg = LinkProgram(stages, &err);

    for (GLuint st : stages) glDeleteShader(st);

    if (!newProg)
    {
        if (outErrorLog) *outErrorLog = err;
        return false;
    }

    Destroy();
    program_ = newProg;

    // If building from sources, stop watching files.
    stageFiles_.clear();
    stageTimes_.clear();
    return true;
}

bool ShaderProgram::BuildFromFiles(const std::unordered_map<GLenum, std::filesystem::path>& stageFiles,
                                  std::string* outErrorLog)
{
    std::unordered_map<GLenum, std::string> sources;
    sources.reserve(stageFiles.size());

    std::string err;
    for (auto& kv : stageFiles)
    {
        std::string src;
        if (!ReadFile(kv.second, src, &err))
        {
            if (outErrorLog) *outErrorLog = err;
            return false;
        }
        sources[kv.first] = std::move(src);
    }

    if (!BuildFromSources(sources, &err))
    {
        if (outErrorLog) *outErrorLog = err;
        return false;
    }

    // Start watching files.
    stageFiles_ = stageFiles;
    stageTimes_.clear();
    for (auto& kv : stageFiles_)
    {
        stageTimes_[kv.first] = std::filesystem::last_write_time(kv.second);
    }
    return true;
}

bool ShaderProgram::ReloadIfChanged(std::string* outErrorLog)
{
    if (stageFiles_.empty())
        return false;

    bool changed = false;
    for (auto& kv : stageFiles_)
    {
        auto t = std::filesystem::last_write_time(kv.second);
        auto it = stageTimes_.find(kv.first);
        if (it == stageTimes_.end() || t != it->second)
        {
            changed = true;
            break;
        }
    }
    if (!changed)
        return false;

    std::unordered_map<GLenum, std::filesystem::path> files = stageFiles_;
    std::string err;
    ShaderProgram temp;
    if (!temp.BuildFromFiles(files, &err))
    {
        if (outErrorLog) *outErrorLog = err;
        return false; // keep old program
    }

    // swap
    Destroy();
    program_ = temp.program_;
    temp.program_ = 0;
    stageFiles_ = std::move(files);
    stageTimes_.clear();
    for (auto& kv : stageFiles_)
        stageTimes_[kv.first] = std::filesystem::last_write_time(kv.second);

    return true;
}

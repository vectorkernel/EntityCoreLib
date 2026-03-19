#include "pch.h"
#include "ShaderLibrary.h"
#include <cstdio>

ShaderLibrary& ShaderLibrary::Get()
{
    static ShaderLibrary inst;
    return inst;
}

std::shared_ptr<ShaderProgram> ShaderLibrary::CreateFromSources(
    const std::string& name,
    const std::unordered_map<GLenum, std::string>& stageSources)
{
    auto prog = std::make_shared<ShaderProgram>();
    std::string err;
    if (!prog->BuildFromSources(stageSources, &err))
    {
        std::printf("ShaderLibrary: build failed for '%s'\n%s\n", name.c_str(), err.c_str());
        return nullptr;
    }
    programs_[name] = prog;
    return prog;
}

std::shared_ptr<ShaderProgram> ShaderLibrary::CreateFromFiles(
    const std::string& name,
    const std::unordered_map<GLenum, std::filesystem::path>& stageFiles)
{
    auto prog = std::make_shared<ShaderProgram>();
    std::string err;
    if (!prog->BuildFromFiles(stageFiles, &err))
    {
        std::printf("ShaderLibrary: build-from-files failed for '%s'\n%s\n", name.c_str(), err.c_str());
        return nullptr;
    }
    programs_[name] = prog;
    return prog;
}

std::shared_ptr<ShaderProgram> ShaderLibrary::Find(const std::string& name) const
{
    auto it = programs_.find(name);
    if (it == programs_.end()) return nullptr;
    return it->second;
}

void ShaderLibrary::ReloadChanged()
{
    for (auto& kv : programs_)
    {
        std::string err;
        if (kv.second && kv.second->ReloadIfChanged(&err))
        {
            std::printf("ShaderLibrary: reloaded '%s'\n", kv.first.c_str());
        }
        else if (!err.empty())
        {
            std::printf("ShaderLibrary: reload failed for '%s'\n%s\n", kv.first.c_str(), err.c_str());
        }
    }
}

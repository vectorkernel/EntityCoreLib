#pragma once
#include "ShaderProgram.h"
#include <memory>
#include <string>
#include <unordered_map>

class ShaderLibrary
{
public:
    static ShaderLibrary& Get();

    // Create or replace a named program from GLSL sources.
    std::shared_ptr<ShaderProgram> CreateFromSources(
        const std::string& name,
        const std::unordered_map<GLenum, std::string>& stageSources);

    // Create or replace a named program from files (enables hot reload).
    std::shared_ptr<ShaderProgram> CreateFromFiles(
        const std::string& name,
        const std::unordered_map<GLenum, std::filesystem::path>& stageFiles);

    std::shared_ptr<ShaderProgram> Find(const std::string& name) const;

    // Call once per frame (or on keypress) to hot-reload any changed programs.
    void ReloadChanged();

private:
    ShaderLibrary() = default;

private:
    std::unordered_map<std::string, std::shared_ptr<ShaderProgram>> programs_;
};

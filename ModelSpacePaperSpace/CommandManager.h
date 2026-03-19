#pragma once

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

// Windows virtual key chord (VK_* from WinUser.h)
struct KeyChord
{
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    unsigned int key = 0; // VK_*
};

class CommandManager
{
public:
    CommandManager() = default; // MUST be public (main.cpp instantiates global g_commands)

    // Register a command with default shortcut + callback
    void Register(
        const std::string& name,
        const std::string& description,
        const KeyChord& defaultShortcut,
        std::function<void()> fn);

    // Returns true if a command handled the key press
    bool HandleKeyDown(unsigned int vk, bool ctrl, bool alt, bool shift);

    // Execute a command line entered by the user (e.g. "ERASE" or "LINE 0 0 10 10").
    // Currently only the first token (command name) is used.
    bool ExecuteLine(const std::string& line);

    // Loads bindings from file; if missing creates one with defaults
    void LoadOrCreateBindingsFile(const std::string& filename);

    // Returns (NAME, description) pairs for building a command help table.
    std::vector<std::pair<std::string, std::string>> GetCommandList() const;

private:
    struct Command
    {
        std::string name;
        std::string description;
        KeyChord chord;
        std::function<void()> fn;
        KeyChord defaultChord;
    };

    std::unordered_map<std::string, Command> m_commands;

    void LoadBindings(const std::string& filename);
    void SaveBindings(const std::string& filename) const;

    static bool Match(const KeyChord& c, unsigned int vk, bool ctrl, bool alt, bool shift);

    static bool ParseChordString(const std::string& text, KeyChord& outChord);
    static std::string ToChordString(const KeyChord& chord);

    static std::string Trim(const std::string& s);
};

#include "CommandManager.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <vector>
#include <cctype>

#ifdef _WIN32
#include <windows.h> // VK_* constants
#endif

// ------------------------- helpers -------------------------

std::string CommandManager::Trim(const std::string& s)
{
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static std::string ToUpperCopy(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return (char)std::toupper(c); });
    return s;
}

bool CommandManager::Match(const KeyChord& c, unsigned int vk, bool ctrl, bool alt, bool shift)
{
    return c.key == vk && c.ctrl == ctrl && c.alt == alt && c.shift == shift;
}

// Convert chord to text like "Ctrl+Shift+Delete" or "A"
std::string CommandManager::ToChordString(const KeyChord& chord)
{
    if (chord.key == 0)
        return "None";
    std::string out;
    if (chord.ctrl)  out += "Ctrl+";
    if (chord.alt)   out += "Alt+";
    if (chord.shift) out += "Shift+";

#ifdef _WIN32
    if (chord.key == VK_DELETE) out += "Delete";
    else if (chord.key == VK_BACK) out += "Backspace";
    else if (chord.key == VK_RETURN) out += "Enter";
    else if (chord.key == VK_ESCAPE) out += "Escape";
    else if (chord.key >= 'A' && chord.key <= 'Z') out += (char)chord.key;
    else if (chord.key >= '0' && chord.key <= '9') out += (char)chord.key;
    else
    {
        // fallback
        out += "VK_" + std::to_string(chord.key);
    }
#else
    out += std::to_string(chord.key);
#endif

    return out;
}

// Parse strings like "Delete", "Ctrl+E", "Ctrl+Shift+Delete"
bool CommandManager::ParseChordString(const std::string& text, KeyChord& outChord)
{
    KeyChord chord;

    std::string raw = Trim(text);
    if (raw.empty()) return false;

    if (ToUpperCopy(raw) == "NONE")
    {
        outChord = KeyChord{};
        outChord.key = 0;
        return true;
    }

    std::stringstream ss(raw);
    std::string tok;

    while (std::getline(ss, tok, '+'))
    {
        tok = Trim(tok);
        if (tok.empty()) continue;

        std::string u = ToUpperCopy(tok);

        if (u == "CTRL" || u == "CONTROL")
        {
            chord.ctrl = true;
            continue;
        }
        if (u == "ALT")
        {
            chord.alt = true;
            continue;
        }
        if (u == "SHIFT")
        {
            chord.shift = true;
            continue;
        }

#ifdef _WIN32
        if (u == "DELETE" || u == "DEL") chord.key = VK_DELETE;
        else if (u == "BACKSPACE" || u == "BKSP") chord.key = VK_BACK;
        else if (u == "ENTER" || u == "RETURN") chord.key = VK_RETURN;
        else if (u == "ESC" || u == "ESCAPE") chord.key = VK_ESCAPE;
        else if (u.size() == 1)
        {
            // allow A-Z / 0-9
            char c = tok[0];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            chord.key = (unsigned int)c;
        }
        else if (u.rfind("VK_", 0) == 0)
        {
            chord.key = (unsigned int)std::stoi(u.substr(3));
        }
        else
        {
            return false;
        }
#else
        if (u.size() == 1) chord.key = (unsigned int)tok[0];
        else return false;
#endif
    }

    if (chord.key == 0) return false;
    outChord = chord;
    return true;
}

// ------------------------- public API -------------------------

void CommandManager::Register(
    const std::string& name,
    const std::string& description,
    const KeyChord& defaultShortcut,
    std::function<void()> fn)
{
    // Store commands by uppercase key so lookups are case-insensitive and consistent.
    const std::string key = ToUpperCopy(name);

    Command cmd;
    cmd.name = key;
    cmd.description = description;
    cmd.fn = std::move(fn);
    cmd.chord = defaultShortcut;
    cmd.defaultChord = defaultShortcut;

    m_commands[key] = std::move(cmd);
}

bool CommandManager::HandleKeyDown(unsigned int vk, bool ctrl, bool alt, bool shift)
{
    // Find command whose chord matches this key event
    for (auto& it : m_commands)
    {
        Command& cmd = it.second;
        if (Match(cmd.chord, vk, ctrl, alt, shift))
        {
            if (cmd.fn) cmd.fn();
            return true;
        }
    }
    return false;
}


bool CommandManager::ExecuteLine(const std::string& line)
{
    std::string s = Trim(line);
    if (s.empty()) return false;

    // Command name is first token (space-delimited)
    std::string name;
    {
        std::istringstream iss(s);
        iss >> name;
    }

    // Uppercase for lookup consistency
    name = ToUpperCopy(name);

    auto it = m_commands.find(name);
    if (it == m_commands.end())
        return false;

    if (it->second.fn)
        it->second.fn();

    return true;
}

void CommandManager::LoadOrCreateBindingsFile(const std::string& filename)
{
    std::ifstream test(filename);
    if (!test.good())
    {
        // create with defaults
        SaveBindings(filename);
        return;
    }

    LoadBindings(filename);
}

std::vector<std::pair<std::string, std::string>> CommandManager::GetCommandList() const
{
    std::vector<std::pair<std::string, std::string>> out;
    out.reserve(m_commands.size());

    for (const auto& kv : m_commands)
        out.emplace_back(kv.second.name, kv.second.description);

    std::sort(out.begin(), out.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    return out;
}

// ------------------------- file I/O -------------------------

void CommandManager::LoadBindings(const std::string& filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
        return;

    // file format:
    // ERASE = Delete
    // ERASE = Ctrl+E
    std::string line;
    while (std::getline(file, line))
    {
        line = Trim(line);
        if (line.empty()) continue;
        if (line[0] == '#') continue;
        if (line.size() >= 2 && line[0] == '/' && line[1] == '/') continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string name = ToUpperCopy(Trim(line.substr(0, eq)));
        std::string rhs = Trim(line.substr(eq + 1));

        auto it = m_commands.find(name);
        if (it == m_commands.end()) continue;

        KeyChord parsed;
        if (ParseChordString(rhs, parsed))
        {
            it->second.chord = parsed;
        }
    }
}

void CommandManager::SaveBindings(const std::string& filename) const
{
    std::ofstream file(filename, std::ios::trunc);
    if (!file.is_open())
        return;

    file << "# Command shortcuts\n";
    file << "# Format: COMMAND_NAME = Ctrl+Shift+Key\n";
    file << "# Example: ERASE = Delete\n";
    file << "\n";

    for (const auto& kv : m_commands)
    {
        const Command& cmd = kv.second;
        file << cmd.name << " = " << ToChordString(cmd.chord) << "\n";
    }
}

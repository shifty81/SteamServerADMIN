#include "IniEditor.hpp"

#include <fstream>
#include <sstream>
#include <algorithm>

// ---------------------------------------------------------------------------
// Trim helper
// ---------------------------------------------------------------------------

static std::string trim(const std::string &s)
{
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// ---------------------------------------------------------------------------
// Loading / Parsing
// ---------------------------------------------------------------------------

bool IniEditor::loadFile(const std::string &path)
{
    std::ifstream f(path);
    if (!f.is_open())
        return false;
    std::string content{std::istreambuf_iterator<char>(f),
                        std::istreambuf_iterator<char>()};
    loadFromString(content);
    return true;
}

void IniEditor::loadFromString(const std::string &content)
{
    m_lines.clear();

    std::istringstream stream(content);
    std::string line;
    std::string currentSection;

    while (std::getline(stream, line)) {
        // Remove trailing CR if present (Windows line endings)
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        std::string trimmed = trim(line);

        IniLine il;
        il.rawLine = line;

        if (trimmed.empty()) {
            il.type = IniLine::Blank;
        } else if (trimmed[0] == ';' || trimmed[0] == '#') {
            il.type = IniLine::Comment;
        } else if (trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']') {
            il.type = IniLine::Section;
            il.section = trimmed.substr(1, trimmed.size() - 2);
            currentSection = il.section;
        } else {
            auto eq = trimmed.find('=');
            if (eq != std::string::npos) {
                il.type = IniLine::KeyValue;
                il.section = currentSection;
                il.key = trim(trimmed.substr(0, eq));
                il.value = trim(trimmed.substr(eq + 1));
            } else {
                // Treat as a comment (unrecognised line)
                il.type = IniLine::Comment;
            }
        }

        m_lines.push_back(il);
    }
}

// ---------------------------------------------------------------------------
// Saving / Serialising
// ---------------------------------------------------------------------------

bool IniEditor::saveFile(const std::string &path) const
{
    std::ofstream f(path, std::ios::trunc);
    if (!f.is_open())
        return false;
    f << toString();
    return true;
}

std::string IniEditor::toString() const
{
    std::string result;
    for (size_t i = 0; i < m_lines.size(); ++i) {
        const IniLine &il = m_lines[i];
        switch (il.type) {
            case IniLine::Section:
                result += "[" + il.section + "]";
                break;
            case IniLine::KeyValue:
                result += il.key + "=" + il.value;
                break;
            case IniLine::Comment:
            case IniLine::Blank:
            default:
                result += il.rawLine;
                break;
        }
        // Always add newline after each line (consistent with standard INI files)
        result += "\n";
    }
    return result;
}

// ---------------------------------------------------------------------------
// Query methods
// ---------------------------------------------------------------------------

std::vector<std::string> IniEditor::sections() const
{
    std::vector<std::string> result;
    for (const auto &il : m_lines) {
        if (il.type == IniLine::Section)
            result.push_back(il.section);
    }
    return result;
}

std::vector<std::pair<std::string, std::string>>
IniEditor::keysInSection(const std::string &section) const
{
    std::vector<std::pair<std::string, std::string>> result;
    for (const auto &il : m_lines) {
        if (il.type == IniLine::KeyValue && il.section == section)
            result.emplace_back(il.key, il.value);
    }
    return result;
}

std::string IniEditor::getValue(const std::string &section, const std::string &key) const
{
    for (const auto &il : m_lines) {
        if (il.type == IniLine::KeyValue && il.section == section && il.key == key)
            return il.value;
    }
    return {};
}

bool IniEditor::hasSection(const std::string &section) const
{
    for (const auto &il : m_lines) {
        if (il.type == IniLine::Section && il.section == section)
            return true;
    }
    return false;
}

bool IniEditor::hasKey(const std::string &section, const std::string &key) const
{
    for (const auto &il : m_lines) {
        if (il.type == IniLine::KeyValue && il.section == section && il.key == key)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Mutation methods
// ---------------------------------------------------------------------------

void IniEditor::setValue(const std::string &section, const std::string &key, const std::string &value)
{
    // Try to update an existing key
    for (auto &il : m_lines) {
        if (il.type == IniLine::KeyValue && il.section == section && il.key == key) {
            il.value = value;
            return;
        }
    }

    // Key not found — find the section and append
    int sectionEndIdx = -1;
    for (int i = 0; i < static_cast<int>(m_lines.size()); ++i) {
        if (m_lines[i].type == IniLine::Section && m_lines[i].section == section) {
            // Find the last line belonging to this section
            sectionEndIdx = i;
            for (int j = i + 1; j < static_cast<int>(m_lines.size()); ++j) {
                if (m_lines[j].type == IniLine::Section)
                    break;
                sectionEndIdx = j;
            }
            break;
        }
    }

    IniLine newLine;
    newLine.type = IniLine::KeyValue;
    newLine.section = section;
    newLine.key = key;
    newLine.value = value;

    if (sectionEndIdx >= 0) {
        // Insert after the last line in the section
        m_lines.insert(m_lines.begin() + sectionEndIdx + 1, newLine);
    } else {
        // Section doesn't exist — create it
        if (!m_lines.empty()) {
            IniLine blank;
            blank.type = IniLine::Blank;
            m_lines.push_back(blank);
        }
        IniLine sectionLine;
        sectionLine.type = IniLine::Section;
        sectionLine.section = section;
        m_lines.push_back(sectionLine);

        newLine.section = section;
        m_lines.push_back(newLine);
    }
}

bool IniEditor::removeKey(const std::string &section, const std::string &key)
{
    for (auto it = m_lines.begin(); it != m_lines.end(); ++it) {
        if (it->type == IniLine::KeyValue && it->section == section && it->key == key) {
            m_lines.erase(it);
            return true;
        }
    }
    return false;
}

const std::vector<IniEditor::IniLine> &IniEditor::lines() const
{
    return m_lines;
}

#pragma once

#include <string>
#include <vector>
#include <map>

/**
 * @brief Structured INI file parser and editor.
 *
 * Supports:
 *  - Sections [SectionName]
 *  - Key=Value pairs within sections
 *  - Comment lines (starting with ; or #) are preserved
 *  - Round-trip fidelity: loading and saving preserves comments and ordering
 */
class IniEditor {
public:
    /**
     * @brief Represents a single line in an INI file.
     *
     * Each line is either a section header, a key-value pair, a comment,
     * or a blank line.  This preserves the exact structure for round-trip.
     */
    struct IniLine {
        enum Type { Section, KeyValue, Comment, Blank };
        Type type = Blank;
        std::string section;       // for Section lines: the section name
        std::string key;           // for KeyValue lines: the key
        std::string value;         // for KeyValue lines: the value
        std::string rawLine;       // original text (for comments/blanks)
    };

    IniEditor() = default;

    /**
     * @brief Load and parse an INI file.
     * @param path Absolute path to the INI file.
     * @return true on success.
     */
    bool loadFile(const std::string &path);

    /**
     * @brief Parse INI content from a string.
     * @param content The INI file content.
     */
    void loadFromString(const std::string &content);

    /**
     * @brief Save the INI content back to a file.
     * @param path Absolute path to write to.
     * @return true on success.
     */
    bool saveFile(const std::string &path) const;

    /**
     * @brief Serialise the INI content to a string.
     * @return The full INI file text.
     */
    std::string toString() const;

    /**
     * @brief Get all section names in order of appearance.
     */
    std::vector<std::string> sections() const;

    /**
     * @brief Get all key-value pairs for a section.
     * @return Vector of {key, value} pairs in order of appearance.
     */
    std::vector<std::pair<std::string, std::string>> keysInSection(const std::string &section) const;

    /**
     * @brief Get a value for a key in a section.
     * @return The value, or empty string if not found.
     */
    std::string getValue(const std::string &section, const std::string &key) const;

    /**
     * @brief Set a value for a key in a section.
     *
     * If the key already exists it is updated in-place.
     * If the key does not exist it is appended to the section.
     * If the section does not exist it is created at the end.
     */
    void setValue(const std::string &section, const std::string &key, const std::string &value);

    /**
     * @brief Check whether a section exists.
     */
    bool hasSection(const std::string &section) const;

    /**
     * @brief Check whether a key exists in a section.
     */
    bool hasKey(const std::string &section, const std::string &key) const;

    /**
     * @brief Remove a key from a section.
     * @return true if the key was found and removed.
     */
    bool removeKey(const std::string &section, const std::string &key);

    /**
     * @brief Get the parsed lines (read-only).
     */
    const std::vector<IniLine> &lines() const;

private:
    std::vector<IniLine> m_lines;
};

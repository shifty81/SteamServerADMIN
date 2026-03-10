#pragma once

#include "portable-file-dialogs.h"

#include <string>
#include <cstring>

/**
 * @brief Helpers that open native file-picker / folder-picker dialogs
 *        and write the result into a fixed-size char buffer.
 *
 * Uses portable-file-dialogs (header-only, cross-platform).
 * Every function is blocking – the ImGui render loop will pause while the
 * OS dialog is open, which is the expected UX for modal file selection.
 */
namespace FileDialogHelper {

/// Open a native folder-picker and copy the result into @p buf.
inline void browseFolder(const char *title, char *buf, size_t bufSize)
{
    auto dir = pfd::select_folder(title).result();
    if (!dir.empty()) {
        std::strncpy(buf, dir.c_str(), bufSize - 1);
        buf[bufSize - 1] = '\0';
    }
}

/// Open a native file-open dialog (single selection) and copy the result.
inline void browseOpenFile(const char *title, char *buf, size_t bufSize,
                           const std::vector<std::string> &filters = {"All Files", "*"})
{
    auto files = pfd::open_file(title, "", filters).result();
    if (!files.empty()) {
        std::strncpy(buf, files[0].c_str(), bufSize - 1);
        buf[bufSize - 1] = '\0';
    }
}

/// Open a native save-file dialog and copy the result.
inline void browseSaveFile(const char *title, char *buf, size_t bufSize,
                           const std::vector<std::string> &filters = {"All Files", "*"})
{
    std::string defaultPath = (buf && buf[0] != '\0') ? std::string(buf) : std::string();
    auto file = pfd::save_file(title, defaultPath, filters).result();
    if (!file.empty()) {
        std::strncpy(buf, file.c_str(), bufSize - 1);
        buf[bufSize - 1] = '\0';
    }
}

} // namespace FileDialogHelper

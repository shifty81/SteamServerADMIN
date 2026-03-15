#include "ConfigBackupManager.hpp"

#include <filesystem>
#include <fstream>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <iostream>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Backup directory location
// ---------------------------------------------------------------------------

std::string ConfigBackupManager::backupDir(const std::string &configPath)
{
    fs::path p(configPath);
    return (p.parent_path() / ".ssa_config_backups").string();
}

// ---------------------------------------------------------------------------
// Create a backup
// ---------------------------------------------------------------------------

std::string ConfigBackupManager::createBackup(const std::string &configPath)
{
    if (!fs::exists(configPath))
        return {};

    std::string dir = backupDir(configPath);
    fs::create_directories(dir);

    // Generate timestamp
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char tsBuf[32];
    std::strftime(tsBuf, sizeof(tsBuf), "%Y%m%d_%H%M%S", &tm);

    fs::path origPath(configPath);
    std::string backupName = origPath.filename().string() + "." + tsBuf + ".bak";
    std::string backupPath = (fs::path(dir) / backupName).string();

    std::error_code ec;
    fs::copy_file(configPath, backupPath, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "ConfigBackupManager::createBackup: " << ec.message() << "\n";
        return {};
    }

    return backupPath;
}

// ---------------------------------------------------------------------------
// List backups
// ---------------------------------------------------------------------------

std::vector<ConfigBackupManager::BackupEntry>
ConfigBackupManager::listBackups(const std::string &configPath)
{
    std::vector<BackupEntry> entries;

    std::string dir = backupDir(configPath);
    if (!fs::exists(dir))
        return entries;

    fs::path origPath(configPath);
    std::string origName = origPath.filename().string();

    for (const auto &entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file())
            continue;
        std::string fname = entry.path().filename().string();

        // Expect: <originalName>.<YYYYMMDD_HHMMSS>.bak (at least 20 extra chars)
        if (fname.size() <= origName.size() + 5)
            continue;
        if (fname.substr(0, origName.size()) != origName)
            continue;
        if (fname.substr(fname.size() - 4) != ".bak")
            continue;

        BackupEntry be;
        be.filePath = fs::absolute(entry.path()).string();
        be.originalName = origName;

        // Extract timestamp from filename
        // Format: origName.YYYYMMDD_HHMMSS.bak
        std::string middle = fname.substr(origName.size() + 1,
                                           fname.size() - origName.size() - 5);
        // Format nicely: YYYYMMDD_HHMMSS -> YYYY-MM-DD HH:MM:SS
        if (middle.size() == 15) {
            be.timestamp = middle.substr(0, 4) + "-" + middle.substr(4, 2) + "-" +
                           middle.substr(6, 2) + " " + middle.substr(9, 2) + ":" +
                           middle.substr(11, 2) + ":" + middle.substr(13, 2);
        } else {
            be.timestamp = middle;
        }

        entries.push_back(be);
    }

    // Sort newest-first (reverse alphabetical by timestamp)
    std::sort(entries.begin(), entries.end(),
              [](const BackupEntry &a, const BackupEntry &b) {
                  return a.timestamp > b.timestamp;
              });

    return entries;
}

// ---------------------------------------------------------------------------
// Restore from backup
// ---------------------------------------------------------------------------

bool ConfigBackupManager::restoreBackup(const std::string &backupPath,
                                         const std::string &configPath)
{
    if (!fs::exists(backupPath))
        return false;

    std::error_code ec;
    fs::copy_file(backupPath, configPath, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "ConfigBackupManager::restoreBackup: " << ec.message() << "\n";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Rotate backups
// ---------------------------------------------------------------------------

void ConfigBackupManager::rotateBackups(const std::string &configPath, int maxBackups)
{
    auto entries = listBackups(configPath);
    // entries are sorted newest-first
    for (int i = maxBackups; i < static_cast<int>(entries.size()); ++i) {
        std::error_code ec;
        fs::remove(entries[i].filePath, ec);
    }
}

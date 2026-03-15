#pragma once

#include <string>
#include <vector>

/**
 * @brief Manages timestamped backups of configuration files (INI, etc.)
 *        before edits are applied, enabling revert to any previous version.
 *
 * Backups are stored in a ".ssa_config_backups" subdirectory next to the
 * original file, named as: <filename>.<timestamp>.bak
 */
class ConfigBackupManager {
public:
    /** Information about a single config backup. */
    struct BackupEntry {
        std::string filePath;        // full path to the backup file
        std::string timestamp;       // human-readable timestamp
        std::string originalName;    // original file name
    };

    /**
     * @brief Create a backup of the given config file before editing.
     * @param configPath Absolute path to the config file to back up.
     * @return The path to the backup file, or empty string on failure.
     */
    static std::string createBackup(const std::string &configPath);

    /**
     * @brief List all available backups for a given config file, newest first.
     * @param configPath Absolute path to the original config file.
     * @return List of backup entries.
     */
    static std::vector<BackupEntry> listBackups(const std::string &configPath);

    /**
     * @brief Restore a config file from a backup.
     * @param backupPath Absolute path to the backup file.
     * @param configPath Absolute path to the config file to overwrite.
     * @return true on success.
     */
    static bool restoreBackup(const std::string &backupPath, const std::string &configPath);

    /**
     * @brief Get the backup directory path for a given config file.
     * @param configPath Absolute path to the original config file.
     * @return The backup directory path.
     */
    static std::string backupDir(const std::string &configPath);

    /**
     * @brief Rotate backups, keeping only the most recent maxBackups entries.
     * @param configPath Absolute path to the original config file.
     * @param maxBackups Maximum number of backups to keep.
     */
    static void rotateBackups(const std::string &configPath, int maxBackups = 20);
};

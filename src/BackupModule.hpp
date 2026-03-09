#pragma once

#include "ServerConfig.hpp"

#include <string>
#include <vector>

/**
 * @brief Handles zipped backup and restore of server directories.
 *
 * Backups are stored as timestamped zip files inside server.backupFolder.
 * Rotation keeps at most server.keepBackups copies of each backup type.
 * Backup types: "config", "map", "mods", "all".
 */
class BackupModule {
public:
    /**
     * @brief Create a zip of sourceDir and write it to destZip.
     * @param compressionLevel  0-9 where 0=store, 1=fastest, 9=best (default 6).
     * @return true on success.
     */
    static bool createZip(const std::string &sourceDir, const std::string &destZip,
                          int compressionLevel = 6);

    /**
     * @brief Extract a zip archive into destDir.
     * @return true on success.
     */
    static bool extractZip(const std::string &zipFile, const std::string &destDir);

    /**
     * @brief Take a full snapshot (configs + maps + mods) for the server.
     * Returns the timestamp string used, or empty string on failure.
     */
    static std::string takeSnapshot(const ServerConfig &server);

    /**
     * @brief Restore a previously saved snapshot zip.
     * @param zipFile Absolute path to the snapshot zip.
     * @param server  The server to restore into.
     * @return true on success.
     */
    static bool restoreSnapshot(const std::string &zipFile, const ServerConfig &server);

    /**
     * @brief List all snapshot zip files for a server, sorted newest-first.
     */
    static std::vector<std::string> listSnapshots(const ServerConfig &server);

    /**
     * @brief Remove old backups, keeping only server.keepBackups entries.
     */
    static void rotateBackups(const ServerConfig &server);
};

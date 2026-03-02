#pragma once

#include "ServerConfig.hpp"
#include <QString>
#include <QStringList>

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
     * @return true on success.
     */
    static bool createZip(const QString &sourceDir, const QString &destZip);

    /**
     * @brief Extract a zip archive into destDir.
     * @return true on success.
     */
    static bool extractZip(const QString &zipFile, const QString &destDir);

    /**
     * @brief Take a full snapshot (configs + maps + mods) for the server.
     * Returns the timestamp string used, or empty string on failure.
     */
    static QString takeSnapshot(const ServerConfig &server);

    /**
     * @brief Restore a previously saved snapshot zip.
     * @param zipFile Absolute path to the snapshot zip.
     * @param server  The server to restore into.
     * @return true on success.
     */
    static bool restoreSnapshot(const QString &zipFile, const ServerConfig &server);

    /**
     * @brief List all snapshot zip files for a server, sorted newest-first.
     */
    static QStringList listSnapshots(const ServerConfig &server);

    /**
     * @brief Remove old backups, keeping only server.keepBackups entries.
     */
    static void rotateBackups(const ServerConfig &server);
};

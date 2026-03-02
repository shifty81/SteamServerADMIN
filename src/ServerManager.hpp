#pragma once

#include "ServerConfig.hpp"

#include <QObject>
#include <QList>
#include <QString>
#include <QProcess>
#include <QMap>

/**
 * @brief Central backend for managing all servers.
 *
 * Handles:
 *  - Loading/saving the server list (servers.json)
 *  - Starting / stopping / restarting server processes
 *  - Status checks and player-count queries
 *  - Delegating to BackupModule and SteamCmdModule
 *  - Cluster-wide operations (mod sync, config sync)
 */
class ServerManager : public QObject {
    Q_OBJECT
public:
    explicit ServerManager(const QString &configFile, QObject *parent = nullptr);

    // ---- Config persistence ----
    bool loadConfig();
    bool saveConfig() const;

    QList<ServerConfig> &servers();
    const QList<ServerConfig> &servers() const;

    // ---- Server lifecycle ----
    void startServer(ServerConfig &server);
    void stopServer(ServerConfig &server);
    void restartServer(ServerConfig &server);
    bool isServerRunning(const ServerConfig &server) const;

    // ---- SteamCMD ----
    void deployServer(ServerConfig &server);
    void updateMods(ServerConfig &server);

    // ---- Backup / restore ----
    QString takeSnapshot(const ServerConfig &server);
    bool restoreSnapshot(const QString &zipFile, const ServerConfig &server);
    QStringList listSnapshots(const ServerConfig &server) const;

    // ---- Player count (via RCON) ----
    int getPlayerCount(const ServerConfig &server);
    QString sendRconCommand(const ServerConfig &server, const QString &cmd);

    // ---- Cluster operations ----
    void syncModsCluster();
    void syncConfigsCluster(const QString &masterConfigZip);

    // ---- SteamCMD path ----
    void setSteamCmdPath(const QString &path);
    QString steamCmdPath() const;

signals:
    void logMessage(const QString &serverName, const QString &message);
    void serverCrashed(const QString &serverName);

private:
    void onProcessFinished(const QString &serverName, int exitCode,
                           QProcess::ExitStatus exitStatus);
    QString m_configFile;
    QList<ServerConfig> m_servers;
    QMap<QString, QProcess *> m_processes;                // keyed by server name
    QMap<QString, QMetaObject::Connection> m_crashConns;  // crash-detection connections
    QString m_steamCmdPath;

    QProcess *processFor(const ServerConfig &server) const;
};

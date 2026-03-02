#pragma once

#include "ServerConfig.hpp"
#include <QObject>
#include <QString>

/**
 * @brief Wraps SteamCMD to install/update game servers and workshop mods.
 *
 * All operations run via QProcess.  The steamcmdPath must point to the
 * SteamCMD binary (steamcmd.exe on Windows, steamcmd on Linux).
 */
class SteamCmdModule : public QObject {
    Q_OBJECT
public:
    explicit SteamCmdModule(QObject *parent = nullptr);

    void setSteamCmdPath(const QString &path);
    QString steamCmdPath() const;

    /**
     * @brief Install or update a game server identified by server.appid into
     *        server.dir.  Emits outputLine() during execution.
     */
    void deployServer(const ServerConfig &server);

    /**
     * @brief Download/update all mods listed in server.mods via the Steam
     *        Workshop.  Emits outputLine() during execution.
     */
    void updateMods(const ServerConfig &server);

    /**
     * @brief Download a single workshop mod.
     */
    void downloadMod(int appid, int modId);

signals:
    void outputLine(const QString &line);
    void finished(bool success);

private:
    void runSteamCmd(const QStringList &args);

    QString m_steamCmdPath;
};

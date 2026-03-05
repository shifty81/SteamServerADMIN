#pragma once

#include <QString>
#include <QList>
#include <QStringList>

struct RconInfo {
    QString host;
    int port = 27015;
    QString password;
};

struct ServerConfig {
    QString name;
    int appid = 0;
    QString dir;
    QString executable;       // server EXE, relative to dir (e.g. "ShooterGameServer.exe")
    QString launchArgs;       // extra command-line arguments passed to the server executable
    RconInfo rcon;
    QList<int> mods;
    QList<int> disabledMods;   // mods that are installed but not active on launch
    QString backupFolder;
    QString notes;             // free-form user notes / description for this server
    QString discordWebhookUrl; // Discord webhook URL for event notifications
    bool autoUpdate = true;
    bool autoStartOnLaunch = false;  // start this server when the SSA application launches
    bool favorite = false;           // pinned / favorite server (appears first in sidebar)
    int backupIntervalMinutes = 30;
    int restartIntervalHours = 24;
    int keepBackups = 10;     // maximum number of versioned backups to retain
    QStringList scheduledRconCommands; // RCON commands to run at a scheduled interval
    int rconCommandIntervalMinutes = 0; // 0 = disabled

    /**
     * @brief Validate this server configuration.
     * @return A list of human-readable error strings. Empty list means valid.
     */
    QStringList validate() const
    {
        QStringList errors;

        if (name.trimmed().isEmpty())
            errors << QStringLiteral("Server name must not be empty.");

        if (appid <= 0)
            errors << QStringLiteral("Steam AppID must be a positive integer.");

        if (dir.trimmed().isEmpty())
            errors << QStringLiteral("Installation directory must not be empty.");

        if (rcon.port < 1 || rcon.port > 65535)
            errors << QStringLiteral("RCON port must be between 1 and 65535.");

        if (keepBackups < 0)
            errors << QStringLiteral("Keep-backups count must not be negative.");

        if (backupIntervalMinutes < 0)
            errors << QStringLiteral("Backup interval must not be negative.");

        if (restartIntervalHours < 0)
            errors << QStringLiteral("Restart interval must not be negative.");

        if (rconCommandIntervalMinutes < 0)
            errors << QStringLiteral("RCON command interval must not be negative.");

        return errors;
    }
};

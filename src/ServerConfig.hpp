#pragma once

#include <QString>
#include <QList>

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
    QString backupFolder;
    bool autoUpdate = true;
    int backupIntervalMinutes = 30;
    int restartIntervalHours = 24;
    int keepBackups = 10;     // maximum number of versioned backups to retain
};

#pragma once

#include <QList>
#include <QString>

/**
 * @brief Pre-defined game server profiles for popular Steam dedicated servers.
 *
 * Provides a curated list of templates with the correct AppID, executable
 * name, and typical launch arguments so users do not have to look these up
 * manually when adding a new server.
 */
struct GameTemplate {
    QString displayName;   // human-readable game name
    int     appid;
    QString executable;    // server binary relative to install dir
    QString defaultArgs;   // typical launch arguments

    /** Return the built-in list of known game server templates. */
    static QList<GameTemplate> builtinTemplates()
    {
        return {
            { QStringLiteral("ARK: Survival Ascended"),
              2430930,
              QStringLiteral("ShooterGameServer"),
              QStringLiteral("TheIsland_WP?listen?MaxPlayers=70") },

            { QStringLiteral("Counter-Strike 2"),
              730,
              QStringLiteral("cs2"),
              QStringLiteral("-dedicated +map de_dust2") },

            { QStringLiteral("Rust"),
              258550,
              QStringLiteral("RustDedicated"),
              QStringLiteral("-batchmode +server.port 28015 +server.level Procedural Map") },

            { QStringLiteral("Valheim"),
              896660,
              QStringLiteral("valheim_server.x86_64"),
              QStringLiteral("-name MyServer -port 2456 -world Dedicated") },

            { QStringLiteral("Project Zomboid"),
              380870,
              QStringLiteral("start-server.sh"),
              QString() },

            { QStringLiteral("Palworld"),
              2394010,
              QStringLiteral("PalServer-Linux-Test"),
              QStringLiteral("-useperfthreads -NoAsyncLoadingThread -UseMultithreadForDS") },

            { QStringLiteral("Satisfactory"),
              1690800,
              QStringLiteral("FactoryServer"),
              QStringLiteral("-unattended") },

            { QStringLiteral("Custom (manual entry)"),
              0,
              QString(),
              QString() },
        };
    }
};

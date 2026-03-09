#pragma once

#include <string>
#include <vector>

/**
 * @brief Pre-defined game server profiles for popular Steam dedicated servers.
 *
 * Provides a curated list of templates with the correct AppID, executable
 * name, and typical launch arguments so users do not have to look these up
 * manually when adding a new server.
 */
struct GameTemplate {
    std::string displayName;   // human-readable game name
    int         appid;
    std::string executable;    // server binary relative to install dir
    std::string defaultArgs;   // typical launch arguments

    /** Return the built-in list of known game server templates. */
    static std::vector<GameTemplate> builtinTemplates()
    {
        return {
            { "ARK: Survival Ascended",
              2430930,
              "ShooterGameServer",
              "TheIsland_WP?listen?MaxPlayers=70" },

            { "Counter-Strike 2",
              730,
              "cs2",
              "-dedicated +map de_dust2" },

            { "Rust",
              258550,
              "RustDedicated",
              "-batchmode +server.port 28015 +server.level Procedural Map" },

            { "Valheim",
              896660,
              "valheim_server.x86_64",
              "-name MyServer -port 2456 -world Dedicated" },

            { "Project Zomboid",
              380870,
              "start-server.sh",
              "" },

            { "Palworld",
              2394010,
              "PalServer-Linux-Test",
              "-useperfthreads -NoAsyncLoadingThread -UseMultithreadForDS" },

            { "Satisfactory",
              1690800,
              "FactoryServer",
              "-unattended" },

            { "Custom (manual entry)",
              0,
              "",
              "" },
        };
    }
};

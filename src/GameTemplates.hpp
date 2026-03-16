#pragma once

#include <string>
#include <vector>

/**
 * @brief Pre-defined game server profiles for popular Steam dedicated servers.
 *
 * Provides a curated list of templates with the correct AppID, executable
 * name, typical launch arguments, and known config file paths so users do
 * not have to look these up manually when adding a new server.
 */
struct GameTemplate {
    std::string displayName;   // human-readable game name
    int         appid;
    std::string executable;    // server binary relative to install dir
    std::string defaultArgs;   // typical launch arguments
    std::string folderHint;    // short folder name hint (e.g. "ark_sa")
    std::vector<std::string> configPaths; // known config file paths relative to install dir

    /** Return the built-in list of known game server templates. */
    static std::vector<GameTemplate> builtinTemplates()
    {
        return {
            { "ARK: Survival Ascended",
              2430930,
              "ShooterGameServer",
              "TheIsland_WP?listen?MaxPlayers=70",
              "ark_sa",
              { "ShooterGame/Saved/Config/WindowsServer/GameUserSettings.ini",
                "ShooterGame/Saved/Config/WindowsServer/Game.ini" } },

            { "Counter-Strike 2",
              730,
              "cs2",
              "-dedicated +map de_dust2",
              "cs2",
              { "game/csgo/cfg/server.cfg",
                "game/csgo/cfg/gamemode_competitive.cfg" } },

            { "Rust",
              258550,
              "RustDedicated",
              "-batchmode +server.port 28015 +server.level Procedural Map",
              "rust",
              { "server/rust_server/cfg/server.cfg",
                "server/rust_server/cfg/users.cfg" } },

            { "Valheim",
              896660,
              "valheim_server.x86_64",
              "-name MyServer -port 2456 -world Dedicated",
              "valheim",
              { "adminlist.txt",
                "bannedlist.txt",
                "permittedlist.txt" } },

            { "Project Zomboid",
              380870,
              "start-server.sh",
              "",
              "project_zomboid",
              { "Server/servertest.ini",
                "Server/servertest_SandboxVars.lua" } },

            { "Palworld",
              2394010,
              "PalServer-Linux-Test",
              "-useperfthreads -NoAsyncLoadingThread -UseMultithreadForDS",
              "palworld",
              { "Pal/Saved/Config/LinuxServer/PalWorldSettings.ini",
                "Pal/Saved/Config/WindowsServer/PalWorldSettings.ini" } },

            { "Satisfactory",
              1690800,
              "FactoryServer",
              "-unattended",
              "satisfactory",
              { "FactoryGame/Saved/Config/LinuxServer/Game.ini",
                "FactoryGame/Saved/Config/LinuxServer/Engine.ini" } },

            // ---- Additional templates ----

            { "7 Days to Die",
              294420,
              "7DaysToDieServer.x86_64",
              "-configfile=serverconfig.xml",
              "7dtd",
              { "serverconfig.xml",
                "serveradmin.xml" } },

            { "Garry's Mod",
              4020,
              "srcds_run",
              "-game garrysmod +maxplayers 16 +map gm_flatgrass",
              "gmod",
              { "garrysmod/cfg/server.cfg",
                "garrysmod/cfg/autoexec.cfg" } },

            { "Team Fortress 2",
              232250,
              "srcds_run",
              "-game tf +map cp_badlands +maxplayers 24",
              "tf2",
              { "tf/cfg/server.cfg",
                "tf/cfg/autoexec.cfg" } },

            { "Left 4 Dead 2",
              222860,
              "srcds_run",
              "-game left4dead2 +map c1m1_hotel",
              "l4d2",
              { "left4dead2/cfg/server.cfg",
                "left4dead2/cfg/autoexec.cfg" } },

            { "DayZ",
              223350,
              "DayZServer_x64",
              "-config=serverDZ.cfg -port=2302",
              "dayz",
              { "serverDZ.cfg",
                "mpmissions/dayzOffline.chernarusplus/cfgeconomycore.xml" } },

            { "Conan Exiles",
              443030,
              "ConanSandboxServer.exe",
              "-log",
              "conan_exiles",
              { "ConanSandbox/Saved/Config/WindowsServer/ServerSettings.ini",
                "ConanSandbox/Saved/Config/WindowsServer/Game.ini",
                "ConanSandbox/Saved/Config/WindowsServer/Engine.ini" } },

            { "Unturned",
              1110390,
              "Unturned_Headless.x86_64",
              "+InternetServer/MyServer",
              "unturned",
              { "Servers/MyServer/Server/Commands.dat",
                "Servers/MyServer/Server/Config.json" } },

            { "The Forest",
              556450,
              "TheForestDedicatedServer",
              "",
              "the_forest",
              { "config.cfg" } },

            { "Enshrouded",
              2278520,
              "enshrouded_server.exe",
              "",
              "enshrouded",
              { "enshrouded_server.json" } },

            { "V Rising",
              1829350,
              "VRisingServer.exe",
              "",
              "v_rising",
              { "VRisingServer_Data/StreamingAssets/Settings/ServerHostSettings.json",
                "VRisingServer_Data/StreamingAssets/Settings/ServerGameSettings.json" } },

            { "Terraria (tModLoader)",
              1281930,
              "start-tModLoaderServer.sh",
              "",
              "terraria",
              { "serverconfig.txt" } },

            { "ARK: Survival Evolved",
              376030,
              "ShooterGameServer",
              "TheIsland?listen?MaxPlayers=70",
              "ark_se",
              { "ShooterGame/Saved/Config/LinuxServer/GameUserSettings.ini",
                "ShooterGame/Saved/Config/LinuxServer/Game.ini" } },

            { "Custom (manual entry)",
              0,
              "",
              "",
              "custom",
              {} },
        };
    }
};

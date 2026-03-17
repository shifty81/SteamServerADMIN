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
    int         defaultRconPort = 27015;  // default RCON port for this game

    /** Return the built-in list of known game server templates. */
    static std::vector<GameTemplate> builtinTemplates()
    {
        return {
            { "ARK: Survival Ascended",
              2430930,
#ifdef _WIN32
              "ShooterGame/Binaries/Win64/ArkAscendedServer.exe",
#else
              "ShooterGame/Binaries/Linux/ArkAscendedServer",
#endif
              "TheIsland_WP?listen?SessionName=MyServer?MaxPlayers=70?RCONEnabled=True?RCONPort=27020",
              "ark_sa",
              { "ShooterGame/Saved/Config/WindowsServer/GameUserSettings.ini",
                "ShooterGame/Saved/Config/WindowsServer/Game.ini" },
              27020 },

            { "Counter-Strike 2",
              730,
#ifdef _WIN32
              "game/bin/win64/cs2.exe",
#else
              "game/bin/linuxsteamrt64/cs2",
#endif
              "-dedicated +game_type 0 +game_mode 0 +map de_dust2 +sv_setsteamaccount \"\" -maxplayers 16",
              "cs2",
              { "game/csgo/cfg/server.cfg",
                "game/csgo/cfg/gamemode_competitive.cfg" },
              27015 },

            { "Rust",
              258550,
#ifdef _WIN32
              "RustDedicated.exe",
#else
              "RustDedicated",
#endif
              "-batchmode +server.port 28015 +rcon.port 28016 +rcon.password changeme +rcon.web 1 +server.level \"Procedural Map\" +server.hostname \"My Rust Server\"",
              "rust",
              { "server/rust_server/cfg/server.cfg",
                "server/rust_server/cfg/users.cfg" },
              28016 },

            { "Valheim",
              896660,
#ifdef _WIN32
              "valheim_server.exe",
#else
              "valheim_server.x86_64",
#endif
              "-name \"My Server\" -port 2456 -world Dedicated -password secret -public 1",
              "valheim",
              { "adminlist.txt",
                "bannedlist.txt",
                "permittedlist.txt" },
              0 },   // Valheim has no RCON by default

            { "Project Zomboid",
              380870,
#ifdef _WIN32
              "StartServer64.bat",
#else
              "start-server.sh",
#endif
              "",
              "project_zomboid",
              { "Server/servertest.ini",
                "Server/servertest_SandboxVars.lua" },
              27015 },

            { "Palworld",
              2394010,
#ifdef _WIN32
              "Pal/Binaries/Win64/PalServer-Win64-Test-Cmd.exe",
#else
              "PalServer.sh",
#endif
              "-useperfthreads -NoAsyncLoadingThread -UseMultithreadForDS",
              "palworld",
              { "Pal/Saved/Config/LinuxServer/PalWorldSettings.ini",
                "Pal/Saved/Config/WindowsServer/PalWorldSettings.ini" },
              25575 },

            { "Satisfactory",
              1690800,
#ifdef _WIN32
              "FactoryServer.exe",
#else
              "FactoryServer.sh",
#endif
              "-unattended",
              "satisfactory",
              { "FactoryGame/Saved/Config/LinuxServer/Game.ini",
                "FactoryGame/Saved/Config/LinuxServer/Engine.ini" },
              0 },   // Satisfactory has no RCON

            // ---- Additional templates ----

            { "7 Days to Die",
              294420,
#ifdef _WIN32
              "7DaysToDieServer.exe",
#else
              "7DaysToDieServer.x86_64",
#endif
              "-configfile=serverconfig.xml -logfile output_log.txt -quit -batchmode -nographics -dedicated",
              "7dtd",
              { "serverconfig.xml",
                "serveradmin.xml" },
              8081 },  // 7DTD uses telnet on 8081 by default

            { "Garry's Mod",
              4020,
#ifdef _WIN32
              "srcds.exe",
#else
              "srcds_run",
#endif
              "-game garrysmod +maxplayers 16 +map gm_flatgrass",
              "gmod",
              { "garrysmod/cfg/server.cfg",
                "garrysmod/cfg/autoexec.cfg" },
              27015 },

            { "Team Fortress 2",
              232250,
#ifdef _WIN32
              "srcds.exe",
#else
              "srcds_run",
#endif
              "-game tf +map cp_badlands +maxplayers 24",
              "tf2",
              { "tf/cfg/server.cfg",
                "tf/cfg/autoexec.cfg" },
              27015 },

            { "Left 4 Dead 2",
              222860,
#ifdef _WIN32
              "srcds.exe",
#else
              "srcds_run",
#endif
              "-game left4dead2 +map c1m1_hotel +maxplayers 4",
              "l4d2",
              { "left4dead2/cfg/server.cfg",
                "left4dead2/cfg/autoexec.cfg" },
              27015 },

            { "DayZ",
              223350,
#ifdef _WIN32
              "DayZServer_x64.exe",
#else
              "DayZServer",
#endif
              "-config=serverDZ.cfg -port=2302 -dologs -adminlog -netlog",
              "dayz",
              { "serverDZ.cfg",
                "mpmissions/dayzOffline.chernarusplus/cfgeconomycore.xml" },
              2302 },  // DayZ uses BattlEye RCON on game port

            { "Conan Exiles",
              443030,
#ifdef _WIN32
              "ConanSandboxServer.exe",
#else
              "ConanSandboxServer",
#endif
              "-log -MaxPlayers=40",
              "conan_exiles",
              { "ConanSandbox/Saved/Config/WindowsServer/ServerSettings.ini",
                "ConanSandbox/Saved/Config/WindowsServer/Game.ini",
                "ConanSandbox/Saved/Config/WindowsServer/Engine.ini" },
              25575 },

            { "Unturned",
              1110390,
#ifdef _WIN32
              "Unturned.exe",
#else
              "Unturned_Headless.x86_64",
#endif
              "+InternetServer/MyServer",
              "unturned",
              { "Servers/MyServer/Server/Commands.dat",
                "Servers/MyServer/Server/Config.json" },
              27115 },

            { "The Forest",
              556450,
#ifdef _WIN32
              "TheForestDedicatedServer.exe",
#else
              "TheForestDedicatedServer",
#endif
              "-serverip 0.0.0.0 -serversteamport 8766",
              "the_forest",
              { "config.cfg" },
              0 },   // The Forest has no RCON

            { "Enshrouded",
              2278520,
#ifdef _WIN32
              "enshrouded_server.exe",
#else
              "enshrouded_server",
#endif
              "",
              "enshrouded",
              { "enshrouded_server.json" },
              0 },   // Enshrouded has no RCON

            { "V Rising",
              1829350,
#ifdef _WIN32
              "VRisingServer.exe",
#else
              "VRisingServer",
#endif
              "",
              "v_rising",
              { "VRisingServer_Data/StreamingAssets/Settings/ServerHostSettings.json",
                "VRisingServer_Data/StreamingAssets/Settings/ServerGameSettings.json" },
              25575 },

            { "Terraria (tModLoader)",
              1281930,
#ifdef _WIN32
              "start-tModLoaderServer.bat",
#else
              "start-tModLoaderServer.sh",
#endif
              "-config serverconfig.txt",
              "terraria",
              { "serverconfig.txt" },
              7777 },

            { "ARK: Survival Evolved",
              376030,
#ifdef _WIN32
              "ShooterGame/Binaries/Win64/ShooterGameServer.exe",
#else
              "ShooterGame/Binaries/Linux/ShooterGameServer",
#endif
              "TheIsland?listen?SessionName=MyServer?MaxPlayers=70?QueryPort=27015?RCONEnabled=True?RCONPort=27020",
              "ark_se",
              { "ShooterGame/Saved/Config/LinuxServer/GameUserSettings.ini",
                "ShooterGame/Saved/Config/LinuxServer/Game.ini" },
              27020 },

            { "Custom (manual entry)",
              0,
              "",
              "",
              "custom",
              {},
              27015 },
        };
    }
};

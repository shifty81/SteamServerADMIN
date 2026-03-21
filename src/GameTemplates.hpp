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
    int         defaultRconPort  = 27015; // default RCON port for this game
    int         defaultQueryPort = 27015; // default Steam A2S query port (0 = no query support)

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
              28016, 28015 },  // Rust RCON=28016, A2S query=28015 (same as game port)

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
              0, 2457 },   // Valheim has no RCON; A2S query port is game port + 1

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
              0, 0 },   // Satisfactory has no RCON and no A2S query support

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
              8081, 26900 },  // 7DTD telnet RCON=8081, A2S query=26900

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
              0, 0 },   // The Forest has no RCON and no A2S query support

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
              0, 0 },   // Enshrouded has no RCON and no A2S query support

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

            { "Don't Starve Together",
              343050,
#ifdef _WIN32
              "bin64/dontstarve_dedicated_server_nullrenderer_x64.exe",
#else
              "bin64/dontstarve_dedicated_server_nullrenderer_x64",
#endif
              "-console -cluster MyDediServer -shard Master",
              "dst",
              { "cluster.ini",
                "cluster_token.txt" },
              0, 0 },   // DST has no RCON and no A2S query support

            { "ARMA 3",
              233780,
#ifdef _WIN32
              "arma3server_x64.exe",
#else
              "arma3server_x64",
#endif
              "-name=server -config=server.cfg -port=2302",
              "arma3",
              { "server.cfg",
                "basic.cfg" },
              2302 },  // ARMA 3 uses BattlEye RCON

            { "Squad",
              403240,
#ifdef _WIN32
              "SquadGameServer.exe",
#else
              "SquadGameServer.sh",
#endif
              "-log",
              "squad",
              { "SquadGame/ServerConfig/Server.cfg",
                "SquadGame/ServerConfig/Rcon.cfg",
                "SquadGame/ServerConfig/Admins.cfg" },
              21114 },

            { "Insurgency: Sandstorm",
              581330,
#ifdef _WIN32
              "InsurgencyServer-Win64-Shipping.exe",
#else
              "InsurgencyServer-Linux-Shipping",
#endif
              "-Port=27102 -QueryPort=27131 -log",
              "insurgency",
              { "Insurgency/Saved/Config/LinuxServer/Game.ini",
                "Insurgency/Saved/Config/LinuxServer/Engine.ini" },
              27015 },

            { "Barotrauma",
              1026340,
#ifdef _WIN32
              "DedicatedServer.exe",
#else
              "DedicatedServer",
#endif
              "",
              "barotrauma",
              { "serversettings.xml" },
              0, 0 },   // Barotrauma has no RCON and no A2S query support

            { "Space Engineers",
              298740,
#ifdef _WIN32
              "DedicatedServer64/SpaceEngineersDedicated.exe",
#else
              "DedicatedServer64/SpaceEngineersDedicated",
#endif
              "-console",
              "space_engineers",
              { "SpaceEngineers-Dedicated.cfg" },
              0, 0 },   // Space Engineers has no standard RCON or A2S

            { "Sons of the Forest",
              2465200,
#ifdef _WIN32
              "SonsOfTheForestDS.exe",
#else
              "SonsOfTheForestDS",
#endif
              "",
              "sons_of_the_forest",
              { "dedicatedserver.cfg" },
              0, 0 },   // Sons of the Forest has no RCON or A2S

            { "Mordhau",
              629800,
#ifdef _WIN32
              "MordhauServer.exe",
#else
              "MordhauServer-Linux-Shipping",
#endif
              "-log -Port=7777 -QueryPort=27015",
              "mordhau",
              { "Mordhau/Saved/Config/LinuxServer/Game.ini" },
              0, 27015 },   // Mordhau: no standard RCON; A2S query on default port 27015

            // ---- Additional popular dedicated servers ----

            { "Stationeers",
              544550,
#ifdef _WIN32
              "rocketstation_DedicatedServer.exe",
#else
              "rocketstation_DedicatedServer.x86_64",
#endif
              "-load",
              "stationeers",
              { "saves/default/world.xml" },
              27500, 27015 },   // Stationeers RCON=27500, A2S query=27015

            { "SCUM",
              996580,
#ifdef _WIN32
              "SCUM/Binaries/Win64/SCUMServer-Win64-Shipping.exe",
#else
              "SCUM/Binaries/Linux/SCUMServer-Linux-Shipping",
#endif
              "-log",
              "scum",
              { "SCUM/Saved/Config/WindowsServer/Game.ini",
                "SCUM/Saved/Config/WindowsServer/GameUserSettings.ini" },
              7779, 27016 },   // SCUM RCON=7779, A2S query=27016

            { "Arma Reforger",
              1874900,
#ifdef _WIN32
              "ArmaReforgerServer.exe",
#else
              "ArmaReforgerServer",
#endif
              "",
              "arma_reforger",
              { "config.json" },
              19999, 17777 },  // Arma Reforger RCON=19999, A2S query=17777

            { "ECO",
              382310,
#ifdef _WIN32
              "EcoServer.exe",
#else
              "EcoServer.x86_64",
#endif
              "",
              "eco",
              { "Configs/Network.eco",
                "Configs/Difficulty.eco" },
              3001, 3000 },    // ECO REST/RCON=3001, A2S query=3000

            { "Custom (manual entry)",
              0,
              "",
              "",
              "custom",
              {},
              27015, 0 },   // Custom: user configures everything manually
        };
    }
};

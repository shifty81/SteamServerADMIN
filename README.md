# SSA – Steam Server ADMIN

A cross-platform Qt6 desktop application for managing SteamCMD-based game servers (ARK, CS2, Rust, Valheim, and any other Steam dedicated server).

---

## Features

| Feature | Details |
|---|---|
| **Home Dashboard** | Live status lights (🟢/🟡/🔴) and player counts for all servers |
| **Config-driven servers** | All servers defined in `servers.json`; add new servers from the GUI without restarting |
| **SteamCMD integration** | Install and update game servers and workshop mods directly from the UI |
| **Mod management** | Per-server mod list; add / remove / update mods individually or cluster-wide |
| **Versioned backups** | Timestamped zip snapshots of configs, maps, and mods; configurable retention count |
| **Backup rotation** | Automatically keeps only the last *N* backups per type |
| **Snapshot restore** | Select any previous snapshot from the Backups tab and restore with one click |
| **Scheduled backups** | Automatic periodic snapshots driven by `backupIntervalMinutes` per server |
| **Scheduled restarts** | Automatic periodic server restarts driven by `restartIntervalHours` per server |
| **Crash detection** | Detects unexpected server exits and automatically restarts the process |
| **Auto-update on start** | When `autoUpdate` is true, mods are updated via SteamCMD before launching |
| **Pre-update snapshots** | A full snapshot is taken before every mod update, enabling safe rollback |
| **Live RCON console** | Send commands to any running server; receive responses in the built-in console |
| **Config file editor** | Edit `GameUserSettings.ini` (or any config file) with syntax-highlighted text editor |
| **Cluster sync** | Push mod updates or a master config zip to every managed server at once |
| **Server lifecycle** | Start / Stop / Restart server processes directly from the GUI |
| **Config validation** | Server configs are validated before saving; catches empty names, invalid AppIDs, port ranges, duplicates |
| **Searchable sidebar** | Instantly filter the server list by name |

---

## Quick Start

### Prerequisites

- Qt 6.4+ (Core, Widgets, Network)
- CMake 3.22+
- A C++20 compiler (GCC 12+, Clang 15+, MSVC 2022+)
- `zip` / `unzip` (Linux/macOS) **or** PowerShell (Windows) for backup compression
- [SteamCMD](https://developer.valvesoftware.com/wiki/SteamCMD) on your `PATH` (or configure the path inside the app)

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Run

```bash
./build/SSA
```

On first launch `servers.json` is created in the working directory.  
See `servers.json` in the repository root for an example configuration.

---

## Configuration (`servers.json`)

```json
[
  {
    "name": "ARK Cluster 1",
    "appid": 2430930,
    "dir": "/srv/ark_cluster1",
    "executable": "ShooterGameServer",
    "launchArgs": "TheIsland_WP?listen?MaxPlayers=70",
    "rcon": { "host": "127.0.0.1", "port": 27020, "password": "changeme" },
    "mods": [731604991, 880454836],
    "backupFolder": "/srv/backups/ark_cluster1",
    "autoUpdate": true,
    "keepBackups": 10,
    "backupIntervalMinutes": 30,
    "restartIntervalHours": 24
  }
]
```

Adding a new server = append a new object and restart, **or** use the **＋ Add Server** button in the sidebar.

---

## Project Structure

```
SSA/
├── src/
│   ├── main.cpp              # Qt entry point
│   ├── MainWindow.*          # Main window, sidebar, tab management
│   ├── HomeDashboard.*       # Landing dashboard with server health & players
│   ├── ServerTabWidget.*     # Per-server tabs (Overview/Config/Mods/Backups/Console)
│   ├── ServerManager.*       # Core backend (start/stop/deploy/backup/RCON)
│   ├── ServerConfig.hpp      # Server data structures
│   ├── BackupModule.*        # Versioned zip snapshots
│   ├── SteamCmdModule.*      # SteamCMD wrapper
│   ├── RconClient.*          # Source RCON protocol (TCP)
│   └── SchedulerModule.*     # Scheduled backups & restarts
├── tests/
│   └── test_serverconfig.cpp # Qt Test unit tests
├── CMakeLists.txt
├── servers.json              # Example server configuration
└── README.md
```

---

## Running Tests

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target SSA_Tests
ctest --test-dir build --output-on-failure
```

---

## License

See [LICENSE](LICENSE).

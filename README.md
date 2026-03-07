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
| **Export / Import** | Export any server config to JSON; import a config file to add a server |
| **Mod enable / disable** | Toggle individual mods on or off without removing them from the list |
| **Dark mode** | Switch between light and dark themes via the View menu; preference is saved |
| **Searchable sidebar** | Instantly filter the server list by name |
| **Searchable log** | Filter operation-log entries with a real-time search box |
| **System tray** | Minimize to tray; balloon notifications for crashes, backups, and clone events |
| **Server cloning** | Duplicate an existing server config with a new name in one click |
| **Server removal** | Remove a server from the configuration (stops it first if running; files on disk are preserved) |
| **Operation log** | Centralized timestamped log of all server operations, viewable in a dedicated tab |
| **Cluster summary** | Home Dashboard shows total / online / offline server counts at a glance |
| **Broadcast command** | Send an RCON command to all managed servers simultaneously |
| **Tab status indicators** | Server tabs display 🟢/🔴 status indicators that update in real time |
| **Game server templates** | Pre-defined profiles for popular games (ARK:SA, CS2, Rust, Valheim, Palworld, Satisfactory, Project Zomboid) with pre-filled AppID, executable, and launch arguments when adding a server |
| **RCON command history** | Up/Down arrow keys in the RCON console recall previously sent commands |
| **Server uptime display** | Overview tab shows how long each server has been running |
| **Crash restart backoff** | Exponential backoff on crash auto-restart (up to 5 attempts) prevents infinite restart loops |
| **Backup file sizes** | Backup list shows the size of each snapshot file |
| **Server notes** | Per-server free-form notes/description field, persisted in `servers.json` |
| **Config editor revert** | Revert button discards unsaved config edits and restores the last saved version |
| **Drag & drop mod ordering** | Reorder mods by dragging rows in the Mods tab; saved order controls load priority |
| **Mod update rollback** | Automatic rollback to the pre-update snapshot when a mod update via SteamCMD fails |
| **Server log viewer** | Dedicated Logs tab per server that displays and tails the server log file |
| **Discord webhook notifications** | Receive Discord notifications on server start/stop, crashes, and backup completions via configurable webhook URL |
| **Auto-start on launch** | Servers with `autoStartOnLaunch` enabled start automatically when the SSA application opens |
| **Scheduled RCON commands** | Run configured RCON commands at a repeating interval (e.g. broadcast messages, auto-save) |
| **Config diff preview** | Before saving config editor changes, a diff dialog shows exactly what lines were added or removed |
| **Favorite / pinned servers** | Mark servers as favorites (⭐); favorites sort to the top of the sidebar; double-click to toggle |
| **Dashboard badge cards** | Each server displayed as a rich card on the Home Dashboard with health light, uptime, player count / max slots, and pending update badges |
| **Right-click context menu** | Right-click any server badge to Save Config or Restart (with optional in-game warning countdown) |
| **Form-based server settings** | Dedicated ⚙ Settings sub-tab with labelled form fields for every server property — no raw JSON editing needed |
| **Restart warning broadcasts** | Configurable in-game RCON warnings before scheduled restarts; customisable message template with `{minutes}` placeholder |
| **Max players display** | `maxPlayers` field shows capacity alongside live player count on the dashboard |
| **Pending update indicators** | Dashboard badges highlight servers with pending game or mod updates |

---

## Quick Start

### Prerequisites

- Qt 6.4+ (Core, Widgets, Network)
- CMake 3.22+
- A C++20 compiler (GCC 12+, Clang 15+, MSVC 2022+)
- `zip` / `unzip` (Linux/macOS) **or** PowerShell (Windows) for backup compression
- [SteamCMD](https://developer.valvesoftware.com/wiki/SteamCMD) on your `PATH` (or configure the path inside the app)

### Automated Build (recommended)

The build scripts automatically detect or install Qt6 and compile the project:

**Linux / macOS:**

```bash
./scripts/build.sh            # Release build (default)
./scripts/build.sh Debug      # Debug build
```

**Windows (PowerShell):**

```powershell
.\scripts\build.ps1                  # Release build (default)
.\scripts\build.ps1 -BuildType Debug # Debug build
```

The scripts will:
1. Check for CMake
2. Detect Qt6 — install it automatically if missing (apt/dnf/pacman/brew)
3. Configure and build the project
4. Run tests if available

### Manual Build

If you prefer to build manually, or the scripts do not suit your setup:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

On Windows, if Qt6 is not on the default search path, pass its location:

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:\Qt\6.x.x\msvc20xx_64"
cmake --build build --config Release
```

### CMake Presets

CMake presets are provided for common configurations:

```bash
cmake --preset default    # Release build
cmake --preset debug      # Debug build
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
    "disabledMods": [],
    "backupFolder": "/srv/backups/ark_cluster1",
    "notes": "",
    "discordWebhookUrl": "",
    "autoUpdate": true,
    "autoStartOnLaunch": false,
    "favorite": false,
    "keepBackups": 10,
    "backupIntervalMinutes": 30,
    "restartIntervalHours": 24,
    "scheduledRconCommands": [],
    "rconCommandIntervalMinutes": 0,
    "maxPlayers": 70,
    "restartWarningMinutes": 15,
    "restartWarningMessage": ""
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
│   ├── ServerTabWidget.*     # Per-server tabs (Overview/Config/Mods/Backups/Console/Logs)
│   ├── ServerManager.*       # Core backend (start/stop/deploy/backup/RCON)
│   ├── ServerConfig.hpp      # Server data structures
│   ├── GameTemplates.hpp     # Pre-defined game server profiles
│   ├── BackupModule.*        # Versioned zip snapshots
│   ├── SteamCmdModule.*      # SteamCMD wrapper
│   ├── RconClient.*          # Source RCON protocol (TCP)
│   ├── SchedulerModule.*     # Scheduled backups, restarts & RCON commands
│   ├── LogModule.*           # Centralized operation logging
│   ├── TrayManager.*         # System tray icon & notifications
│   └── WebhookModule.*       # Discord webhook event notifications
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

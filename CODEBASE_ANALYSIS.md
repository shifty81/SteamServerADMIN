# SteamServerADMIN - Complete Codebase Architecture & Flow

## Overview
SteamServerADMIN (SSA) is a multi-server management and deployment tool built with C++, ImGui, and SteamCMD. Manages game servers with features for deployment, backup, RCON, scheduling, and monitoring.

---

## 1. ADD SERVER DIALOG FLOW

**File:** `/src/MainWindow.cpp` (lines 635-842)

### Opening
- User clicks: **File → Add Server...** or **+ Add Server** button
- Sets `m_showAddServer = true`
- Next frame renders modal popup

### Template Selection
When user selects a template:
- AppID auto-filled (e.g., 2430930 for ARK)
- Executable auto-filled (e.g., "ShooterGameServer")
- Default launch args auto-filled
- Install dir auto-generated: `servers/<game>_<name>`

### Optional: Steam Library Detection
- Click "Scan Steam Library" to list installed apps
- Select app to auto-fill AppID, Name, Directory

### User Form Entry
- Server Name
- Steam AppID
- Install Directory (browse button)
- Executable (browse button)
- Launch Arguments
- RCON: Host, Port, Password

### SteamCMD Installation Option
- Checkbox: "Install server via SteamCMD"
- User specifies SteamCMD path (or uses PATH default)

### Add Button Flow
```cpp
// Lines 780-834
if (ImGui::Button("Add")) {
    ServerConfig s;
    s.name = m_addName;
    s.appid = m_addAppId;
    s.dir = m_addDir;
    s.executable = m_addExe;
    s.launchArgs = m_addArgs;
    s.rcon.host = m_addRconHost;
    s.rcon.port = m_addRconPort;
    s.rcon.password = m_addRconPass;  // XOR+base64 obfuscated
    
    m_manager->servers().push_back(s);
    auto errors = m_manager->validateAll();
    
    if (errors.empty()) {
        m_manager->saveConfig();  // servers.json
        std::filesystem::create_directories(s.dir);
        
        // Deploy via SteamCMD if checked
        if (m_addInstallViaSteamCmd) {
            m_manager->setSteamCmdPath(m_steamCmdPath);
            m_manager->deployServer(m_manager->servers().back());
        }
        
        addServerTab(...);
        m_scheduler->startScheduler(s.name);
        m_trayManager->notify("Server Added", ...);
        ImGui::CloseCurrentPopup();
    }
}
```

---

## 2. START BUTTON FLOW

**Locations:**
- HomeDashboard (line 202): "▶ Start" on each card
- ServerTabWidget (line 155): "Start" button

**File:** `/src/ServerManager.cpp` (lines 759-796)

```cpp
void ServerManager::startServer(ServerConfig &server)
{
    // Check if already running
    if (isServerRunning(server)) {
        emitLog(server.name, "Server is already running.");
        return;
    }
    
    // Auto-update mods if enabled
    if (server.autoUpdate && !server.mods.empty())
        updateMods(server);
    
    m_crashCounts.erase(server.name);
    
    // Build exe path and arguments
    std::string exe = (fs::path(server.dir) / server.executable).string();
    std::vector<std::string> args;
    if (!server.launchArgs.empty())
        args = splitString(server.launchArgs, ' ');
    
    // Launch process
    ProcessInfo proc;
    bool ok = launchProcess(exe, args, server.environmentVariables, proc, server.dir);
    
    if (ok) {
        // Track process
        m_processes[server.name] = proc;
        m_startTimes[server.name] = std::chrono::system_clock::now();
        
        emitLog(server.name, "Server started (PID " + std::to_string(pid) + ").");
        m_resourceMonitor.trackProcess(server.name, pid);
        
        // Fire onStart hook
        if (server.eventHooks.count("onStart"))
            m_eventHookManager.fireHook(server.name, server.dir, "onStart", ...);
        
        // Send webhook
        m_webhook.sendNotification(server.discordWebhookUrl, server.name,
                                   "Server started.", server.webhookTemplate);
    } else {
        emitLog(server.name, "Failed to start server.");
    }
}
```

**Key Points:**
- Executable: `<dir>/<executable>`
- Arguments from `launchArgs` field
- Tracks PID and start time
- Auto-updates mods before starting
- Fires "onStart" event hook
- Sends Discord webhook notification

---

## 3. DEPLOY/UPDATE BUTTON FLOW

**Location:** ServerTabWidget (line 161): "Deploy / Update" button

### ServerManager Wrapper
**File:** `/src/ServerManager.cpp` (lines 973-983)

```cpp
void ServerManager::deployServer(ServerConfig &server)
{
    emitLog(server.name, "Starting SteamCMD deployment...");
    
    SteamCmdModule steamCmd;
    steamCmd.setSteamCmdPath(m_steamCmdPath);
    
    steamCmd.onOutputLine = [this, &server](const std::string &line) {
        emitLog(server.name, line);
    };
    
    steamCmd.deployServer(server);
    setPendingUpdate(server.name, false);
}
```

### SteamCmdModule Implementation
**File:** `/src/SteamCmdModule.cpp` (lines 46-70)

```cpp
bool SteamCmdModule::deployServer(const ServerConfig &server)
{
    if (!isPathSafeForShell(server.dir)) {
        onOutputLine("Server directory contains invalid characters");
        onFinished(false);
        return false;
    }
    
    try {
        fs::create_directories(server.dir);
    } catch (const fs::filesystem_error &e) {
        onOutputLine("Failed to create server directory: " + std::string(e.what()));
        onFinished(false);
        return false;
    }
    
    std::vector<std::string> args = {
        "+login", "anonymous",
        "+force_install_dir", server.dir,
        "+app_update", std::to_string(server.appid),
        "validate",
        "+quit"
    };
    return runSteamCmd(args);
}
```

### Execution
**File:** `/src/SteamCmdModule.cpp` (lines 189-218)

```cpp
bool SteamCmdModule::runSteamCmd(const std::vector<std::string> &args)
{
    std::string cmd = m_steamCmdPath;
    for (const auto &arg : args)
        cmd += " " + arg;
    cmd += " 2>&1";
    
    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        onOutputLine("Failed to launch SteamCMD");
        onFinished(false);
        return false;
    }
    
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        std::string line(buffer);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        if (!line.empty() && onOutputLine)
            onOutputLine(line);
    }
    
    int status = pclose(pipe);
    bool ok = (status == 0);
    if (onFinished) onFinished(ok);
    return ok;
}
```

**Example Command:**
```
steamcmd.exe +login anonymous +force_install_dir "C:\servers\ark_sa" +app_update 2430930 validate +quit
```

---

## 4. GAME TEMPLATES - ALL 19 TEMPLATES

**File:** `/src/GameTemplates.hpp` (lines 13-186)

### Template Structure
```cpp
struct GameTemplate {
    std::string displayName;
    int appid;
    std::string executable;
    std::string defaultArgs;
    std::string folderHint;
    std::vector<std::string> configPaths;
};
```

### Complete List with configPaths and default args:

#### ARK: Survival Ascended (2430930)
- Exe: `ShooterGameServer`
- Args: `TheIsland_WP?listen?MaxPlayers=70`
- Configs:
  - `ShooterGame/Saved/Config/WindowsServer/GameUserSettings.ini`
  - `ShooterGame/Saved/Config/WindowsServer/Game.ini`

#### Counter-Strike 2 (730)
- Exe: `cs2`
- Args: `-dedicated +map de_dust2`
- Configs:
  - `game/csgo/cfg/server.cfg`
  - `game/csgo/cfg/gamemode_competitive.cfg`

#### Rust (258550)
- Exe: `RustDedicated`
- Args: `-batchmode +server.port 28015 +server.level Procedural Map`
- Configs:
  - `server/rust_server/cfg/server.cfg`
  - `server/rust_server/cfg/users.cfg`

#### Valheim (896660)
- Exe: `valheim_server.x86_64`
- Args: `-name MyServer -port 2456 -world Dedicated`
- Configs:
  - `adminlist.txt`
  - `bannedlist.txt`
  - `permittedlist.txt`

#### Project Zomboid (380870)
- Exe: `start-server.sh`
- Args: `` (empty)
- Configs:
  - `Server/servertest.ini`
  - `Server/servertest_SandboxVars.lua`

#### Palworld (2394010)
- Exe: `PalServer-Linux-Test`
- Args: `-useperfthreads -NoAsyncLoadingThread -UseMultithreadForDS`
- Configs:
  - `Pal/Saved/Config/LinuxServer/PalWorldSettings.ini`
  - `Pal/Saved/Config/WindowsServer/PalWorldSettings.ini`

#### Satisfactory (1690800)
- Exe: `FactoryServer`
- Args: `-unattended`
- Configs:
  - `FactoryGame/Saved/Config/LinuxServer/Game.ini`
  - `FactoryGame/Saved/Config/LinuxServer/Engine.ini`

#### 7 Days to Die (294420)
- Exe: `7DaysToDieServer.x86_64`
- Args: `-configfile=serverconfig.xml`
- Configs:
  - `serverconfig.xml`
  - `serveradmin.xml`

#### Garry's Mod (4020)
- Exe: `srcds_run`
- Args: `-game garrysmod +maxplayers 16 +map gm_flatgrass`
- Configs:
  - `garrysmod/cfg/server.cfg`
  - `garrysmod/cfg/autoexec.cfg`

#### Team Fortress 2 (232250)
- Exe: `srcds_run`
- Args: `-game tf +map cp_badlands +maxplayers 24`
- Configs:
  - `tf/cfg/server.cfg`
  - `tf/cfg/autoexec.cfg`

#### Left 4 Dead 2 (222860)
- Exe: `srcds_run`
- Args: `-game left4dead2 +map c1m1_hotel`
- Configs:
  - `left4dead2/cfg/server.cfg`
  - `left4dead2/cfg/autoexec.cfg`

#### DayZ (223350)
- Exe: `DayZServer_x64`
- Args: `-config=serverDZ.cfg -port=2302`
- Configs:
  - `serverDZ.cfg`
  - `mpmissions/dayzOffline.chernarusplus/cfgeconomycore.xml`

#### Conan Exiles (443030)
- Exe: `ConanSandboxServer.exe`
- Args: `-log`
- Configs:
  - `ConanSandbox/Saved/Config/WindowsServer/ServerSettings.ini`
  - `ConanSandbox/Saved/Config/WindowsServer/Game.ini`
  - `ConanSandbox/Saved/Config/WindowsServer/Engine.ini`

#### Unturned (1110390)
- Exe: `Unturned_Headless.x86_64`
- Args: `+InternetServer/MyServer`
- Configs:
  - `Servers/MyServer/Server/Commands.dat`
  - `Servers/MyServer/Server/Config.json`

#### The Forest (556450)
- Exe: `TheForestDedicatedServer`
- Args: `` (empty)
- Configs:
  - `config.cfg`

#### Enshrouded (2278520)
- Exe: `enshrouded_server.exe`
- Args: `` (empty)
- Configs:
  - `enshrouded_server.json`

#### V Rising (1829350)
- Exe: `VRisingServer.exe`
- Args: `` (empty)
- Configs:
  - `VRisingServer_Data/StreamingAssets/Settings/ServerHostSettings.json`
  - `VRisingServer_Data/StreamingAssets/Settings/ServerGameSettings.json`

#### Terraria (tModLoader) (1281930)
- Exe: `start-tModLoaderServer.sh`
- Args: `` (empty)
- Configs:
  - `serverconfig.txt`

#### ARK: Survival Evolved Legacy (376030)
- Exe: `ShooterGameServer`
- Args: `TheIsland?listen?MaxPlayers=70`
- Configs:
  - `ShooterGame/Saved/Config/LinuxServer/GameUserSettings.ini`
  - `ShooterGame/Saved/Config/LinuxServer/Game.ini`

#### Custom (Manual Entry) (0)
- Exe: `` (manual)
- Args: `` (manual)
- Configs: `` (none)

**NOTE:** RCON passwords are NOT in templates—they are user-configured and stored in ServerConfig with XOR + base64 obfuscation.

---

## 5. STEAMCMDMODULE - COMPLETE

**File:** `/src/SteamCmdModule.hpp` & `/src/SteamCmdModule.cpp`

### deployServer() Method
See Section 3 above.

### updateMods() Method
```cpp
bool SteamCmdModule::updateMods(const ServerConfig &server)
{
    bool allOk = true;
    for (int modId : server.mods) {
        if (!downloadMod(server.appid, modId))
            allOk = false;
    }
    return allOk;
}

bool SteamCmdModule::downloadMod(int appid, int modId)
{
    std::vector<std::string> args = {
        "+login", "anonymous",
        "+workshop_download_item", std::to_string(appid), std::to_string(modId),
        "+quit"
    };
    return runSteamCmd(args);
}
```

**Example Command:**
```
steamcmd.exe +login anonymous +workshop_download_item 258550 1234567 +quit
```

### installSteamCmd() Method
Downloads official SteamCMD package:

**Windows:**
```powershell
powershell -NoProfile -Command "
  Invoke-WebRequest -Uri 'https://steamcdn-a.akamaihd.net/client/installer/steamcmd.zip'
    -OutFile 'C:\steamcmd\steamcmd.zip';
  Expand-Archive -Path 'C:\steamcmd\steamcmd.zip' -DestinationPath 'C:\steamcmd' -Force
"
```

**Linux:**
```bash
curl -sqL 'https://steamcdn-a.akamaihd.net/client/installer/steamcmd_linux.tar.gz' | tar xzf - -C '/opt/steamcmd'
```

---

## 6. SERVERMANAGER - KEY METHODS

**File:** `/src/ServerManager.hpp` & `/src/ServerManager.cpp`

### startServer(ServerConfig &server)
See Section 2 above.

### stopServer(ServerConfig &server)
**Lines 798-841:**
```cpp
void ServerManager::stopServer(ServerConfig &server)
{
    auto it = m_processes.find(server.name);
    if (it == m_processes.end() || !it->second.running) {
        emitLog(server.name, "Server is not running.");
        return;
    }
    
    // Accumulate uptime
    auto stIt = m_startTimes.find(server.name);
    if (stIt != m_startTimes.end()) {
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
            now - stIt->second).count();
        server.totalUptimeSeconds += uptime;
        saveConfig();
    }
    
    // Graceful shutdown
    ProcessInfo &proc = it->second;
    int timeoutMs = server.gracefulShutdownSeconds * 1000;
    
    if (timeoutMs <= 0) {
        killProcess(proc);
    } else {
        terminateProcess(proc);
        if (!waitForProcess(proc, timeoutMs))
            killProcess(proc);
    }
    
    cleanupProcess(proc);
    m_processes.erase(server.name);
    m_startTimes.erase(server.name);
    m_crashCounts.erase(server.name);
    m_pendingRestarts.erase(server.name);
    m_resourceMonitor.untrackProcess(server.name);
    
    emitLog(server.name, "Server stopped.");
    
    if (server.eventHooks.count("onStop"))
        m_eventHookManager.fireHook(server.name, server.dir, "onStop", ...);
    
    m_webhook.sendNotification(..., "Server stopped.", ...);
}
```

### restartServer(ServerConfig &server)
**Lines 843-847:**
```cpp
void ServerManager::restartServer(ServerConfig &server)
{
    stopServer(server);
    startServer(server);
}
```

### deployServer(ServerConfig &server)
See Section 3 above.

### updateMods(ServerConfig &server)
**Lines 985-1020:**
```cpp
bool ServerManager::updateMods(ServerConfig &server)
{
    // Take pre-update snapshot for rollback
    emitLog(server.name, "Taking pre-update snapshot...");
    std::string snapshotTs = takeSnapshot(server);
    
    // Update mods
    emitLog(server.name, "Updating mods...");
    SteamCmdModule steamCmd;
    steamCmd.setSteamCmdPath(m_steamCmdPath);
    steamCmd.onOutputLine = [this, &server](const std::string &line) {
        emitLog(server.name, line);
    };
    bool ok = steamCmd.updateMods(server);
    
    if (ok) {
        setPendingModUpdate(server.name, false);
        if (server.eventHooks.count("onUpdate"))
            m_eventHookManager.fireHook(server.name, server.dir, "onUpdate", ...);
    }
    
    // Rollback if failed
    if (!ok && !snapshotTs.empty()) {
        emitLog(server.name, "Mod update failed - rolling back to snapshot...");
        // Find and restore pre-update snapshot
    }
    return ok;
}
```

### isServerRunning(const ServerConfig &server)
**Lines 849-855:**
```cpp
bool ServerManager::isServerRunning(const ServerConfig &server) const
{
    auto it = m_processes.find(server.name);
    if (it == m_processes.end()) return false;
    ProcessInfo copy = it->second;
    return isProcessRunning(copy);
}
```

### tick()
**Lines 861-867:**
```cpp
void ServerManager::tick()
{
    checkProcesses();
    processPendingRestarts();
    m_resourceMonitor.tick();
    m_gracefulRestartManager.tick();
}
```

---

## 7. HOMEDASHBOARD - UI COMPONENTS

**File:** `/src/HomeDashboard.cpp` & `/src/HomeDashboard.hpp`

### render() - Main Dashboard
**Lines 63-129:**
```cpp
void HomeDashboard::render()
{
    // Process delayed restarts
    auto now = std::chrono::steady_clock::now();
    for (auto it = m_pendingRestarts.begin(); it != m_pendingRestarts.end(); ) {
        if (now >= it->restartAt) {
            for (ServerConfig &srv : m_manager->servers()) {
                if (srv.name == it->serverName) {
                    m_manager->restartServer(srv);
                    break;
                }
            }
            it = m_pendingRestarts.erase(it);
        } else {
            ++it;
        }
    }
    
    // Title
    ImGui::SetWindowFontScale(1.5f);
    ImGui::Text("Server Health Dashboard");
    ImGui::SetWindowFontScale(1.0f);
    
    // Summary
    auto &servers = m_manager->servers();
    int total = servers.size();
    int onlineCount = 0;
    for (const auto &s : servers) {
        if (m_manager->isServerRunning(s))
            ++onlineCount;
    }
    
    ImGui::Text("Total: %d | Online: %d (green) | Offline: %d (red)", total, onlineCount, total - onlineCount);
    
    // Server cards grid (3 per row)
    int col = 0;
    for (int i = 0; i < servers.size(); ++i) {
        if (col > 0) ImGui::SameLine();
        renderCard(servers[i], i);
        ++col;
        if (col >= 3) col = 0;
    }
}
```

### renderCard() - Individual Server Card
**Lines 135-218:**
```cpp
void HomeDashboard::renderCard(ServerConfig &server, int index)
{
    ImGui::BeginChild(...);
    
    bool online = m_manager->isServerRunning(server);
    int players = online ? m_manager->getPlayerCount(server) : -1;
    
    // Status light
    const char *light = !online ? "🔴" : (players > 0 ? "🟢" : "🟡");
    ImGui::Text("%s", light);
    ImGui::SameLine();
    ImGui::TextWrapped("%s", server.name.c_str());
    
    // Group
    std::string grp = trimString(server.group);
    if (!grp.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("[%s]", grp.c_str());
    }
    
    // Player count
    if (players >= 0) {
        if (server.maxPlayers > 0)
            ImGui::Text("Players: %d / %d", players, server.maxPlayers);
        else
            ImGui::Text("Players: %d", players);
    }
    
    // Uptime
    int64_t secs = m_manager->serverUptimeSeconds(server.name);
    ImGui::Text("Uptime: %s", formatUptime(secs).c_str());
    
    // Update badges
    if (m_manager->hasPendingUpdate(server.name))
        ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.13f, 1.0f), "⬆ Update Available");
    if (m_manager->hasPendingModUpdate(server.name))
        ImGui::TextColored(ImVec4(0.2f, 0.6f, 0.86f, 1.0f), "🔧 Mod Update Available");
    
    // Stats
    ImGui::TextDisabled("Total: %s | Crashes: %d",
                        formatTotalUptime(server.totalUptimeSeconds).c_str(),
                        server.totalCrashes);
    
    // Buttons
    if (ImGui::Button("▶ Start"))     m_manager->startServer(server);
    ImGui::SameLine();
    if (ImGui::Button("■ Stop"))      m_manager->stopServer(server);
    ImGui::SameLine();
    if (ImGui::Button("↺ Restart"))   m_manager->restartServer(server);
    ImGui::SameLine();
    if (ImGui::Button("📦 Backup"))   m_manager->takeSnapshot(server);
    
    // Context menu
    renderContextMenu(server);
    
    ImGui::EndChild();
}
```

### renderContextMenu() - Right-Click Menu
**Lines 224-254:**
```cpp
void HomeDashboard::renderContextMenu(ServerConfig &server)
{
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("💾 Save Config"))
            m_manager->saveConfig();
        
        char restartLabel[128];
        if (server.restartWarningMinutes > 0 && m_manager->isServerRunning(server))
            std::snprintf(restartLabel, sizeof(restartLabel),
                          "↺ Restart (warn %d min)", server.restartWarningMinutes);
        else
            std::snprintf(restartLabel, sizeof(restartLabel), "↺ Restart Server");
        
        if (ImGui::MenuItem(restartLabel)) {
            if (server.restartWarningMinutes > 0 && m_manager->isServerRunning(server)) {
                m_manager->sendRestartWarning(server, server.restartWarningMinutes);
                PendingWarningRestart pwr;
                pwr.serverName = server.name;
                pwr.restartAt = std::chrono::steady_clock::now()
                    + std::chrono::minutes(server.restartWarningMinutes);
                m_pendingRestarts.push_back(pwr);
            } else {
                m_manager->restartServer(server);
            }
        }
        
        ImGui::EndPopup();
    }
}
```

---

## 8. MAINWINDOW.CPP - ADD SERVER DIALOG

**File:** `/src/MainWindow.cpp` (lines 635-842)

Complete implementation of renderAddServerDialog() showing all input fields and workflow.

---

## 9. COMPLETE APPLICATION FLOW

```
User Action
    ↓
MainWindow::render() [Every Frame]
    ├─ renderMenuBar() [File, Tools, Batch]
    ├─ renderSidebar() [Server list]
    └─ renderTabArea()
        ├─ HomeDashboard::render()
        │   ├─ renderCard() [Each server]
        │   │   ├─ Button: "▶ Start" → ServerManager::startServer()
        │   │   ├─ Button: "■ Stop" → ServerManager::stopServer()
        │   │   ├─ Button: "↺ Restart" → ServerManager::restartServer()
        │   │   ├─ Button: "📦 Backup" → ServerManager::takeSnapshot()
        │   │   └─ Right-click → renderContextMenu()
        │   └─ renderContextMenu()
        │       └─ "↺ Restart (warn X min)" → Send RCON warning + Schedule restart
        │
        └─ ServerTabWidget::render() [Per-server]
            ├─ Button: "Start" → ServerManager::startServer()
            ├─ Button: "Stop" → ServerManager::stopServer()
            ├─ Button: "Restart" → ServerManager::restartServer()
            ├─ Button: "Deploy/Update" → ServerManager::deployServer()
            │   └─ SteamCmdModule::deployServer()
            │       └─ runSteamCmd(+login, +force_install_dir, +app_update, validate, +quit)
            ├─ Button: "Update All Mods" → ServerManager::updateMods()
            └─ Settings/Config tabs

Add Server Dialog:
    User clicks: "File → Add Server"
    ├─ Select GameTemplate
    ├─ Scan Steam Library (optional)
    ├─ Fill form: Name, AppID, Dir, Exe, Args, RCON
    ├─ Optional: Check "Install via SteamCMD"
    └─ Click "Add"
        ├─ Create ServerConfig
        ├─ Validate
        ├─ Save servers.json
        ├─ Create directory
        ├─ IF SteamCMD checked:
        │   └─ ServerManager::deployServer()
        │       └─ SteamCmdModule::deployServer()
        │           └─ popen() SteamCMD
        ├─ Add ServerTabWidget
        ├─ Enable Scheduler
        └─ Show notification
```

---

## SUMMARY

**Key Components:**
1. **Add Server Dialog** - Template selection, form validation, optional SteamCMD deploy
2. **Start Button** - Launch exe with args, track PID, monitor resources, fire hooks
3. **Deploy/Update Button** - SteamCMD download/validate game files via popen()
4. **19 Game Templates** - Pre-configured AppID, executable, args, and config paths
5. **SteamCmdModule** - Wrapper for SteamCMD with installServer, updateMods, installSteamCmd
6. **ServerManager** - Central orchestration for all server lifecycle operations
7. **HomeDashboard** - Rich UI with server cards, status lights, quick actions

**RCON Password Storage:** Obfuscated with XOR + base64 encoding (not encrypted, but safer than plaintext)

**Cross-Platform:** Windows (CreateProcessA) and Linux (fork/exec) process management


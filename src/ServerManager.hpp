#pragma once

#include "ServerConfig.hpp"
#include "WebhookModule.hpp"
#include "ResourceMonitor.hpp"
#include "EventHookManager.hpp"
#include "GracefulRestartManager.hpp"
#include "SteamLibraryDetector.hpp"

#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <chrono>
#include <cstdint>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <sys/types.h>
#endif

/**
 * @brief Portable process information.
 */
struct ProcessInfo {
#ifdef _WIN32
    HANDLE processHandle = nullptr;
    DWORD pid = 0;
#else
    pid_t pid = 0;
#endif
    bool running = false;
};

// Portable process helpers
bool launchProcess(const std::string &exe, const std::vector<std::string> &args,
                   const std::map<std::string, std::string> &env, ProcessInfo &out);
bool isProcessRunning(const ProcessInfo &info);
void terminateProcess(ProcessInfo &info);
void killProcess(ProcessInfo &info);

/**
 * @brief Central backend for managing all servers.
 *
 * Handles:
 *  - Loading/saving the server list (servers.json)
 *  - Starting / stopping / restarting server processes
 *  - Status checks and player-count queries
 *  - Delegating to BackupModule and SteamCmdModule
 *  - Cluster-wide operations (mod sync, config sync)
 */
class ServerManager {
public:
    explicit ServerManager(const std::string &configFile);

    // ---- Config persistence ----
    bool loadConfig();
    bool saveConfig() const;

    // ---- Validation ----
    std::vector<std::string> validateAll() const;

    std::vector<ServerConfig> &servers();
    const std::vector<ServerConfig> &servers() const;

    // ---- Server lifecycle ----
    void startServer(ServerConfig &server);
    void stopServer(ServerConfig &server);
    void restartServer(ServerConfig &server);
    bool isServerRunning(const ServerConfig &server) const;

    void autoStartServers();

    // ---- Batch server operations ----
    /** Start all servers (respecting startup priority). */
    void startAllServers();
    /** Stop all currently running servers. */
    void stopAllServers();
    /** Restart all currently running servers. */
    void restartAllServers();
    /** Start all servers belonging to the given group. */
    void startGroup(const std::string &group);
    /** Stop all running servers in the given group. */
    void stopGroup(const std::string &group);
    /** Restart all running servers in the given group. */
    void restartGroup(const std::string &group);
    /** Return the list of unique, non-empty group names across all servers. */
    std::vector<std::string> serverGroups() const;
    /** Return the number of currently running servers. */
    int runningServerCount() const;

    // ---- SteamCMD ----
    void deployServer(ServerConfig &server);
    bool updateMods(ServerConfig &server);

    // ---- Backup / restore ----
    std::string takeSnapshot(const ServerConfig &server);
    bool restoreSnapshot(const std::string &zipFile, const ServerConfig &server);
    std::vector<std::string> listSnapshots(const ServerConfig &server) const;

    // ---- Player count (via RCON) ----
    int getPlayerCount(const ServerConfig &server);
    std::string sendRconCommand(const ServerConfig &server, const std::string &cmd);

    // ---- Server removal ----
    bool removeServer(const std::string &serverName);

    // ---- Cluster operations ----
    void syncModsCluster();
    void syncConfigsCluster(const std::string &masterConfigZip);
    std::vector<std::string> broadcastRconCommand(const std::string &cmd);

    // ---- Export / Import individual server configs ----
    bool exportServerConfig(const std::string &serverName, const std::string &filePath) const;
    std::string importServerConfig(const std::string &filePath);

    // ---- SteamCMD path ----
    void setSteamCmdPath(const std::string &path);
    std::string steamCmdPath() const;

    // ---- SteamCMD installation ----
    /** Download and install SteamCMD into @p installDir. */
    bool installSteamCmd(const std::string &installDir);
    /** Check whether the configured SteamCMD binary exists on disk. */
    bool isSteamCmdInstalled() const;

    // ---- Uptime tracking ----
    std::chrono::system_clock::time_point serverStartTime(const std::string &serverName) const;
    int64_t serverUptimeSeconds(const std::string &serverName) const;

    // ---- Crash backoff ----
    static constexpr int kMaxCrashRestarts = 5;
    static constexpr int kCrashBackoffBaseMs = 2000;
    int crashCount(const std::string &serverName) const;
    void resetCrashCount(const std::string &serverName);

    // ---- Pending update tracking ----
    void setPendingUpdate(const std::string &serverName, bool pending);
    bool hasPendingUpdate(const std::string &serverName) const;
    void setPendingModUpdate(const std::string &serverName, bool pending);
    bool hasPendingModUpdate(const std::string &serverName) const;

    // ---- Restart warning ----
    void sendRestartWarning(ServerConfig &server, int minutesRemaining);

    // ---- Graceful restart with countdown ----
    GracefulRestartManager *gracefulRestartManager();

    // ---- Steam library detection ----
    SteamLibraryDetector *steamLibraryDetector();

    // ---- Resource monitoring ----
    ResourceMonitor *resourceMonitor();

    // ---- Event hooks ----
    EventHookManager *eventHookManager();

    /** Call from main loop to check for crashed processes and pending restarts. */
    void tick();

    // ---- Callbacks (replacing Qt signals) ----
    std::function<void(const std::string &serverName, const std::string &message)> onLogMessage;
    std::function<void(const std::string &serverName)> onServerCrashed;
    std::function<void(const std::string &serverName, const std::string &detail)> onResourceAlert;

private:
    void checkProcesses();
    void handleCrash(const std::string &serverName, int exitCode);
    void processPendingRestarts();

    std::string m_configFile;
    std::vector<ServerConfig> m_servers;
    std::map<std::string, ProcessInfo> m_processes;
    std::string m_steamCmdPath;

    // Uptime: stores when each server was started
    std::map<std::string, std::chrono::system_clock::time_point> m_startTimes;

    // Crash backoff
    std::map<std::string, int> m_crashCounts;

    // Pending update tracking
    std::map<std::string, bool> m_pendingUpdates;
    std::map<std::string, bool> m_pendingModUpdates;

    // Pending crash restarts: {serverName -> time_point when restart should happen}
    struct PendingRestart {
        std::chrono::steady_clock::time_point when;
    };
    std::map<std::string, PendingRestart> m_pendingRestarts;

    WebhookModule    m_webhook;
    ResourceMonitor  m_resourceMonitor;
    EventHookManager m_eventHookManager;
    GracefulRestartManager m_gracefulRestartManager;
    SteamLibraryDetector   m_steamLibraryDetector;

    void emitLog(const std::string &serverName, const std::string &msg);
    std::string lookupEventHook(const std::string &serverName, const std::string &event) const;
};

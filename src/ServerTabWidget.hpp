#pragma once

#include "ServerManager.hpp"
#include "IniEditor.hpp"
#include "ConfigBackupManager.hpp"
#include <string>
#include <vector>
#include <chrono>

/**
 * @brief Per-server tabbed widget (Dear ImGui immediate mode).
 *
 * Sub-tabs:
 *   Overview  – status light, quick actions (start/stop/restart/deploy), uptime, notes
 *   Settings  – form fields for every server property
 *   Config    – text editor for the primary config file with revert support
 *   Mods      – mod list with add/remove/update
 *   Backups   – snapshot list with take/restore actions
 *   Console   – live RCON command console with command history
 *   Logs      – live server log file viewer
 */
class ServerTabWidget {
public:
    ServerTabWidget(ServerManager *manager, ServerConfig &server);
    ~ServerTabWidget() = default;

    /** Render the full widget.  Call once per frame inside ImGui context. */
    void render();

private:
    void renderOverviewTab();
    void renderSettingsTab();
    void renderConfigTab();
    void renderModsTab();
    void renderBackupsTab();
    void renderConsoleTab();
    void renderLogsTab();

    ServerManager *m_manager;
    ServerConfig  &m_server;

    // Overview
    char m_notesBuf[4096] = {};
    int  m_cachedPlayerCount = -1;
    std::chrono::steady_clock::time_point m_lastPlayerCountRefresh;

    // Settings form buffers
    char m_settName[256]     = {};
    int  m_settAppid         = 0;
    char m_settDir[512]      = {};
    char m_settExe[256]      = {};
    char m_settArgs[512]     = {};
    char m_settRconHost[128] = {};
    int  m_settRconPort      = 27015;
    char m_settRconPass[128] = {};
    bool m_settShowRconPass  = false;      // hold-to-reveal RCON password
    char m_settBackupDir[512]= {};
    int  m_settMaxPlayers    = 0;
    bool m_settAutoUpdate    = true;
    bool m_settAutoStart     = false;
    bool m_settFavorite      = false;
    int  m_settBackupInterval = 30;
    int  m_settRestartInterval = 24;
    int  m_settKeepBackups   = 10;
    int  m_settRestartWarnMin = 15;
    char m_settRestartWarnMsg[256] = {};
    int  m_settGracefulShutdown = 10;
    int  m_settStartupPriority = 0;
    bool m_settBackupBeforeRestart = false;
    double m_settCpuAlert    = 90.0;
    double m_settMemAlert    = 0.0;
    int  m_settCompression   = 6;
    int  m_settMaintStart    = -1;
    int  m_settMaintEnd      = -1;
    bool m_settConsoleLogging = false;
    char m_settWebhookUrl[512] = {};
    char m_settWebhookTpl[512] = {};
    char m_settGroup[256]    = {};
    int  m_settRconCmdInterval = 0;
    int  m_settAutoUpdateCheck = 0;
    int  m_settQueryPort = 0;

    // Tags (comma-separated editing buffer)
    char m_settTags[1024]    = {};

    // Feedback for test buttons
    std::string m_rconTestResult;   // "OK", error message, or empty before first test
    bool        m_rconTestOk = false;
    std::string m_webhookTestResult; // "Sent." or "No URL configured.", or empty
    bool        m_webhookTestOk = false;

    // Environment variables (key=value pair editing buffers)
    char m_envKey[256]       = {};
    char m_envValue[512]     = {};

    // Scheduled RCON commands (new command editing buffer)
    char m_newRconCmd[512]   = {};

    // Event hook scripts (per-event path buffers)
    char m_hookOnStart[512]  = {};
    char m_hookOnStop[512]   = {};
    char m_hookOnCrash[512]  = {};
    char m_hookOnBackup[512] = {};
    char m_hookOnUpdate[512] = {};

    // Config editor
    char m_configBuf[65536]  = {};
    std::string m_configPath;
    std::string m_originalConfig;
    bool m_configLoaded      = false;
    int  m_configViewMode    = 0;          // 0 = raw text, 1 = INI editor

    // INI editor state
    IniEditor m_iniEditor;
    std::vector<std::string> m_iniSections;
    int  m_iniSelectedSection = 0;

    // Config backup state
    std::vector<ConfigBackupManager::BackupEntry> m_configBackups;

    // Discovered config files (from directory scan + template known paths)
    std::vector<std::string> m_discoveredConfigs;
    bool m_configsDiscovered = false;
    int  m_discoveredSelectedIdx = -1;

    // Graceful restart state
    int  m_gracefulCountdown = 10;
    char m_gracefulSaveCmd[256] = "saveworld";

    // Mods
    char m_newModId[32]      = {};

    // Console (public for history callback access)
public:
    char m_consoleCmdBuf[512] = {};
    std::string m_consoleOutput;
    std::vector<std::string> m_commandHistory;
    int  m_historyIndex      = -1;
private:

    // Log viewer
    std::string m_logContent;
    std::chrono::steady_clock::time_point m_lastLogRefresh;

    // State flags
    bool m_settingsInitialized = false;
};

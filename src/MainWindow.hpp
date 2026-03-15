#pragma once

#include "ServerManager.hpp"
#include "SteamLibraryDetector.hpp"
#include <string>
#include <vector>
#include <chrono>

struct GLFWwindow;
class HomeDashboard;
class ServerTabWidget;
class SchedulerModule;
class LogModule;
class TrayManager;

/**
 * @brief Main application window (Dear ImGui).
 *
 * Layout:
 *   ┌──────────┬────────────────────────────────────────┐
 *   │ Sidebar  │  Tab area (Home Dashboard + per-server)│
 *   │ Search   │                                        │
 *   │ List     │                                        │
 *   │ [Add]    │                                        │
 *   │ [Clone]  │                                        │
 *   │ [Sync]   │                                        │
 *   └──────────┴────────────────────────────────────────┘
 */
class MainWindow {
public:
    explicit MainWindow(GLFWwindow *window);
    ~MainWindow();

    void render();      // called every frame
    bool wantQuit() const;

private:
    // Rendering helpers
    void renderMenuBar();
    void renderSidebar();
    void renderTabArea();
    void renderLogViewerTab();
    void renderNotifications();
    void renderAddServerDialog();
    void renderCloneServerDialog();
    void renderRemoveServerDialog();
    void renderExportServerDialog();
    void renderImportServerDialog();
    void renderBroadcastDialog();
    void renderInstallSteamCmdDialog();
    void renderAboutDialog();

    void loadPreferences();
    void savePreferences() const;
    void applyTheme();

    GLFWwindow *m_window = nullptr;
    ServerManager *m_manager = nullptr;
    SchedulerModule *m_scheduler = nullptr;
    LogModule *m_logModule = nullptr;
    TrayManager *m_trayManager = nullptr;
    HomeDashboard *m_dashboard = nullptr;
    std::vector<ServerTabWidget *> m_serverTabs;

    // UI state
    bool m_wantQuit = false;
    int m_selectedTab = 0;        // 0 = Home, 1..N = servers, last = Log
    std::string m_searchText;
    std::string m_logFilterText;
    int m_selectedSidebarServer = -1;

    // Dialog state
    bool m_showAddServer = false;
    bool m_showCloneServer = false;
    bool m_showRemoveServer = false;
    bool m_showExportServer = false;
    bool m_showImportServer = false;
    bool m_showBroadcast = false;
    bool m_showAbout = false;
    bool m_showInstallSteamCmd = false;

    // Theme / preferences
    bool m_darkMode = true;  // true = dark theme (default)

    // Add server dialog fields
    int m_addTemplateIdx = 0;
    char m_addName[256] = {};
    int m_addAppId = 0;
    char m_addDir[512] = {};
    char m_addExe[256] = {};
    char m_addArgs[512] = {};
    char m_addRconHost[128] = "127.0.0.1";
    int m_addRconPort = 27015;
    char m_addRconPass[128] = {};
    bool m_addInstallViaSteamCmd = false;  // install the server via SteamCMD on add
    char m_steamCmdPath[512] = {};         // path to steamcmd binary

    // Clone dialog
    int m_cloneSourceIdx = 0;
    char m_cloneName[256] = {};

    // Export dialog
    int m_exportSourceIdx = 0;
    char m_exportPath[512] = {};

    // Broadcast dialog
    char m_broadcastCmd[512] = {};

    // Install SteamCMD dialog
    char m_installSteamCmdDir[512] = {};

    // Steam Library detection (for Add Server dialog)
    std::vector<SteamLibraryDetector::InstalledApp> m_steamLibraryApps;
    int m_steamLibSelectedIdx = -1;

    // Tick timing
    std::chrono::steady_clock::time_point m_lastTick;

    // Helpers
    void addServerTab(ServerConfig &server);
    void rebuildServerTabs();
};

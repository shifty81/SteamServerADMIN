#include "MainWindow.hpp"
#include "HomeDashboard.hpp"
#include "ServerTabWidget.hpp"
#include "SchedulerModule.hpp"
#include "LogModule.hpp"
#include "TrayManager.hpp"
#include "GameTemplates.hpp"
#include "FileDialogHelper.hpp"
#include "SteamLibraryDetector.hpp"
#include "SteamCmdModule.hpp"
#include "ConfigFileDiscovery.hpp"

#include "imgui.h"
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>

static constexpr int    kSidebarButtonCount       = 11;
static constexpr float  kNotificationTimeoutSec   = 5.0f;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool containsIgnoreCase(const std::string &haystack, const std::string &needle)
{
    if (needle.empty()) return true;
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](unsigned char a, unsigned char b) {
            return std::tolower(a) == std::tolower(b);
        });
    return it != haystack.end();
}

static int caseInsensitiveCompare(const std::string &a, const std::string &b)
{
    size_t len = std::min(a.size(), b.size());
    for (size_t i = 0; i < len; ++i) {
        int ca = std::tolower(static_cast<unsigned char>(a[i]));
        int cb = std::tolower(static_cast<unsigned char>(b[i]));
        if (ca != cb) return ca - cb;
    }
    return static_cast<int>(a.size()) - static_cast<int>(b.size());
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

MainWindow::MainWindow(GLFWwindow *window)
    : m_window(window)
    , m_lastTick(std::chrono::steady_clock::now())
{
    loadPreferences();
    applyTheme();

    m_manager = new ServerManager("servers.json");
    m_manager->loadConfig();

    // Apply persisted SteamCMD path
    if (m_steamCmdPath[0] != '\0')
        m_manager->setSteamCmdPath(m_steamCmdPath);

    m_logModule = new LogModule("ssa.log");
    m_manager->onLogMessage = [this](const std::string &server, const std::string &msg) {
        m_logModule->log(server, msg);
    };

    m_trayManager = new TrayManager();
    m_trayManager->onQuitRequested = [this]() { m_wantQuit = true; };

    m_manager->onServerCrashed = [this](const std::string &name) {
        m_trayManager->notify("Server Crashed",
                              "'" + name + "' crashed and is being restarted.");
    };

    m_dashboard = new HomeDashboard(m_manager);

    m_scheduler = new SchedulerModule(m_manager);

    // Wire up scheduled RCON commands
    m_scheduler->onScheduledRconCommand = [this](const std::string &serverName) {
        for (auto &s : m_manager->servers()) {
            if (s.name == serverName) {
                for (const auto &cmd : s.scheduledRconCommands)
                    m_manager->sendRconCommand(s, cmd);
                break;
            }
        }
    };

    // Wire up auto-update checks to set pending update indicators
    m_scheduler->onScheduledUpdateCheck = [this](const std::string &serverName) {
        m_manager->setPendingUpdate(serverName, true);
        m_logModule->log(serverName, "Periodic update check: marked as pending.");
    };

    m_scheduler->startAll();

    m_manager->autoStartServers();

    rebuildServerTabs();
}

MainWindow::~MainWindow()
{
    if (m_installThread.joinable())
        m_installThread.join();
    for (auto *tab : m_serverTabs)
        delete tab;
    delete m_dashboard;
    delete m_trayManager;
    delete m_logModule;
    delete m_scheduler;
    delete m_manager;
}

// ---------------------------------------------------------------------------
// Main render
// ---------------------------------------------------------------------------

void MainWindow::render()
{
    m_manager->tick();
    m_scheduler->tick();

    // Build per-frame status cache (one system call per server, reused everywhere)
    {
        const auto &servers = m_manager->servers();
        m_cachedRunningStatus.resize(servers.size());
        m_cachedTabLabels.resize(servers.size());
        for (size_t i = 0; i < servers.size(); ++i) {
            m_cachedRunningStatus[i] = m_manager->isServerRunning(servers[i]);
            m_cachedTabLabels[i] = m_cachedRunningStatus[i] ? "[ON] " : "[OFF] ";
            m_cachedTabLabels[i] += servers[i].name;
        }
    }

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("##MainWindow", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar);

    renderMenuBar();

    renderSidebar();
    ImGui::SameLine();
    renderTabArea();

    // ---------- Open modal popups (must be in same window scope) ----------
    if (m_showAddServer) {
        m_addTemplateIdx = 0;
        std::memset(m_addName, 0, sizeof(m_addName));
        m_addAppId = 0;
        std::memset(m_addDir, 0, sizeof(m_addDir));
        std::memset(m_addExe, 0, sizeof(m_addExe));
        std::memset(m_addArgs, 0, sizeof(m_addArgs));
        std::strncpy(m_addRconHost, "127.0.0.1", sizeof(m_addRconHost) - 1);
        m_addRconPort = 27015;
        std::memset(m_addRconPass, 0, sizeof(m_addRconPass));
        m_addInstallViaSteamCmd = true;  // default to deploying via SteamCMD
        ImGui::OpenPopup("Add Server");
        m_showAddServer = false;
    }
    if (m_showCloneServer) {
        m_cloneSourceIdx = 0;
        std::memset(m_cloneName, 0, sizeof(m_cloneName));
        if (!m_manager->servers().empty()) {
            std::snprintf(m_cloneName, sizeof(m_cloneName), "%s (Copy)",
                          m_manager->servers()[0].name.c_str());
        }
        ImGui::OpenPopup("Clone Server");
        m_showCloneServer = false;
    }
    if (m_showRemoveServer) {
        ImGui::OpenPopup("Remove Server");
        m_showRemoveServer = false;
    }
    if (m_showExportServer) {
        m_exportSourceIdx = 0;
        std::memset(m_exportPath, 0, sizeof(m_exportPath));
        if (!m_manager->servers().empty()) {
            std::snprintf(m_exportPath, sizeof(m_exportPath), "%s.json",
                          m_manager->servers()[0].name.c_str());
        }
        ImGui::OpenPopup("Export Server");
        m_showExportServer = false;
    }
    if (m_showImportServer) {
        ImGui::OpenPopup("Import Server");
        m_showImportServer = false;
    }
    if (m_showBroadcast) {
        std::memset(m_broadcastCmd, 0, sizeof(m_broadcastCmd));
        ImGui::OpenPopup("Broadcast Command");
        m_showBroadcast = false;
    }
    if (m_showInstallSteamCmd) {
        std::string defaultDir = SteamCmdModule::defaultInstallDir();
        std::strncpy(m_installSteamCmdDir, defaultDir.c_str(),
                     sizeof(m_installSteamCmdDir) - 1);
        m_installSteamCmdDir[sizeof(m_installSteamCmdDir) - 1] = '\0';
        ImGui::OpenPopup("Install SteamCMD");
        m_showInstallSteamCmd = false;
    }
    if (m_showAbout) {
        ImGui::OpenPopup("About SSA");
        m_showAbout = false;
    }

    // ---------- Render modal popups ----------
    renderAddServerDialog();
    renderCloneServerDialog();
    renderRemoveServerDialog();
    renderExportServerDialog();
    renderImportServerDialog();
    renderBroadcastDialog();
    renderInstallSteamCmdDialog();
    renderAboutDialog();

    ImGui::End();

    renderNotifications();
}

bool MainWindow::wantQuit() const
{
    return m_wantQuit;
}

// ---------------------------------------------------------------------------
// Menu bar
// ---------------------------------------------------------------------------

void MainWindow::renderMenuBar()
{
    static bool openSyncConfigsPopup = false;

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Add Server..."))    m_showAddServer = true;
            if (ImGui::MenuItem("Clone Server..."))  m_showCloneServer = true;
            if (ImGui::MenuItem("Remove Server...")) m_showRemoveServer = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Export Server...")) m_showExportServer = true;
            if (ImGui::MenuItem("Import Server...")) m_showImportServer = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Quit"))             m_wantQuit = true;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("Install SteamCMD..."))
                m_showInstallSteamCmd = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Sync Mods (All)"))
                m_manager->syncModsCluster();
            if (ImGui::MenuItem("Sync Configs (All)..."))
                openSyncConfigsPopup = true;
            if (ImGui::MenuItem("Broadcast Command..."))
                m_showBroadcast = true;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Batch")) {
            if (ImGui::MenuItem("Start All"))   m_manager->startAllServers();
            if (ImGui::MenuItem("Stop All"))    m_manager->stopAllServers();
            if (ImGui::MenuItem("Restart All")) m_manager->restartAllServers();

            auto groups = m_manager->serverGroups();
            if (!groups.empty()) {
                ImGui::Separator();
                if (ImGui::BeginMenu("Start Group")) {
                    for (const auto &g : groups) {
                        if (ImGui::MenuItem(g.c_str()))
                            m_manager->startGroup(g);
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Stop Group")) {
                    for (const auto &g : groups) {
                        if (ImGui::MenuItem(g.c_str()))
                            m_manager->stopGroup(g);
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Restart Group")) {
                    for (const auto &g : groups) {
                        if (ImGui::MenuItem(g.c_str()))
                            m_manager->restartGroup(g);
                    }
                    ImGui::EndMenu();
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Dark Theme", nullptr, m_darkMode)) {
                m_darkMode = true;
                applyTheme();
                savePreferences();
            }
            if (ImGui::MenuItem("Light Theme", nullptr, !m_darkMode)) {
                m_darkMode = false;
                applyTheme();
                savePreferences();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About SSA..."))
                m_showAbout = true;
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    // Sync configs popup (opened from menu, needs path input)
    if (openSyncConfigsPopup) {
        ImGui::OpenPopup("##SyncConfigsMenu");
        openSyncConfigsPopup = false;
    }
    if (ImGui::BeginPopup("##SyncConfigsMenu")) {
        static char syncPath[512] = {};
        ImGui::Text("Master Config Zip Path:");
        ImGui::InputText("##syncPath", syncPath, sizeof(syncPath));
        ImGui::SameLine();
        if (ImGui::Button("Browse##syncMenu")) {
            FileDialogHelper::browseOpenFile("Select Config Zip",
                syncPath, sizeof(syncPath),
                {"Zip Files", "*.zip", "All Files", "*"});
        }
        if (ImGui::Button("Sync") && syncPath[0] != '\0') {
            m_manager->syncConfigsCluster(syncPath);
            syncPath[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------
// Sidebar
// ---------------------------------------------------------------------------

void MainWindow::renderSidebar()
{
    ImGui::BeginChild("Sidebar", ImVec2(220, 0), ImGuiChildFlags_Borders);

    ImGui::TextUnformatted("Servers");
    ImGui::Separator();

    // Search box
    char searchBuf[256];
    std::strncpy(searchBuf, m_searchText.c_str(), sizeof(searchBuf) - 1);
    searchBuf[sizeof(searchBuf) - 1] = '\0';
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##search", "Search servers...", searchBuf, sizeof(searchBuf)))
        m_searchText = searchBuf;

    // Sort: favorites first, then alphabetical
    struct SortEntry {
        int index;
        const ServerConfig *config;
    };
    std::vector<SortEntry> sorted;
    const auto &servers = m_manager->servers();
    sorted.reserve(servers.size());
    for (int i = 0; i < static_cast<int>(servers.size()); ++i)
        sorted.push_back({i, &servers[i]});

    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const SortEntry &a, const SortEntry &b) {
                         if (a.config->favorite != b.config->favorite)
                             return a.config->favorite > b.config->favorite;
                         return caseInsensitiveCompare(a.config->name, b.config->name) < 0;
                     });

    // Scrollable server list — fill space above buttons
    const float buttonHeight = ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("##ServerList",
                      ImVec2(0, -(buttonHeight * kSidebarButtonCount + ImGui::GetStyle().ItemSpacing.y)));

    for (const auto &entry : sorted) {
        const ServerConfig *s = entry.config;

        if (!m_searchText.empty()) {
            bool match = containsIgnoreCase(s->name, m_searchText)
                      || containsIgnoreCase(s->group, m_searchText);
            if (!match) {
                for (const auto &tag : s->tags) {
                    if (containsIgnoreCase(tag, m_searchText)) { match = true; break; }
                }
            }
            if (!match)
                continue;
        }

        bool running = (entry.index < static_cast<int>(m_cachedRunningStatus.size()))
            ? m_cachedRunningStatus[entry.index] : false;

        // Build label: ⭐ 🟢/🟡/🔴 name
        // Use 🟡 when server has a pending update or is in graceful restart
        bool pendingUpdate = m_manager->hasPendingUpdate(s->name)
                             || m_manager->hasPendingModUpdate(s->name);
        bool inRestart = m_manager->gracefulRestartManager()
                         && m_manager->gracefulRestartManager()->isRestarting(s->name);

        std::string label;
        if (s->favorite) label += "\xe2\xad\x90 ";  // ⭐
        if (!running)
            label += "\xF0\x9F\x94\xB4 ";  // 🔴 offline
        else if (inRestart)
            label += "\xF0\x9F\x94\x84 ";  // 🔄 restarting
        else if (pendingUpdate)
            label += "\xF0\x9F\x9F\xA1 ";  // 🟡 pending update
        else
            label += "\xF0\x9F\x9F\xA2 ";  // 🟢 online
        label += s->name;
        label += "##sidebaritem";  // unique ID suffix

        bool selected = (m_selectedSidebarServer == entry.index);
        ImGui::PushID(entry.index);
        if (ImGui::Selectable(label.c_str(), selected)) {
            m_selectedSidebarServer = entry.index;
            m_selectedTab = entry.index + 1; // 0=Home, 1..N=servers
        }

        // Double-click to toggle favorite
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            m_manager->servers()[entry.index].favorite =
                !m_manager->servers()[entry.index].favorite;
            m_manager->saveConfig();
        }

        // Right-click context menu with quick actions
        if (ImGui::BeginPopupContextItem("##SidebarCtx")) {
            ImGui::TextDisabled("%s", s->name.c_str());
            ImGui::Separator();
            if (running) {
                if (ImGui::MenuItem("\xe2\x96\xa0  Stop Server"))
                    m_manager->stopServer(m_manager->servers()[entry.index]);
                if (ImGui::MenuItem("\xe2\x86\xba  Restart Server"))
                    m_manager->restartServer(m_manager->servers()[entry.index]);
            } else {
                if (ImGui::MenuItem("\xe2\x96\xb6  Start Server"))
                    m_manager->startServer(m_manager->servers()[entry.index]);
            }
            ImGui::Separator();
            if (ImGui::MenuItem(s->favorite ? "\xe2\xad\x90  Remove Favorite"
                                            : "\xe2\xad\x90  Mark as Favorite")) {
                m_manager->servers()[entry.index].favorite =
                    !m_manager->servers()[entry.index].favorite;
                m_manager->saveConfig();
            }
            if (ImGui::MenuItem("\xF0\x9F\x93\xA6  Take Backup"))
                m_manager->takeSnapshot(m_manager->servers()[entry.index]);
            ImGui::Separator();
            if (ImGui::MenuItem("\xF0\x9F\x93\x8B  Open Tab")) {
                m_selectedSidebarServer = entry.index;
                m_selectedTab = entry.index + 1;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    ImGui::EndChild(); // ##ServerList

    // Action buttons
    const float w = ImGui::GetContentRegionAvail().x;
    if (ImGui::Button("+ Add Server",      ImVec2(w, 0))) m_showAddServer = true;
    if (ImGui::Button("Clone Server",       ImVec2(w, 0))) m_showCloneServer = true;
    if (ImGui::Button("- Remove Server",    ImVec2(w, 0))) m_showRemoveServer = true;
    if (ImGui::Button("Export Server",      ImVec2(w, 0))) m_showExportServer = true;
    if (ImGui::Button("Import Server",      ImVec2(w, 0))) m_showImportServer = true;
    if (ImGui::Button("Sync Mods",          ImVec2(w, 0)))
        m_manager->syncModsCluster();

    if (ImGui::Button("Sync Configs",       ImVec2(w, 0)))
        ImGui::OpenPopup("##SyncConfigsSidebar");
    if (ImGui::BeginPopup("##SyncConfigsSidebar")) {
        static char cfgPath[512] = {};
        ImGui::Text("Master Config Zip Path:");
        ImGui::InputText("##cfgPath", cfgPath, sizeof(cfgPath));
        ImGui::SameLine();
        if (ImGui::Button("Browse##syncSidebar")) {
            FileDialogHelper::browseOpenFile("Select Config Zip",
                cfgPath, sizeof(cfgPath),
                {"Zip Files", "*.zip", "All Files", "*"});
        }
        if (ImGui::Button("Sync") && cfgPath[0] != '\0') {
            m_manager->syncConfigsCluster(cfgPath);
            cfgPath[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ImGui::Button("Broadcast Cmd",      ImVec2(w, 0))) m_showBroadcast = true;
    if (ImGui::Button("Start All",          ImVec2(w, 0))) m_manager->startAllServers();
    if (ImGui::Button("Stop All",           ImVec2(w, 0))) m_manager->stopAllServers();
    if (ImGui::Button("Restart All",        ImVec2(w, 0))) m_manager->restartAllServers();

    ImGui::EndChild(); // Sidebar
}

// ---------------------------------------------------------------------------
// Tab area
// ---------------------------------------------------------------------------

void MainWindow::renderTabArea()
{
    // NoScrollbar/NoScrollWithMouse keeps the tab bar permanently visible;
    // each tab's content area provides its own scrollable child.
    ImGui::BeginChild("##TabArea", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (ImGui::BeginTabBar("##MainTabs")) {
        // Home tab (index 0)
        {
            ImGuiTabItemFlags f = 0;
            if (m_selectedTab == 0) f |= ImGuiTabItemFlags_SetSelected;
            if (ImGui::BeginTabItem("Home", nullptr, f)) {
                ImGui::BeginChild("##HomeContent", ImVec2(0, 0));
                m_dashboard->render();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }

        // Per-server tabs (index 1..N)
        for (int i = 0; i < static_cast<int>(m_serverTabs.size()); ++i) {
            int tabIdx = i + 1;
            ImGuiTabItemFlags f = 0;
            if (m_selectedTab == tabIdx) f |= ImGuiTabItemFlags_SetSelected;

            // Use cached tab label (built once per frame in render())
            const char *label = (i < static_cast<int>(m_cachedTabLabels.size()))
                ? m_cachedTabLabels[i].c_str()
                : "Server";

            ImGui::PushID(i);
            if (ImGui::BeginTabItem(label, nullptr, f)) {
                m_serverTabs[i]->render();
                ImGui::EndTabItem();
            }
            ImGui::PopID();
        }

        // Log tab (last)
        {
            int logIdx = static_cast<int>(m_serverTabs.size()) + 1;
            ImGuiTabItemFlags f = 0;
            if (m_selectedTab == logIdx) f |= ImGuiTabItemFlags_SetSelected;
            if (ImGui::BeginTabItem("Log", nullptr, f)) {
                renderLogViewerTab();
                ImGui::EndTabItem();
            }
        }

        ImGui::EndTabBar();
    }

    // Consume programmatic tab selection after one frame
    m_selectedTab = -1;

    ImGui::EndChild(); // ##TabArea
}

// ---------------------------------------------------------------------------
// Log viewer tab
// ---------------------------------------------------------------------------

void MainWindow::renderLogViewerTab()
{
    ImGui::Text("Operation Log");
    ImGui::Separator();

    // Filter input
    char filterBuf[256];
    std::strncpy(filterBuf, m_logFilterText.c_str(), sizeof(filterBuf) - 1);
    filterBuf[sizeof(filterBuf) - 1] = '\0';
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##logFilter", "Filter log entries...",
                                 filterBuf, sizeof(filterBuf)))
        m_logFilterText = filterBuf;

    // Build the filtered log text for display
    static std::string filteredLogText;
    filteredLogText.clear();
    const auto entries = m_logModule->entries();
    for (const auto &entry : entries) {
        if (!m_logFilterText.empty() && !containsIgnoreCase(entry, m_logFilterText))
            continue;
        filteredLogText += entry;
        filteredLogText += '\n';
    }

    // Copy to clipboard button
    if (ImGui::Button("📋 Copy All")) {
        ImGui::SetClipboardText(filteredLogText.c_str());
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(Select text below with mouse to highlight, Ctrl+C to copy)");

    // Use InputTextMultiline in read-only mode so users can highlight and
    // copy log entries for debugging.
    ImVec2 avail = ImGui::GetContentRegionAvail();
    // Copy into a mutable buffer to avoid const_cast UB
    static std::vector<char> logDisplayBuf;
    logDisplayBuf.assign(filteredLogText.begin(), filteredLogText.end());
    logDisplayBuf.push_back('\0');
    ImGui::InputTextMultiline("##LogEntries",
                              logDisplayBuf.data(),
                              logDisplayBuf.size(), avail,
                              ImGuiInputTextFlags_ReadOnly);
}

// ---------------------------------------------------------------------------
// Notifications (toast overlay in top-right corner)
// ---------------------------------------------------------------------------

void MainWindow::renderNotifications()
{
    // Persistent display list (function-local for single MainWindow instance)
    static std::vector<TrayManager::Notification> active;

    // Consume new notifications from TrayManager
    auto fresh = m_trayManager->consumeNotifications();
    for (auto &n : fresh)
        active.push_back(std::move(n));

    // Remove expired notifications (older than 5 seconds)
    auto now = std::chrono::steady_clock::now();
    active.erase(
        std::remove_if(active.begin(), active.end(),
            [&now](const TrayManager::Notification &n) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    now - n.timestamp);
                return elapsed.count() >= static_cast<long long>(kNotificationTimeoutSec);
            }),
        active.end());

    if (active.empty()) return;

    const ImGuiViewport *vp = ImGui::GetMainViewport();
    float y = vp->WorkPos.y + 40.0f;

    for (size_t i = 0; i < active.size(); ++i) {
        const auto &n = active[i];
        float age = std::chrono::duration<float>(now - n.timestamp).count();
        float alpha = (age > kNotificationTimeoutSec - 1.0f)
            ? (kNotificationTimeoutSec - age) : 1.0f;
        if (alpha <= 0.0f) continue;

        ImGui::SetNextWindowPos(
            ImVec2(vp->WorkPos.x + vp->WorkSize.x - 320.0f, y));
        ImGui::SetNextWindowSize(ImVec2(300.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.85f * alpha);

        char winId[64];
        std::snprintf(winId, sizeof(winId), "##Notif%zu", i);

        ImGui::Begin(winId, nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", n.title.c_str());
        ImGui::TextWrapped("%s", n.message.c_str());
        ImGui::PopStyleVar();

        y += ImGui::GetWindowHeight() + 8.0f;
        ImGui::End();
    }
}

// ---------------------------------------------------------------------------
// Add Server dialog
// ---------------------------------------------------------------------------

void MainWindow::renderAddServerDialog()
{
    if (!ImGui::BeginPopupModal("Add Server", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    static std::string validationError;

    // Template combo
    auto templates = GameTemplate::builtinTemplates();
    const char *previewName = (m_addTemplateIdx >= 0 &&
                               m_addTemplateIdx < static_cast<int>(templates.size()))
        ? templates[m_addTemplateIdx].displayName.c_str()
        : "Select...";

    if (ImGui::BeginCombo("Game Template", previewName)) {
        for (int i = 0; i < static_cast<int>(templates.size()); ++i) {
            bool sel = (m_addTemplateIdx == i);
            if (ImGui::Selectable(templates[i].displayName.c_str(), sel)) {
                m_addTemplateIdx = i;
                m_addAppId = templates[i].appid;
                std::strncpy(m_addExe, templates[i].executable.c_str(),
                             sizeof(m_addExe) - 1);
                m_addExe[sizeof(m_addExe) - 1] = '\0';
                std::strncpy(m_addArgs, templates[i].defaultArgs.c_str(),
                             sizeof(m_addArgs) - 1);
                m_addArgs[sizeof(m_addArgs) - 1] = '\0';

                // Set the game-specific default RCON port
                if (templates[i].defaultRconPort > 0)
                    m_addRconPort = templates[i].defaultRconPort;

                // Set the game-specific default A2S query port
                m_addQueryPort = templates[i].defaultQueryPort;

                // Auto-generate install directory: servers/<game>_<name>
                std::string folder = ConfigFileDiscovery::generateFolderName(
                    templates[i].folderHint, m_addName);
                std::string autoDir = (std::filesystem::path(
                    ConfigFileDiscovery::defaultServersBaseDir()) / folder).string();
                std::strncpy(m_addDir, autoDir.c_str(), sizeof(m_addDir) - 1);
                m_addDir[sizeof(m_addDir) - 1] = '\0';
            }
        }
        ImGui::EndCombo();
    }

    // --- Steam Library Detection ---
    ImGui::Separator();
    ImGui::Text("Detect Installed Steam Games");
    if (ImGui::Button("Scan Steam Library")) {
        m_steamLibraryApps = m_manager->steamLibraryDetector()->detect();
    }
    if (!m_steamLibraryApps.empty()) {
        ImGui::SameLine();
        ImGui::Text("(%d apps found)", static_cast<int>(m_steamLibraryApps.size()));

        ImGui::BeginChild("##SteamLibList", ImVec2(0, 150), ImGuiChildFlags_Borders);
        for (int i = 0; i < static_cast<int>(m_steamLibraryApps.size()); ++i) {
            const auto &app = m_steamLibraryApps[i];
            char label[512];
            std::snprintf(label, sizeof(label), "%s (AppID: %d)", app.name.c_str(), app.appid);
            if (ImGui::Selectable(label, m_steamLibSelectedIdx == i)) {
                m_steamLibSelectedIdx = i;
                // Auto-fill form from detected app
                m_addAppId = app.appid;
                std::strncpy(m_addName, app.name.c_str(), sizeof(m_addName) - 1);
                m_addName[sizeof(m_addName) - 1] = '\0';
                std::strncpy(m_addDir, app.installDir.c_str(), sizeof(m_addDir) - 1);
                m_addDir[sizeof(m_addDir) - 1] = '\0';
            }
        }
        ImGui::EndChild();
    }

    ImGui::Separator();
    if (ImGui::InputText("Server Name",       m_addName, sizeof(m_addName))) {
        // Auto-update install directory when server name changes
        if (m_addTemplateIdx >= 0 && m_addTemplateIdx < static_cast<int>(templates.size())) {
            std::string folder = ConfigFileDiscovery::generateFolderName(
                templates[m_addTemplateIdx].folderHint, m_addName);
            std::string autoDir = (std::filesystem::path(
                ConfigFileDiscovery::defaultServersBaseDir()) / folder).string();
            std::strncpy(m_addDir, autoDir.c_str(), sizeof(m_addDir) - 1);
            m_addDir[sizeof(m_addDir) - 1] = '\0';
        }
    }
    ImGui::InputInt("Steam AppID",        &m_addAppId);
    ImGui::InputText("Install Directory", m_addDir,  sizeof(m_addDir));
    ImGui::SameLine();
    if (ImGui::Button("Browse##addDir")) {
        FileDialogHelper::browseFolder("Select Install Directory",
            m_addDir, sizeof(m_addDir));
    }
    ImGui::InputText("Executable",        m_addExe,  sizeof(m_addExe));
    ImGui::SameLine();
    if (ImGui::Button("Browse##addExe")) {
        FileDialogHelper::browseOpenFile("Select Executable",
            m_addExe, sizeof(m_addExe),
            {"All Files", "*"});
    }
    ImGui::InputText("Launch Arguments",  m_addArgs, sizeof(m_addArgs));

    ImGui::Separator();
    ImGui::Text("RCON Settings");
    ImGui::InputText("Host",     m_addRconHost, sizeof(m_addRconHost));
    ImGui::InputInt("Port",      &m_addRconPort);
    {
        ImGuiInputTextFlags passFlags = m_addShowRconPass ? 0 : ImGuiInputTextFlags_Password;
        ImGui::InputText("Password", m_addRconPass, sizeof(m_addRconPass), passFlags);
        ImGui::SameLine();
        ImGui::Button("Show##addRconPass");
        m_addShowRconPass = ImGui::IsItemActive();
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Hold 'Show' to reveal.\n"
                              "Stored with XOR + base64 obfuscation in servers.json\n"
                              "(not encrypted, but better than plain text).");
    }
    ImGui::InputInt("A2S Query Port (0 = disabled)", &m_addQueryPort);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "UDP port for Steam A2S_INFO queries.\n"
            "Enables player count display for servers without RCON.\n"
            "Auto-filled from template. 0 = disabled.");

    // SteamCMD install option
    ImGui::Separator();
    ImGui::Text("SteamCMD Installation");
    ImGui::Checkbox("Install server via SteamCMD", &m_addInstallViaSteamCmd);
    if (m_addInstallViaSteamCmd) {
        // Pre-populate the path field from the manager's configured path when
        // the buffer is empty (e.g. first use or after the "Install SteamCMD"
        // dialog was used to set it).
        if (m_steamCmdPath[0] == '\0' && m_manager->isSteamCmdInstalled()) {
            std::string managerPath = m_manager->steamCmdPath();
            std::strncpy(m_steamCmdPath, managerPath.c_str(), sizeof(m_steamCmdPath) - 1);
            m_steamCmdPath[sizeof(m_steamCmdPath) - 1] = '\0';
        }
        ImGui::InputText("SteamCMD Path", m_steamCmdPath, sizeof(m_steamCmdPath));
        ImGui::SameLine();
        if (ImGui::Button("Browse##steamcmd")) {
            FileDialogHelper::browseOpenFile("Select SteamCMD Executable",
                m_steamCmdPath, sizeof(m_steamCmdPath),
                {"All Files", "*"});
        }
        if (m_steamCmdPath[0] == '\0')
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                "SteamCMD path is empty – using default from PATH.");
    }

    // Validation error display
    if (!validationError.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s",
                           validationError.c_str());

    ImGui::Separator();
    if (ImGui::Button("Add", ImVec2(120, 0))) {
        ServerConfig s;
        s.name         = m_addName;
        s.appid        = m_addAppId;
        s.dir          = m_addDir;
        s.executable   = m_addExe;
        s.launchArgs   = m_addArgs;
        s.backupFolder = std::string(m_addDir) + "/Backups";
        s.rcon.host    = m_addRconHost;
        s.rcon.port    = m_addRconPort;
        s.rcon.password = m_addRconPass;
        s.queryPort    = m_addQueryPort;

        m_manager->servers().push_back(s);
        auto errors = m_manager->validateAll();
        if (!errors.empty()) {
            m_manager->servers().pop_back();
            validationError.clear();
            for (const auto &e : errors)
                validationError += e + "\n";
        } else {
            validationError.clear();
            m_manager->saveConfig();
            addServerTab(m_manager->servers().back());
            m_dashboard->refresh();
            m_scheduler->startScheduler(s.name);
            m_logModule->log(s.name, "Server added.");

            // Always create the install directory so the server is ready to use.
            // A non-empty dir that passes validation is expected to be a well-formed
            // path; the filesystem_error catch handles any OS-level rejection.
            if (!s.dir.empty()) {
                try {
                    std::filesystem::create_directories(s.dir);
                    m_logModule->log(s.name, "Install directory created: " + s.dir);
                } catch (const std::filesystem::filesystem_error &e) {
                    std::string dirErr = std::string("Error: could not create install directory: ") + e.what();
                    m_logModule->log(s.name, dirErr);
                    m_trayManager->notify("Directory Error", dirErr);
                }
            }

            // Deploy (install game files) via SteamCMD if requested
            if (m_addInstallViaSteamCmd) {
                if (m_steamCmdPath[0] != '\0')
                    m_manager->setSteamCmdPath(m_steamCmdPath);
                m_logModule->log(s.name, "Scheduling SteamCMD deployment...");
                // Run the deploy asynchronously so the UI does not freeze.
                // The new server's Overview tab will display the progress.
                if (!m_serverTabs.empty()) {
                    ServerTabWidget *newTab = m_serverTabs.back();
                    newTab->startDeployAsync();
                }
                savePreferences();
            }

            m_trayManager->notify("Server Added",
                                  "'" + s.name + "' has been added.");
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        validationError.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Clone Server dialog
// ---------------------------------------------------------------------------

void MainWindow::renderCloneServerDialog()
{
    if (!ImGui::BeginPopupModal("Clone Server", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    static std::string validationError;
    const auto &servers = m_manager->servers();

    if (servers.empty()) {
        ImGui::Text("No servers to clone.");
        if (ImGui::Button("OK", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    // Source server combo
    const char *previewSource =
        (m_cloneSourceIdx >= 0 &&
         m_cloneSourceIdx < static_cast<int>(servers.size()))
        ? servers[m_cloneSourceIdx].name.c_str()
        : "Select...";

    if (ImGui::BeginCombo("Source Server", previewSource)) {
        for (int i = 0; i < static_cast<int>(servers.size()); ++i) {
            bool sel = (m_cloneSourceIdx == i);
            if (ImGui::Selectable(servers[i].name.c_str(), sel)) {
                m_cloneSourceIdx = i;
                std::snprintf(m_cloneName, sizeof(m_cloneName), "%s (Copy)",
                              servers[i].name.c_str());
            }
        }
        ImGui::EndCombo();
    }

    ImGui::InputText("New Server Name", m_cloneName, sizeof(m_cloneName));

    if (!validationError.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s",
                           validationError.c_str());

    ImGui::Separator();
    if (ImGui::Button("Clone", ImVec2(120, 0))) {
        std::string newName = m_cloneName;

        ServerConfig cloned = servers[m_cloneSourceIdx];
        cloned.name = newName;

        m_manager->servers().push_back(cloned);
        auto errors = m_manager->validateAll();
        if (!errors.empty()) {
            m_manager->servers().pop_back();
            validationError.clear();
            for (const auto &e : errors)
                validationError += e + "\n";
        } else {
            validationError.clear();
            m_manager->saveConfig();
            addServerTab(m_manager->servers().back());
            m_dashboard->refresh();
            m_scheduler->startScheduler(newName);
            m_logModule->log(newName, "Cloned from '" +
                             servers[m_cloneSourceIdx].name + "'.");
            m_trayManager->notify("Server Cloned",
                                  "'" + newName + "' cloned from '" +
                                  servers[m_cloneSourceIdx].name + "'.");
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        validationError.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Remove Server dialog
// ---------------------------------------------------------------------------

void MainWindow::renderRemoveServerDialog()
{
    if (!ImGui::BeginPopupModal("Remove Server", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    static int removeIdx = 0;
    const auto &servers = m_manager->servers();

    if (servers.empty()) {
        ImGui::Text("No servers to remove.");
        if (ImGui::Button("OK", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    if (removeIdx >= static_cast<int>(servers.size()))
        removeIdx = 0;

    const char *previewRemove = servers[removeIdx].name.c_str();
    if (ImGui::BeginCombo("Server", previewRemove)) {
        for (int i = 0; i < static_cast<int>(servers.size()); ++i) {
            bool sel = (removeIdx == i);
            if (ImGui::Selectable(servers[i].name.c_str(), sel))
                removeIdx = i;
        }
        ImGui::EndCombo();
    }

    ImGui::TextWrapped("This will stop the server if running.\n"
                       "Server files on disk are NOT deleted.");

    ImGui::Separator();
    if (ImGui::Button("Remove", ImVec2(120, 0))) {
        std::string name = servers[removeIdx].name;
        m_scheduler->stopScheduler(name);
        m_manager->removeServer(name);
        m_manager->saveConfig();
        rebuildServerTabs();
        m_dashboard->refresh();
        m_logModule->log(name, "Server removed.");
        m_trayManager->notify("Server Removed",
                              "'" + name + "' has been removed.");
        removeIdx = 0;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Export Server dialog
// ---------------------------------------------------------------------------

void MainWindow::renderExportServerDialog()
{
    if (!ImGui::BeginPopupModal("Export Server", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    const auto &servers = m_manager->servers();

    if (servers.empty()) {
        ImGui::Text("No servers to export.");
        if (ImGui::Button("OK", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    if (m_exportSourceIdx >= static_cast<int>(servers.size()))
        m_exportSourceIdx = 0;

    const char *previewExport = servers[m_exportSourceIdx].name.c_str();
    if (ImGui::BeginCombo("Server", previewExport)) {
        for (int i = 0; i < static_cast<int>(servers.size()); ++i) {
            bool sel = (m_exportSourceIdx == i);
            if (ImGui::Selectable(servers[i].name.c_str(), sel)) {
                m_exportSourceIdx = i;
                std::snprintf(m_exportPath, sizeof(m_exportPath), "%s.json",
                              servers[i].name.c_str());
            }
        }
        ImGui::EndCombo();
    }

    ImGui::InputText("Export Path", m_exportPath, sizeof(m_exportPath));
    ImGui::SameLine();
    if (ImGui::Button("Browse##export")) {
        FileDialogHelper::browseSaveFile("Export Server Config",
            m_exportPath, sizeof(m_exportPath),
            {"JSON Files", "*.json", "All Files", "*"});
    }

    ImGui::Separator();
    static std::string exportStatus;

    if (ImGui::Button("Export", ImVec2(120, 0)) && m_exportPath[0] != '\0') {
        std::string name = servers[m_exportSourceIdx].name;
        if (m_manager->exportServerConfig(name, m_exportPath)) {
            m_logModule->log(name,
                             "Config exported to " + std::string(m_exportPath));
            m_trayManager->notify("Export Complete",
                                  "'" + name + "' exported.");
            exportStatus.clear();
            ImGui::CloseCurrentPopup();
        } else {
            exportStatus = "Failed to export '" + name + "'.";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        exportStatus.clear();
        ImGui::CloseCurrentPopup();
    }

    if (!exportStatus.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s",
                           exportStatus.c_str());

    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Import Server dialog
// ---------------------------------------------------------------------------

void MainWindow::renderImportServerDialog()
{
    if (!ImGui::BeginPopupModal("Import Server", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    static char importPath[512] = {};
    static std::string importError;

    ImGui::Text("Path to server config JSON:");
    ImGui::InputText("##importPath", importPath, sizeof(importPath));
    ImGui::SameLine();
    if (ImGui::Button("Browse##import")) {
        FileDialogHelper::browseOpenFile("Select Server Config",
            importPath, sizeof(importPath),
            {"JSON Files", "*.json", "All Files", "*"});
    }

    if (!importError.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s",
                           importError.c_str());

    ImGui::Separator();
    if (ImGui::Button("Import", ImVec2(120, 0)) && importPath[0] != '\0') {
        std::string error = m_manager->importServerConfig(importPath);
        if (!error.empty()) {
            importError = error;
        } else {
            importError.clear();
            ServerConfig &imported = m_manager->servers().back();
            m_manager->saveConfig();
            addServerTab(imported);
            m_dashboard->refresh();
            m_scheduler->startScheduler(imported.name);
            m_logModule->log(imported.name,
                             "Config imported from " + std::string(importPath));
            m_trayManager->notify("Server Imported",
                                  "'" + imported.name + "' imported.");
            importPath[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        importError.clear();
        importPath[0] = '\0';
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Broadcast Command dialog
// ---------------------------------------------------------------------------

void MainWindow::renderBroadcastDialog()
{
    if (!ImGui::BeginPopupModal("Broadcast Command", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    if (m_manager->servers().empty()) {
        ImGui::Text("No servers configured.");
        if (ImGui::Button("OK", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    static std::string resultText;

    ImGui::Text("RCON command to send to ALL servers:");
    ImGui::InputText("##broadcastCmd", m_broadcastCmd, sizeof(m_broadcastCmd));

    if (!resultText.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", resultText.c_str());
    }

    ImGui::Separator();
    if (ImGui::Button("Send", ImVec2(120, 0)) && m_broadcastCmd[0] != '\0') {
        auto results = m_manager->broadcastRconCommand(m_broadcastCmd);
        resultText.clear();
        for (const auto &r : results)
            resultText += r + "\n";
        if (resultText.empty())
            resultText = "Command sent (no responses).";
    }
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(120, 0))) {
        resultText.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Install SteamCMD dialog
// ---------------------------------------------------------------------------

void MainWindow::renderInstallSteamCmdDialog()
{
    if (!ImGui::BeginPopupModal("Install SteamCMD", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    bool alreadyInstalled = m_manager->isSteamCmdInstalled();
    if (alreadyInstalled) {
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f),
                           "SteamCMD is already installed at:");
        ImGui::TextWrapped("%s", m_manager->steamCmdPath().c_str());
        ImGui::Separator();
    }

    ImGui::Text("Install Directory:");
    ImGui::InputText("##installDir", m_installSteamCmdDir, sizeof(m_installSteamCmdDir));
    ImGui::SameLine();
    if (ImGui::Button("Browse##installDir")) {
        FileDialogHelper::browseFolder("Select Install Directory",
            m_installSteamCmdDir, sizeof(m_installSteamCmdDir));
    }

    ImGui::Separator();

    if (m_installRunning) {
        // Animated spinner
        static const char *spinFrames[] = {"|", "/", "-", "\\"};
        int frame = static_cast<int>(ImGui::GetTime() * 8.0) % 4;
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                           "%s Installing SteamCMD...", spinFrames[frame]);
    } else if (m_installDone) {
        if (m_installSuccess)
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f),
                               "SteamCMD installed successfully at: %s",
                               m_manager->steamCmdPath().c_str());
        else
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                               "Failed to install SteamCMD. Check the log below.");
    }

    // Scrolling install log output
    if (m_installRunning || m_installDone) {
        ImGui::BeginChild("##InstallLog", ImVec2(500, 200), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_HorizontalScrollbar);
        {
            std::lock_guard<std::mutex> lk(m_installLogMutex);
            for (const auto &line : m_installLog)
                ImGui::TextUnformatted(line.c_str());
        }
        if (m_installRunning)
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }

    ImGui::Separator();

    if (!m_installRunning) {
        if (ImGui::Button("Install", ImVec2(120, 0)) && m_installSteamCmdDir[0] != '\0') {
            m_installRunning = true;
            m_installDone    = false;
            m_installSuccess = false;
            {
                std::lock_guard<std::mutex> lk(m_installLogMutex);
                m_installLog.clear();
            }
            // Register log observer so install output appears in the dialog
            m_manager->setDeployLogObserver("SSA",
                [this](const std::string &line) {
                    std::lock_guard<std::mutex> lk(m_installLogMutex);
                    m_installLog.push_back(line);
                });
            if (m_installThread.joinable())
                m_installThread.join();
            std::string installDir(m_installSteamCmdDir);
            m_installThread = std::thread([this, installDir]() {
                bool ok = m_manager->installSteamCmd(installDir);
                m_manager->clearDeployLogObserver("SSA");
                if (ok) {
                    // Persist the newly installed path so it survives app restarts
                    std::string newPath = m_manager->steamCmdPath();
                    std::strncpy(m_steamCmdPath, newPath.c_str(), sizeof(m_steamCmdPath) - 1);
                    m_steamCmdPath[sizeof(m_steamCmdPath) - 1] = '\0';
                    savePreferences();
                }
                m_installSuccess = ok;
                m_installRunning = false;
                m_installDone    = true;
            });
        }
        ImGui::SameLine();
    }

    if (ImGui::Button("Close", ImVec2(120, 0))) {
        // If install is still running, just close the dialog; the thread
        // continues in the background and will be joined in the destructor.
        if (!m_installRunning && m_installThread.joinable())
            m_installThread.join();
        m_installDone    = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Server tab helpers
// ---------------------------------------------------------------------------

void MainWindow::addServerTab(ServerConfig &server)
{
    m_serverTabs.push_back(new ServerTabWidget(m_manager, server));
}

void MainWindow::rebuildServerTabs()
{
    for (auto *tab : m_serverTabs)
        delete tab;
    m_serverTabs.clear();

    for (auto &s : m_manager->servers())
        addServerTab(s);

    m_selectedTab = 0; // Reset to Home after rebuild
}

// ---------------------------------------------------------------------------
// About dialog
// ---------------------------------------------------------------------------

void MainWindow::renderAboutDialog()
{
    if (!ImGui::BeginPopupModal("About SSA", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::Text("SSA \xe2\x80\x93 Steam Server ADMIN");
    ImGui::Separator();
    ImGui::Text("A cross-platform Dear ImGui / GLFW desktop application");
    ImGui::Text("for managing SteamCMD-based game servers.");
    ImGui::Spacing();
    ImGui::Text("Built with Dear ImGui, GLFW, nlohmann/json");
    ImGui::Text("License: see LICENSE file");
    ImGui::Spacing();
    ImGui::Text("https://github.com/shifty81/SteamServerADMIN");
    ImGui::Spacing();
    if (ImGui::Button("OK", ImVec2(120, 0)))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Preferences (theme persistence)
// ---------------------------------------------------------------------------

static const char *kPreferencesFile = "ssa_preferences.json";

void MainWindow::loadPreferences()
{
    std::ifstream f(kPreferencesFile);
    if (!f.is_open())
        return;

    try {
        nlohmann::json j;
        f >> j;
        if (j.contains("darkMode") && j["darkMode"].is_boolean())
            m_darkMode = j["darkMode"].get<bool>();
        if (j.contains("steamCmdPath") && j["steamCmdPath"].is_string()) {
            std::string path = j["steamCmdPath"].get<std::string>();
            std::strncpy(m_steamCmdPath, path.c_str(), sizeof(m_steamCmdPath) - 1);
            m_steamCmdPath[sizeof(m_steamCmdPath) - 1] = '\0';
        }
    } catch (...) {
        // Ignore malformed preferences
    }
}

void MainWindow::savePreferences() const
{
    nlohmann::json j;
    j["darkMode"] = m_darkMode;
    j["steamCmdPath"] = std::string(m_steamCmdPath);

    std::ofstream f(kPreferencesFile);
    if (f.is_open())
        f << j.dump(2) << "\n";
}

void MainWindow::applyTheme()
{
    if (m_darkMode) {
        ImGui::StyleColorsDark();

        ImGuiStyle &style = ImGui::GetStyle();

        // Rounding & spacing for a modern look
        style.WindowRounding    = 6.0f;
        style.ChildRounding     = 6.0f;
        style.FrameRounding     = 4.0f;
        style.GrabRounding      = 4.0f;
        style.PopupRounding     = 4.0f;
        style.ScrollbarRounding = 6.0f;
        style.TabRounding       = 4.0f;
        style.FramePadding      = ImVec2(8, 4);
        style.ItemSpacing       = ImVec2(8, 6);
        style.WindowPadding     = ImVec2(10, 10);
        style.ScrollbarSize     = 14.0f;
        style.GrabMinSize       = 12.0f;
        style.WindowBorderSize  = 1.0f;
        style.ChildBorderSize   = 1.0f;
        style.FrameBorderSize   = 0.0f;

        // Refined dark colour palette with subtle accent tints
        ImVec4 *c = style.Colors;
        c[ImGuiCol_WindowBg]           = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
        c[ImGuiCol_ChildBg]            = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
        c[ImGuiCol_PopupBg]            = ImVec4(0.11f, 0.11f, 0.14f, 0.96f);
        c[ImGuiCol_Border]             = ImVec4(0.22f, 0.22f, 0.28f, 0.60f);
        c[ImGuiCol_FrameBg]            = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
        c[ImGuiCol_FrameBgHovered]     = ImVec4(0.22f, 0.22f, 0.28f, 1.00f);
        c[ImGuiCol_FrameBgActive]      = ImVec4(0.26f, 0.26f, 0.34f, 1.00f);
        c[ImGuiCol_TitleBg]            = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
        c[ImGuiCol_TitleBgActive]      = ImVec4(0.12f, 0.12f, 0.16f, 1.00f);
        c[ImGuiCol_MenuBarBg]          = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
        c[ImGuiCol_ScrollbarBg]        = ImVec4(0.10f, 0.10f, 0.12f, 0.60f);
        c[ImGuiCol_ScrollbarGrab]      = ImVec4(0.30f, 0.30f, 0.38f, 1.00f);
        c[ImGuiCol_ScrollbarGrabHovered]= ImVec4(0.38f, 0.38f, 0.48f, 1.00f);
        c[ImGuiCol_ScrollbarGrabActive]= ImVec4(0.44f, 0.44f, 0.56f, 1.00f);
        c[ImGuiCol_CheckMark]          = ImVec4(0.40f, 0.72f, 1.00f, 1.00f);
        c[ImGuiCol_SliderGrab]         = ImVec4(0.40f, 0.72f, 1.00f, 1.00f);
        c[ImGuiCol_SliderGrabActive]   = ImVec4(0.50f, 0.80f, 1.00f, 1.00f);
        c[ImGuiCol_Button]             = ImVec4(0.20f, 0.20f, 0.26f, 1.00f);
        c[ImGuiCol_ButtonHovered]      = ImVec4(0.28f, 0.36f, 0.52f, 1.00f);
        c[ImGuiCol_ButtonActive]       = ImVec4(0.30f, 0.50f, 0.78f, 1.00f);
        c[ImGuiCol_Header]             = ImVec4(0.22f, 0.22f, 0.28f, 1.00f);
        c[ImGuiCol_HeaderHovered]      = ImVec4(0.30f, 0.38f, 0.54f, 0.80f);
        c[ImGuiCol_HeaderActive]       = ImVec4(0.34f, 0.50f, 0.78f, 1.00f);
        c[ImGuiCol_Separator]          = ImVec4(0.24f, 0.24f, 0.30f, 1.00f);
        c[ImGuiCol_SeparatorHovered]   = ImVec4(0.40f, 0.55f, 0.80f, 0.78f);
        c[ImGuiCol_SeparatorActive]    = ImVec4(0.40f, 0.55f, 0.80f, 1.00f);
        c[ImGuiCol_Tab]                = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
        c[ImGuiCol_TabHovered]         = ImVec4(0.30f, 0.42f, 0.62f, 0.80f);
        c[ImGuiCol_TabSelected]        = ImVec4(0.24f, 0.36f, 0.56f, 1.00f);
        c[ImGuiCol_TextSelectedBg]     = ImVec4(0.30f, 0.50f, 0.80f, 0.35f);
    } else {
        ImGui::StyleColorsLight();

        ImGuiStyle &style = ImGui::GetStyle();
        style.WindowRounding    = 6.0f;
        style.ChildRounding     = 6.0f;
        style.FrameRounding     = 4.0f;
        style.GrabRounding      = 4.0f;
        style.PopupRounding     = 4.0f;
        style.ScrollbarRounding = 6.0f;
        style.TabRounding       = 4.0f;
        style.FramePadding      = ImVec2(8, 4);
        style.ItemSpacing       = ImVec2(8, 6);
        style.WindowPadding     = ImVec2(10, 10);
    }
}

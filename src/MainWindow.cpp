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

#include "imgui.h"
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>

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

        if (!m_searchText.empty() && !containsIgnoreCase(s->name, m_searchText))
            continue;

        bool running = m_manager->isServerRunning(*s);

        // Build label: [*] [ON/OFF] name
        std::string label;
        if (s->favorite) label += "* ";
        label += running ? "[ON] " : "[OFF] ";
        label += s->name;

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
    ImGui::BeginChild("##TabArea");

    if (ImGui::BeginTabBar("##MainTabs")) {
        // Home tab (index 0)
        {
            ImGuiTabItemFlags f = 0;
            if (m_selectedTab == 0) f |= ImGuiTabItemFlags_SetSelected;
            if (ImGui::BeginTabItem("Home", nullptr, f)) {
                m_dashboard->render();
                ImGui::EndTabItem();
            }
        }

        // Per-server tabs (index 1..N)
        const auto &servers = m_manager->servers();
        for (int i = 0; i < static_cast<int>(m_serverTabs.size()); ++i) {
            int tabIdx = i + 1;
            ImGuiTabItemFlags f = 0;
            if (m_selectedTab == tabIdx) f |= ImGuiTabItemFlags_SetSelected;

            // Status indicator in tab title
            std::string label;
            if (i < static_cast<int>(servers.size())) {
                bool running = m_manager->isServerRunning(servers[i]);
                label = running ? "[ON] " : "[OFF] ";
                label += servers[i].name;
            } else {
                label = "Server " + std::to_string(i);
            }

            ImGui::PushID(i);
            if (ImGui::BeginTabItem(label.c_str(), nullptr, f)) {
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

    // Scrollable log area
    ImGui::BeginChild("##LogEntries", ImVec2(0, 0), ImGuiChildFlags_Borders);

    const auto entries = m_logModule->entries();
    for (const auto &entry : entries) {
        if (!m_logFilterText.empty() && !containsIgnoreCase(entry, m_logFilterText))
            continue;
        ImGui::TextUnformatted(entry.c_str());
    }

    // Auto-scroll when already at the bottom
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);

    ImGui::EndChild();
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
    ImGui::InputText("Server Name",       m_addName, sizeof(m_addName));
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

    // SteamCMD install option
    ImGui::Separator();
    ImGui::Text("SteamCMD Installation");
    ImGui::Checkbox("Install server via SteamCMD", &m_addInstallViaSteamCmd);
    if (m_addInstallViaSteamCmd) {
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

            // Install via SteamCMD if requested
            if (m_addInstallViaSteamCmd) {
                if (m_steamCmdPath[0] != '\0')
                    m_manager->setSteamCmdPath(m_steamCmdPath);
                m_logModule->log(s.name, "Starting SteamCMD installation...");
                m_manager->deployServer(m_manager->servers().back());
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

    static std::string statusText;
    static bool installRunning = false;

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

    if (!statusText.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", statusText.c_str());
    }

    ImGui::Separator();

    if (installRunning) {
        ImGui::TextDisabled("Installing...");
    } else {
        if (ImGui::Button("Install", ImVec2(120, 0)) && m_installSteamCmdDir[0] != '\0') {
            installRunning = true;
            statusText = "Installing SteamCMD...";
            bool ok = m_manager->installSteamCmd(m_installSteamCmdDir);
            if (ok) {
                statusText = "SteamCMD installed successfully at " +
                             m_manager->steamCmdPath();
            } else {
                statusText = "Failed to install SteamCMD. Check logs for details.";
            }
            installRunning = false;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(120, 0))) {
        statusText.clear();
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
    if (m_darkMode)
        ImGui::StyleColorsDark();
    else
        ImGui::StyleColorsLight();
}

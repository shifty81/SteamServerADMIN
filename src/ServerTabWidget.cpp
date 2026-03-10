#include "ServerTabWidget.hpp"
#include "BackupModule.hpp"
#include "ConsoleLogWriter.hpp"
#include "FileDialogHelper.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void copyStr(char *dest, size_t destSize, const std::string &src)
{
    std::strncpy(dest, src.c_str(), destSize - 1);
    dest[destSize - 1] = '\0';
}

static std::string readFileToString(const std::string &path)
{
    std::ifstream f(path);
    if (!f.is_open())
        return {};
    return std::string{std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>()};
}

static std::vector<std::string> splitLines(const std::string &text)
{
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line))
        lines.push_back(line);
    return lines;
}

static std::string formatUptime(int64_t secs)
{
    if (secs < 0) return "\xe2\x80\x93"; // –
    int d = static_cast<int>(secs / 86400);
    int h = static_cast<int>((secs % 86400) / 3600);
    int m = static_cast<int>((secs % 3600) / 60);
    char buf[64];
    if (d > 0) std::snprintf(buf, sizeof(buf), "%dd %dh %dm", d, h, m);
    else if (h > 0) std::snprintf(buf, sizeof(buf), "%dh %dm", h, m);
    else std::snprintf(buf, sizeof(buf), "%dm", m);
    return buf;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ServerTabWidget::ServerTabWidget(ServerManager *manager, ServerConfig &server)
    : m_manager(manager), m_server(server)
{
    copyStr(m_notesBuf, sizeof(m_notesBuf), m_server.notes);
}

// ---------------------------------------------------------------------------
// Main render
// ---------------------------------------------------------------------------

void ServerTabWidget::render()
{
    if (ImGui::BeginTabBar("##ServerSubTabs")) {
        if (ImGui::BeginTabItem("Overview"))  { renderOverviewTab();  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Settings"))  { renderSettingsTab();  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Config"))    { renderConfigTab();    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Mods"))      { renderModsTab();      ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Backups"))   { renderBackupsTab();   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Console"))   { renderConsoleTab();   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Logs"))      { renderLogsTab();      ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
}

// ---------------------------------------------------------------------------
// Overview
// ---------------------------------------------------------------------------

void ServerTabWidget::renderOverviewTab()
{
    bool running = m_manager->isServerRunning(m_server);
    int64_t uptime = m_manager->serverUptimeSeconds(m_server.name);
    int players = m_manager->getPlayerCount(m_server);

    // Status
    ImGui::SeparatorText("Status");
    if (running)
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "ONLINE");
    else
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "OFFLINE");
    ImGui::SameLine();
    ImGui::Text("  Players: %d / %d   Uptime: %s",
                players, m_server.maxPlayers, formatUptime(uptime).c_str());

    // Server control
    ImGui::SeparatorText("Server Control");
    if (ImGui::Button("Start"))   m_manager->startServer(m_server);
    ImGui::SameLine();
    if (ImGui::Button("Stop"))    m_manager->stopServer(m_server);
    ImGui::SameLine();
    if (ImGui::Button("Restart")) m_manager->restartServer(m_server);
    ImGui::SameLine();
    if (ImGui::Button("Deploy / Update")) m_manager->deployServer(m_server);

    // Notes
    ImGui::SeparatorText("Notes");
    ImGui::InputTextMultiline("##Notes", m_notesBuf, sizeof(m_notesBuf),
                              ImVec2(-1, 100));
    if (ImGui::Button("Save Notes")) {
        m_server.notes = m_notesBuf;
        m_manager->saveConfig();
    }

    // Auto-start checkbox
    bool autoStart = m_server.autoStartOnLaunch;
    if (ImGui::Checkbox("Auto-start on launch", &autoStart)) {
        m_server.autoStartOnLaunch = autoStart;
        m_manager->saveConfig();
    }

    // Statistics
    ImGui::SeparatorText("Statistics");
    ImGui::Text("Total Uptime: %s  |  Total Crashes: %d",
                formatUptime(m_server.totalUptimeSeconds).c_str(),
                m_server.totalCrashes);
    if (!m_server.lastCrashTime.empty())
        ImGui::Text("Last Crash: %s", m_server.lastCrashTime.c_str());
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void ServerTabWidget::renderSettingsTab()
{
    if (!m_settingsInitialized) {
        copyStr(m_settName,       sizeof(m_settName),       m_server.name);
        m_settAppid = m_server.appid;
        copyStr(m_settDir,        sizeof(m_settDir),        m_server.dir);
        copyStr(m_settExe,        sizeof(m_settExe),        m_server.executable);
        copyStr(m_settArgs,       sizeof(m_settArgs),       m_server.launchArgs);
        copyStr(m_settRconHost,   sizeof(m_settRconHost),   m_server.rcon.host);
        m_settRconPort = m_server.rcon.port;
        copyStr(m_settRconPass,   sizeof(m_settRconPass),   m_server.rcon.password);
        copyStr(m_settBackupDir,  sizeof(m_settBackupDir),  m_server.backupFolder);
        m_settMaxPlayers      = m_server.maxPlayers;
        m_settAutoUpdate      = m_server.autoUpdate;
        m_settAutoStart       = m_server.autoStartOnLaunch;
        m_settFavorite        = m_server.favorite;
        m_settBackupInterval  = m_server.backupIntervalMinutes;
        m_settRestartInterval = m_server.restartIntervalHours;
        m_settKeepBackups     = m_server.keepBackups;
        m_settRestartWarnMin  = m_server.restartWarningMinutes;
        copyStr(m_settRestartWarnMsg, sizeof(m_settRestartWarnMsg), m_server.restartWarningMessage);
        m_settGracefulShutdown    = m_server.gracefulShutdownSeconds;
        m_settStartupPriority     = m_server.startupPriority;
        m_settBackupBeforeRestart = m_server.backupBeforeRestart;
        m_settCpuAlert            = m_server.cpuAlertThreshold;
        m_settMemAlert            = m_server.memAlertThresholdMB;
        m_settCompression         = m_server.backupCompressionLevel;
        m_settMaintStart          = m_server.maintenanceStartHour;
        m_settMaintEnd            = m_server.maintenanceEndHour;
        m_settConsoleLogging      = m_server.consoleLogging;
        copyStr(m_settWebhookUrl, sizeof(m_settWebhookUrl), m_server.discordWebhookUrl);
        copyStr(m_settWebhookTpl, sizeof(m_settWebhookTpl), m_server.webhookTemplate);
        copyStr(m_settGroup,      sizeof(m_settGroup),      m_server.group);
        m_settRconCmdInterval     = m_server.rconCommandIntervalMinutes;
        m_settAutoUpdateCheck     = m_server.autoUpdateCheckIntervalMinutes;
        m_settingsInitialized = true;
    }

    ImGui::SeparatorText("Server Identity");
    ImGui::InputText("Name",           m_settName,     sizeof(m_settName));
    ImGui::InputInt("AppID",           &m_settAppid);
    ImGui::InputText("Directory",      m_settDir,      sizeof(m_settDir));
    ImGui::SameLine();
    if (ImGui::Button("Browse##settDir")) {
        FileDialogHelper::browseFolder("Select Server Directory",
            m_settDir, sizeof(m_settDir));
    }
    ImGui::InputText("Executable",     m_settExe,      sizeof(m_settExe));
    ImGui::SameLine();
    if (ImGui::Button("Browse##settExe")) {
        FileDialogHelper::browseOpenFile("Select Server Executable",
            m_settExe, sizeof(m_settExe),
            {"All Files", "*"});
    }
    ImGui::InputText("Launch Args",    m_settArgs,     sizeof(m_settArgs));

    ImGui::SeparatorText("RCON");
    ImGui::InputText("Host",           m_settRconHost, sizeof(m_settRconHost));
    ImGui::InputInt("Port",            &m_settRconPort);
    ImGui::InputText("Password",       m_settRconPass, sizeof(m_settRconPass),
                     ImGuiInputTextFlags_Password);

    ImGui::SeparatorText("Paths");
    ImGui::InputText("Backup Folder",  m_settBackupDir, sizeof(m_settBackupDir));
    ImGui::SameLine();
    if (ImGui::Button("Browse##settBackup")) {
        FileDialogHelper::browseFolder("Select Backup Folder",
            m_settBackupDir, sizeof(m_settBackupDir));
    }

    ImGui::SeparatorText("Players");
    ImGui::InputInt("Max Players (0 = unlimited)", &m_settMaxPlayers);

    ImGui::SeparatorText("Automation");
    ImGui::Checkbox("Auto Update",     &m_settAutoUpdate);
    ImGui::Checkbox("Auto Start",      &m_settAutoStart);
    ImGui::Checkbox("Favorite",        &m_settFavorite);
    ImGui::InputInt("Backup Interval (min)", &m_settBackupInterval);
    ImGui::InputInt("Restart Interval (hrs)", &m_settRestartInterval);
    ImGui::InputInt("Keep Backups",    &m_settKeepBackups);
    ImGui::InputInt("Restart Warning (min)", &m_settRestartWarnMin);
    ImGui::InputText("Warning Message", m_settRestartWarnMsg, sizeof(m_settRestartWarnMsg));
    ImGui::InputInt("Graceful Shutdown (sec)", &m_settGracefulShutdown);
    ImGui::InputInt("Startup Priority", &m_settStartupPriority);
    ImGui::Checkbox("Backup Before Restart", &m_settBackupBeforeRestart);

    ImGui::SeparatorText("Resource Alerts");
    ImGui::InputDouble("CPU Alert (%)", &m_settCpuAlert, 0.0, 0.0, "%.1f");
    ImGui::InputDouble("Memory Alert (MB)", &m_settMemAlert, 0.0, 0.0, "%.1f");

    ImGui::SeparatorText("Backup");
    ImGui::SliderInt("Compression Level", &m_settCompression, 0, 9);

    ImGui::SeparatorText("Maintenance");
    ImGui::InputInt("Start Hour (-1 = disabled)", &m_settMaintStart);
    ImGui::InputInt("End Hour (-1 = disabled)",   &m_settMaintEnd);
    ImGui::Checkbox("Console Logging", &m_settConsoleLogging);

    ImGui::SeparatorText("Webhooks");
    ImGui::InputText("Discord URL",    m_settWebhookUrl, sizeof(m_settWebhookUrl));
    ImGui::InputText("Message Template", m_settWebhookTpl, sizeof(m_settWebhookTpl));

    ImGui::SeparatorText("Organization");
    ImGui::InputText("Group",          m_settGroup,     sizeof(m_settGroup));
    ImGui::InputInt("RCON Cmd Interval (min)", &m_settRconCmdInterval);
    ImGui::InputInt("Update Check Interval (min)", &m_settAutoUpdateCheck);

    ImGui::Spacing();
    if (ImGui::Button("Save Settings")) {
        m_server.name                        = m_settName;
        m_server.appid                       = m_settAppid;
        m_server.dir                         = m_settDir;
        m_server.executable                  = m_settExe;
        m_server.launchArgs                  = m_settArgs;
        m_server.rcon.host                   = m_settRconHost;
        m_server.rcon.port                   = m_settRconPort;
        m_server.rcon.password               = m_settRconPass;
        m_server.backupFolder                = m_settBackupDir;
        m_server.maxPlayers                  = m_settMaxPlayers;
        m_server.autoUpdate                  = m_settAutoUpdate;
        m_server.autoStartOnLaunch           = m_settAutoStart;
        m_server.favorite                    = m_settFavorite;
        m_server.backupIntervalMinutes       = m_settBackupInterval;
        m_server.restartIntervalHours        = m_settRestartInterval;
        m_server.keepBackups                 = m_settKeepBackups;
        m_server.restartWarningMinutes       = m_settRestartWarnMin;
        m_server.restartWarningMessage       = m_settRestartWarnMsg;
        m_server.gracefulShutdownSeconds     = m_settGracefulShutdown;
        m_server.startupPriority             = m_settStartupPriority;
        m_server.backupBeforeRestart         = m_settBackupBeforeRestart;
        m_server.cpuAlertThreshold           = m_settCpuAlert;
        m_server.memAlertThresholdMB         = m_settMemAlert;
        m_server.backupCompressionLevel      = m_settCompression;
        m_server.maintenanceStartHour        = m_settMaintStart;
        m_server.maintenanceEndHour          = m_settMaintEnd;
        m_server.consoleLogging              = m_settConsoleLogging;
        m_server.discordWebhookUrl           = m_settWebhookUrl;
        m_server.webhookTemplate             = m_settWebhookTpl;
        m_server.group                       = m_settGroup;
        m_server.rconCommandIntervalMinutes  = m_settRconCmdInterval;
        m_server.autoUpdateCheckIntervalMinutes = m_settAutoUpdateCheck;

        auto errors = m_server.validate();
        if (errors.empty()) {
            m_manager->saveConfig();
            m_consoleOutput += "[SSA] Settings saved.\n";
        } else {
            for (const auto &e : errors)
                m_consoleOutput += "[ERROR] " + e + "\n";
        }
    }
}

// ---------------------------------------------------------------------------
// Config editor
// ---------------------------------------------------------------------------

void ServerTabWidget::renderConfigTab()
{
    if (!m_configLoaded) {
        m_configPath = m_server.dir + "/GameUserSettings.ini";
        m_originalConfig = readFileToString(m_configPath);
        copyStr(m_configBuf, sizeof(m_configBuf), m_originalConfig);
        m_configLoaded = true;
    }

    ImGui::Text("File: %s", m_configPath.c_str());
    ImGui::InputTextMultiline("##ConfigEdit", m_configBuf, sizeof(m_configBuf),
                              ImVec2(-1, -60), ImGuiInputTextFlags_AllowTabInput);

    if (ImGui::Button("Save")) {
        // Show diff preview if there are changes
        std::string current = m_configBuf;
        if (current != m_originalConfig) {
            ImGui::OpenPopup("Config Diff Preview");
        } else {
            m_consoleOutput += "[SSA] No changes to save.\n";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert")) {
        copyStr(m_configBuf, sizeof(m_configBuf), m_originalConfig);
        m_consoleOutput += "[SSA] Config reverted.\n";
    }

    // Diff preview modal
    if (ImGui::BeginPopupModal("Config Diff Preview", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Changes to %s:", m_configPath.c_str());
        ImGui::Separator();

        // Build a simple line-by-line diff
        std::string current = m_configBuf;
        auto oldLines = splitLines(m_originalConfig);
        auto newLines = splitLines(current);

        ImGui::BeginChild("##DiffView", ImVec2(600, 300), ImGuiChildFlags_Borders);
        size_t maxLines = std::max(oldLines.size(), newLines.size());
        static const std::string kEmpty;
        for (size_t i = 0; i < maxLines; ++i) {
            const std::string &oldL = (i < oldLines.size()) ? oldLines[i] : kEmpty;
            const std::string &newL = (i < newLines.size()) ? newLines[i] : kEmpty;

            if (oldL != newL) {
                if (i < oldLines.size() && !oldL.empty())
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "- %s", oldL.c_str());
                if (i < newLines.size() && !newL.empty())
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "+ %s", newL.c_str());
            } else {
                ImGui::Text("  %s", newL.c_str());
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();
        if (ImGui::Button("Confirm Save", ImVec2(140, 0))) {
            std::ofstream f(m_configPath);
            if (f.is_open()) {
                f << m_configBuf;
                m_originalConfig = m_configBuf;
                m_consoleOutput += "[SSA] Config saved.\n";
            } else {
                m_consoleOutput += "[ERROR] Could not write config file.\n";
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(140, 0)))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------
// Mods
// ---------------------------------------------------------------------------

void ServerTabWidget::renderModsTab()
{
    ImGui::SeparatorText("Mods");
    ImGui::InputText("Mod ID", m_newModId, sizeof(m_newModId));
    ImGui::SameLine();
    if (ImGui::Button("Add")) {
        int modId = std::atoi(m_newModId);
        if (modId > 0) {
            if (std::find(m_server.mods.begin(), m_server.mods.end(), modId)
                    == m_server.mods.end()) {
                m_server.mods.push_back(modId);
                m_manager->saveConfig();
            }
            m_newModId[0] = '\0';
        }
    }

    if (ImGui::Button("Update All Mods")) {
        m_manager->updateMods(m_server);
    }

    ImGui::Spacing();
    if (ImGui::BeginTable("##Mods", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Mod ID", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        int removeIdx = -1;
        int toggleIdx = -1;
        for (int i = 0; i < static_cast<int>(m_server.mods.size()); ++i) {
            int mid = m_server.mods[i];
            bool disabled = std::find(m_server.disabledMods.begin(),
                                      m_server.disabledMods.end(), mid)
                            != m_server.disabledMods.end();

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%d", mid);

            ImGui::TableNextColumn();
            ImGui::Text("%s", disabled ? "Disabled" : "Enabled");

            ImGui::TableNextColumn();
            ImGui::PushID(i);
            if (ImGui::SmallButton("Remove"))
                removeIdx = i;
            ImGui::SameLine();
            if (ImGui::SmallButton(disabled ? "Enable" : "Disable"))
                toggleIdx = i;
            ImGui::PopID();
        }

        if (removeIdx >= 0) {
            m_server.mods.erase(m_server.mods.begin() + removeIdx);
            m_manager->saveConfig();
        }
        if (toggleIdx >= 0) {
            int mid = m_server.mods[toggleIdx];
            auto it = std::find(m_server.disabledMods.begin(),
                                m_server.disabledMods.end(), mid);
            if (it != m_server.disabledMods.end())
                m_server.disabledMods.erase(it);
            else
                m_server.disabledMods.push_back(mid);
            m_manager->saveConfig();
        }

        ImGui::EndTable();
    }
}

// ---------------------------------------------------------------------------
// Backups
// ---------------------------------------------------------------------------

void ServerTabWidget::renderBackupsTab()
{
    ImGui::SeparatorText("Backups");
    if (ImGui::Button("Take Snapshot")) {
        std::string ts = m_manager->takeSnapshot(m_server);
        if (!ts.empty())
            m_consoleOutput += "[SSA] Snapshot created: " + ts + "\n";
        else
            m_consoleOutput += "[ERROR] Snapshot failed.\n";
    }

    auto snapshots = m_manager->listSnapshots(m_server);
    static int selectedSnap = -1;
    if (ImGui::BeginListBox("##Snapshots", ImVec2(-1, 200))) {
        for (int i = 0; i < static_cast<int>(snapshots.size()); ++i) {
            bool sel = (selectedSnap == i);
            if (ImGui::Selectable(snapshots[i].c_str(), sel))
                selectedSnap = i;
        }
        ImGui::EndListBox();
    }

    if (ImGui::Button("Restore Selected") && selectedSnap >= 0 &&
        selectedSnap < static_cast<int>(snapshots.size())) {
        bool ok = m_manager->restoreSnapshot(snapshots[selectedSnap], m_server);
        m_consoleOutput += ok ? "[SSA] Restore complete.\n" : "[ERROR] Restore failed.\n";
    }
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

static int consoleHistoryCallback(ImGuiInputTextCallbackData *data)
{
    ServerTabWidget *self = static_cast<ServerTabWidget *>(data->UserData);
    if (self->m_commandHistory.empty())
        return 0;

    if (data->EventKey == ImGuiKey_UpArrow) {
        if (self->m_historyIndex > 0)
            --self->m_historyIndex;
    } else if (data->EventKey == ImGuiKey_DownArrow) {
        if (self->m_historyIndex < static_cast<int>(self->m_commandHistory.size()))
            ++self->m_historyIndex;
    }

    const char *replacement = "";
    if (self->m_historyIndex >= 0 &&
        self->m_historyIndex < static_cast<int>(self->m_commandHistory.size()))
        replacement = self->m_commandHistory[self->m_historyIndex].c_str();

    data->DeleteChars(0, data->BufTextLen);
    data->InsertChars(0, replacement);
    return 0;
}

void ServerTabWidget::renderConsoleTab()
{
    ImGui::SeparatorText("RCON Console");

    // Output area
    ImGui::BeginChild("##ConsoleOutput", ImVec2(-1, -40), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted(m_consoleOutput.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    // Input line with Up/Down arrow history navigation
    bool send = ImGui::InputText("##CmdInput", m_consoleCmdBuf, sizeof(m_consoleCmdBuf),
                                 ImGuiInputTextFlags_EnterReturnsTrue |
                                 ImGuiInputTextFlags_CallbackHistory,
                                 consoleHistoryCallback, this);
    ImGui::SameLine();
    send |= ImGui::Button("Send");

    if (send && m_consoleCmdBuf[0] != '\0') {
        std::string cmd = m_consoleCmdBuf;
        m_consoleOutput += "> " + cmd + "\n";
        m_commandHistory.push_back(cmd);
        m_historyIndex = static_cast<int>(m_commandHistory.size());

        std::string response = m_manager->sendRconCommand(m_server, cmd);
        m_consoleOutput += response + "\n";

        if (m_server.consoleLogging)
            ConsoleLogWriter::append(m_server.dir, m_server.name, "> " + cmd + "\n" + response);

        m_consoleCmdBuf[0] = '\0';
    }
}

// ---------------------------------------------------------------------------
// Logs
// ---------------------------------------------------------------------------

void ServerTabWidget::renderLogsTab()
{
    auto now = std::chrono::steady_clock::now();
    if (now - m_lastLogRefresh > std::chrono::seconds(2)) {
        std::string logPath = m_server.dir + "/server.log";
        m_logContent = readFileToString(logPath);
        m_lastLogRefresh = now;
    }

    ImGui::SeparatorText("Server Log");
    ImGui::BeginChild("##LogView", ImVec2(-1, -1), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted(m_logContent.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

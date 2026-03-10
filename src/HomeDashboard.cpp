#include "HomeDashboard.hpp"
#include "imgui.h"

#include <cstdio>
#include <algorithm>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string formatUptime(int64_t secs) {
    if (secs < 0) return "–";
    int days  = static_cast<int>(secs / 86400);
    int hours = static_cast<int>((secs % 86400) / 3600);
    int mins  = static_cast<int>((secs % 3600) / 60);
    char buf[64];
    if (days > 0)
        std::snprintf(buf, sizeof(buf), "%dd %dh %dm", days, hours, mins);
    else if (hours > 0)
        std::snprintf(buf, sizeof(buf), "%dh %dm", hours, mins);
    else
        std::snprintf(buf, sizeof(buf), "%dm", mins);
    return buf;
}

static std::string formatTotalUptime(int64_t totalSecs) {
    if (totalSecs <= 0) return "0m";
    int d = static_cast<int>(totalSecs / 86400);
    int h = static_cast<int>((totalSecs % 86400) / 3600);
    int m = static_cast<int>((totalSecs % 3600) / 60);
    char buf[64];
    if (d > 0)
        std::snprintf(buf, sizeof(buf), "%dd %dh", d, h);
    else if (h > 0)
        std::snprintf(buf, sizeof(buf), "%dh %dm", h, m);
    else
        std::snprintf(buf, sizeof(buf), "%dm", m);
    return buf;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

HomeDashboard::HomeDashboard(ServerManager *manager)
    : m_manager(manager)
{
}

// ---------------------------------------------------------------------------
// refresh() – no-op in immediate mode
// ---------------------------------------------------------------------------

void HomeDashboard::refresh()
{
    // Nothing to do – ImGui rebuilds the UI every frame.
}

// ---------------------------------------------------------------------------
// render() – called once per frame
// ---------------------------------------------------------------------------

void HomeDashboard::render()
{
    // --- Process pending delayed restarts ---
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

    // --- Title ---
    ImGui::SetWindowFontScale(1.5f);
    ImGui::Text("Server Health Dashboard");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();

    // --- Cluster summary ---
    auto &servers = m_manager->servers();
    int total = static_cast<int>(servers.size());
    int onlineCount = 0;
    for (const auto &s : servers) {
        if (m_manager->isServerRunning(s))
            ++onlineCount;
    }
    int offlineCount = total - onlineCount;

    char summaryBuf[128];
    std::snprintf(summaryBuf, sizeof(summaryBuf), "Total: %d  |  ", total);
    ImGui::Text("%s", summaryBuf);
    ImGui::SameLine(0.0f, 0.0f);

    ImGui::TextColored(ImVec4(0.0f, 0.8f, 0.0f, 1.0f), "Online: %d", onlineCount);
    ImGui::SameLine();
    ImGui::Text("  |  ");
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "Offline: %d", offlineCount);

    ImGui::Spacing();

    // --- Server card grid (3 per row) ---
    static constexpr int kCardsPerRow = 3;
    float availWidth = ImGui::GetContentRegionAvail().x;
    float cardWidth  = (availWidth - ImGui::GetStyle().ItemSpacing.x * (kCardsPerRow - 1)) / kCardsPerRow;
    if (cardWidth < 200.0f) cardWidth = 200.0f;

    int col = 0;
    for (int i = 0; i < static_cast<int>(servers.size()); ++i) {
        if (col > 0)
            ImGui::SameLine();

        ImGui::PushID(i);
        renderCard(servers[static_cast<size_t>(i)], i);
        ImGui::PopID();

        ++col;
        if (col >= kCardsPerRow)
            col = 0;
    }
}

// ---------------------------------------------------------------------------
// renderCard()
// ---------------------------------------------------------------------------

void HomeDashboard::renderCard(ServerConfig &server, int index)
{
    float availWidth = ImGui::GetContentRegionAvail().x;
    static constexpr int kCardsPerRow = 3;
    float cardWidth = (availWidth - ImGui::GetStyle().ItemSpacing.x * (kCardsPerRow - 1)) / kCardsPerRow;
    if (cardWidth < 200.0f) cardWidth = 200.0f;

    // When this is not the first card in a row, the available width has already
    // been reduced.  Use a fixed fraction of the full window width instead.
    float windowWidth = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;
    cardWidth = (windowWidth - ImGui::GetStyle().ItemSpacing.x * (kCardsPerRow - 1)) / kCardsPerRow;
    if (cardWidth < 200.0f) cardWidth = 200.0f;

    ImGui::BeginChild(("card_" + std::to_string(index)).c_str(),
                       ImVec2(cardWidth, 0),
                       ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    bool online  = m_manager->isServerRunning(server);
    int  players = online ? m_manager->getPlayerCount(server) : -1;

    // --- Status light + server name ---
    const char *light;
    if (!online)
        light = "🔴";
    else if (players > 0)
        light = "🟢";
    else
        light = "🟡";

    ImGui::Text("%s", light);
    ImGui::SameLine();
    ImGui::TextWrapped("%s", server.name.c_str());

    // Group badge
    std::string grp = trimString(server.group);
    if (!grp.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("[%s]", grp.c_str());
    }

    // --- Player count ---
    if (players >= 0) {
        if (server.maxPlayers > 0)
            ImGui::Text("Players: %d / %d", players, server.maxPlayers);
        else
            ImGui::Text("Players: %d", players);
    } else {
        ImGui::TextDisabled("Players: –");
    }

    // --- Uptime ---
    int64_t secs = m_manager->serverUptimeSeconds(server.name);
    ImGui::Text("Uptime: %s", formatUptime(secs).c_str());

    // --- Pending update badges ---
    if (m_manager->hasPendingUpdate(server.name))
        ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.13f, 1.0f), "⬆ Update Available");
    if (m_manager->hasPendingModUpdate(server.name))
        ImGui::TextColored(ImVec4(0.2f, 0.6f, 0.86f, 1.0f), "🔧 Mod Update Available");

    // --- Statistics ---
    ImGui::TextDisabled("Total: %s  |  Crashes: %d",
                        formatTotalUptime(server.totalUptimeSeconds).c_str(),
                        server.totalCrashes);

    // --- Quick-action buttons ---
    ImGui::Spacing();
    if (ImGui::Button("▶ Start"))
        m_manager->startServer(server);
    ImGui::SameLine();
    if (ImGui::Button("■ Stop"))
        m_manager->stopServer(server);
    ImGui::SameLine();
    if (ImGui::Button("↺ Restart"))
        m_manager->restartServer(server);
    ImGui::SameLine();
    if (ImGui::Button("📦 Backup"))
        m_manager->takeSnapshot(server);

    // --- Right-click context menu ---
    renderContextMenu(server);

    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// renderContextMenu()
// ---------------------------------------------------------------------------

void HomeDashboard::renderContextMenu(ServerConfig &server)
{
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("💾  Save Config")) {
            m_manager->saveConfig();
        }

        // Build restart label
        char restartLabel[128];
        if (server.restartWarningMinutes > 0 && m_manager->isServerRunning(server))
            std::snprintf(restartLabel, sizeof(restartLabel),
                          "↺  Restart (warn %d min)", server.restartWarningMinutes);
        else
            std::snprintf(restartLabel, sizeof(restartLabel), "↺  Restart Server");

        if (ImGui::MenuItem(restartLabel)) {
            if (server.restartWarningMinutes > 0 && m_manager->isServerRunning(server)) {
                m_manager->sendRestartWarning(server, server.restartWarningMinutes);
                PendingWarningRestart pwr;
                pwr.serverName = server.name;
                pwr.restartAt  = std::chrono::steady_clock::now()
                    + std::chrono::minutes(server.restartWarningMinutes);
                m_pendingRestarts.push_back(pwr);
            } else {
                m_manager->restartServer(server);
            }
        }

        ImGui::EndPopup();
    }
}

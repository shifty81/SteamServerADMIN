#include "HomeDashboard.hpp"
#include "GracefulRestartManager.hpp"
#include "imgui.h"
#include "imgui_internal.h"

#include <cstdio>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <mutex>
#include <thread>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string formatUptime(int64_t secs) {
    if (secs < 0) return "\xe2\x80\x93";
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

static std::string formatMemoryMB(int64_t bytes) {
    double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    char buf[32];
    if (mb >= 1024.0)
        std::snprintf(buf, sizeof(buf), "%.1f GB", mb / 1024.0);
    else
        std::snprintf(buf, sizeof(buf), "%.0f MB", mb);
    return buf;
}

// ---------------------------------------------------------------------------
// Glass-card drawing helpers
// ---------------------------------------------------------------------------

static constexpr float kCardRounding = 12.0f;

/// Draw a multi-layer frosted-glass panel with depth.
static void drawGlassPanel(ImDrawList *dl, ImVec2 pMin, ImVec2 pMax,
                            bool online, bool hovered)
{
    // ---- Outer drop shadow (2 layers for soft falloff) ----
    for (int i = 2; i >= 1; --i) {
        float off = static_cast<float>(i) * 2.5f;
        ImU32 sc = IM_COL32(0, 0, 0, 22 * i);
        dl->AddRectFilled(
            ImVec2(pMin.x + off, pMin.y + off),
            ImVec2(pMax.x + off, pMax.y + off),
            sc, kCardRounding + 2.0f);
    }

    // ---- Base fill (frosted dark tinted glass) ----
    ImU32 baseFill;
    if (online)
        baseFill = hovered ? IM_COL32(28, 38, 58, 210) : IM_COL32(22, 30, 48, 200);
    else
        baseFill = hovered ? IM_COL32(38, 38, 46, 210) : IM_COL32(30, 30, 38, 200);
    dl->AddRectFilled(pMin, pMax, baseFill, kCardRounding);

    // ---- Upper-half specular highlight (glass shine) ----
    {
        float h = (pMax.y - pMin.y) * 0.45f;
        ImVec2 hlMax(pMax.x, pMin.y + h);
        ImU32 hlA = IM_COL32(255, 255, 255, hovered ? 22 : 14);
        ImU32 hlB = IM_COL32(255, 255, 255, 0);
        // We can't do rounded gradient natively, so draw full gradient then
        // re-fill corners with base to simulate rounding.
        dl->AddRectFilledMultiColor(pMin, hlMax, hlA, hlA, hlB, hlB);
        // Mask the two bottom corners of the highlight (they're within the card)
        // – not needed since highlight is in the upper half.
    }

    // ---- Thin inner-glow border (glass edge catch-light) ----
    {
        ImU32 edgeOuter = online
            ? (hovered ? IM_COL32(100, 160, 240, 140) : IM_COL32(70, 120, 200, 100))
            : (hovered ? IM_COL32(140, 140, 160, 120) : IM_COL32(90, 90, 110, 80));
        dl->AddRect(pMin, pMax, edgeOuter, kCardRounding, 0, 1.2f);
    }

    // ---- Top-edge specular line (like light reflecting off glass rim) ----
    {
        float inset = kCardRounding * 0.8f;
        ImVec2 a(pMin.x + inset, pMin.y + 1.0f);
        ImVec2 b(pMax.x - inset, pMin.y + 1.0f);
        ImU32 lineCol = IM_COL32(255, 255, 255, hovered ? 50 : 30);
        dl->AddLine(a, b, lineCol, 1.0f);
    }

    // ---- Subtle bottom-edge shadow line (grounds the card) ----
    {
        float inset = kCardRounding * 0.8f;
        ImVec2 a(pMin.x + inset, pMax.y - 1.0f);
        ImVec2 b(pMax.x - inset, pMax.y - 1.0f);
        dl->AddLine(a, b, IM_COL32(0, 0, 0, 40), 1.0f);
    }
}

/// Coloured status accent stripe along the top of the card.
static void drawStatusStripe(ImDrawList *dl, ImVec2 pMin, ImVec2 pMax,
                              bool online, int players)
{
    const float stripeH = 3.0f;
    ImVec2 sMax(pMax.x, pMin.y + stripeH);

    ImU32 col;
    if (!online)
        col = IM_COL32(220, 55, 55, 200);
    else if (players > 0)
        col = IM_COL32(50, 210, 90, 220);
    else
        col = IM_COL32(230, 190, 40, 200);

    dl->AddRectFilled(pMin, sMax, col, kCardRounding, ImDrawFlags_RoundCornersTop);
}

/// Draw a single glass-style button manually.  Returns true if clicked.
static bool glassButton(const char *label, ImVec2 size = ImVec2(0, 0))
{
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.22f, 0.32f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.28f, 0.38f, 0.58f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.22f, 0.44f, 0.76f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border,         ImVec4(0.45f, 0.55f, 0.75f, 0.30f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    return clicked;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

HomeDashboard::HomeDashboard(ServerManager *manager)
    : m_manager(manager)
{
}

HomeDashboard::~HomeDashboard()
{
    // Join any in-progress deploy threads before destruction
    for (auto &[name, state] : m_deployStates) {
        if (state.thread.joinable())
            state.thread.join();
    }
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
    ImGui::Spacing();
    ImGui::SetWindowFontScale(1.4f);
    ImGui::TextColored(ImVec4(0.70f, 0.82f, 1.0f, 1.0f), "Server Health Dashboard");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Cluster summary ---
    auto &servers = m_manager->servers();
    int total = static_cast<int>(servers.size());
    int onlineCount = 0;
    int pendingUpdateCount = 0;
    for (const auto &s : servers) {
        if (m_manager->isServerRunning(s))
            ++onlineCount;
        if (m_manager->hasPendingUpdate(s.name) || m_manager->hasPendingModUpdate(s.name))
            ++pendingUpdateCount;
    }
    int offlineCount = total - onlineCount;

    // Total players across all running servers (sum cached counts)
    int totalPlayers = 0;
    for (const auto &[name, cache] : m_playerCounts)
        if (cache.count > 0) totalPlayers += cache.count;

    ImGui::TextColored(ImVec4(0.65f, 0.70f, 0.80f, 1.0f), "Total: %d   ", total);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextColored(ImVec4(0.30f, 0.85f, 0.45f, 1.0f), "Online: %d", onlineCount);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.65f, 0.70f, 0.80f, 1.0f), "  |  ");
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.30f, 1.0f), "Offline: %d", offlineCount);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.65f, 0.70f, 0.80f, 1.0f), "  |  ");
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextColored(ImVec4(0.80f, 0.92f, 1.0f, 1.0f), "Players: %d", totalPlayers);
    if (pendingUpdateCount > 0) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.65f, 0.70f, 0.80f, 1.0f), "  |  ");
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.68f, 0.22f, 1.0f),
                           "\xe2\xac\x86 %d update(s) pending", pendingUpdateCount);
    }

    // --- Group filter dropdown ---
    {
        ImGui::SameLine();
        ImGui::Text("   ");
        ImGui::SameLine();

        std::vector<std::string> groups = m_manager->serverGroups();
        const char *preview = m_groupFilter.empty() ? "All Groups" : m_groupFilter.c_str();
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::BeginCombo("##GroupFilter", preview)) {
            if (ImGui::Selectable("All Groups", m_groupFilter.empty()))
                m_groupFilter.clear();
            for (const auto &g : groups) {
                bool selected = (m_groupFilter == g);
                if (ImGui::Selectable(g.c_str(), selected))
                    m_groupFilter = g;
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleVar();
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // --- Server card grid (responsive: 2 cards on narrow windows, up to 4 on wide) ---
    float windowWidth = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;
    float gap = 10.0f;
    // Choose column count based on available width
    int cardsPerRow = 3;
    if (windowWidth < 560.0f)       cardsPerRow = 1;
    else if (windowWidth < 840.0f)  cardsPerRow = 2;
    else if (windowWidth >= 1120.0f) cardsPerRow = 4;
    float cardWidth = (windowWidth - gap * (cardsPerRow - 1)) / static_cast<float>(cardsPerRow);
    if (cardWidth < 220.0f) cardWidth = 220.0f;

    int col = 0;
    for (int i = 0; i < static_cast<int>(servers.size()); ++i) {
        if (!m_groupFilter.empty() && trimString(servers[static_cast<size_t>(i)].group) != m_groupFilter)
            continue;

        if (col > 0) {
            ImGui::SameLine(0.0f, gap);
        }

        ImGui::PushID(i);
        renderCard(servers[static_cast<size_t>(i)], i, cardWidth);
        ImGui::PopID();

        ++col;
        if (col >= cardsPerRow)
            col = 0;
    }
}

// ---------------------------------------------------------------------------
// renderCard()
// ---------------------------------------------------------------------------

void HomeDashboard::renderCard(ServerConfig &server, int index, float cardWidth)
{
    bool online = m_manager->isServerRunning(server);

    // Throttle player count queries
    int players = -1;
    if (online) {
        auto &cache = m_playerCounts[server.name];
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - cache.lastRefresh).count();
        if (elapsed >= kPlayerCountRefreshSeconds || cache.count < 0) {
            cache.count = m_manager->getPlayerCount(server);
            cache.lastRefresh = now;
        }
        players = cache.count;
    } else {
        m_playerCounts.erase(server.name);
    }

    // --- Begin invisible child so we can paint custom glass behind it ---
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border,  ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, kCardRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));

    ImGui::BeginChild(("card_" + std::to_string(index)).c_str(),
                       ImVec2(cardWidth, 0),
                       ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    ImVec2 cardMin = ImGui::GetWindowPos();

    // Split the draw list into 2 channels immediately:
    //   channel 0 = glass background (drawn first → behind)
    //   channel 1 = card content (drawn second → in front)
    // All content below is emitted to channel 1 so it appears above the panel.
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);

    // ====== Card content ======

    // Status indicator + name
    const char *light = !online ? "\xF0\x9F\x94\xB4"
                        : (players > 0 ? "\xF0\x9F\x9F\xA2" : "\xF0\x9F\x9F\xA1");
    ImGui::Text("%s", light);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::SetWindowFontScale(1.1f);
    ImGui::TextWrapped("%s", server.name.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();

    // Group badge
    std::string grp = trimString(server.group);
    if (!grp.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.55f, 0.65f, 0.90f, 0.85f), "[%s]", grp.c_str());
    }

    ImGui::Spacing();
    // Thin separator line inside the card
    {
        ImDrawList *fgDl = ImGui::GetWindowDrawList();
        ImVec2 cPos = ImGui::GetCursorScreenPos();
        fgDl->AddLine(ImVec2(cPos.x, cPos.y),
                       ImVec2(cPos.x + cardWidth - 28.0f, cPos.y),
                       IM_COL32(255, 255, 255, 18), 1.0f);
        ImGui::Dummy(ImVec2(0, 4));
    }

    // Player count
    if (players >= 0) {
        if (server.maxPlayers > 0) {
            ImGui::TextColored(ImVec4(0.80f, 0.92f, 1.0f, 1.0f),
                               "Players: %d / %d", players, server.maxPlayers);
            // Thin fill bar showing occupancy
            float frac = static_cast<float>(players) / static_cast<float>(server.maxPlayers);
            frac = std::min(frac, 1.0f);
            ImVec4 barColor = (frac >= 0.9f) ? ImVec4(1.0f, 0.50f, 0.20f, 1.0f)
                                              : ImVec4(0.30f, 0.72f, 0.95f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.10f, 0.15f, 0.7f));
            ImGui::ProgressBar(frac, ImVec2(-1.0f, 4.0f), "");
            ImGui::PopStyleColor(2);
        } else {
            ImGui::TextColored(ImVec4(0.80f, 0.92f, 1.0f, 1.0f),
                               "Players: %d", players);
        }
    } else {
        ImGui::TextColored(ImVec4(0.50f, 0.50f, 0.56f, 1.0f), "Players: \xe2\x80\x93");
    }

    // Uptime
    int64_t secs = m_manager->serverUptimeSeconds(server.name);
    ImGui::TextColored(ImVec4(0.68f, 0.74f, 0.86f, 1.0f),
                       "Uptime: %s", formatUptime(secs).c_str());

    // Resource usage – text + thin progress bars
    if (online) {
        ResourceUsage ru = m_manager->resourceMonitor()->usage(server.name);
        if (ru.cpuPercent > 0.0 || ru.memoryBytes > 0) {
            bool cpuAlert = (server.cpuAlertThreshold > 0.0
                             && ru.cpuPercent > server.cpuAlertThreshold);
            ImVec4 cpuColor = cpuAlert
                ? ImVec4(1.0f, 0.40f, 0.35f, 1.0f)
                : ImVec4(0.45f, 0.82f, 0.50f, 1.0f);
            double memMB = static_cast<double>(ru.memoryBytes) / (1024.0 * 1024.0);
            bool memAlert = (server.memAlertThresholdMB > 0.0
                             && memMB > server.memAlertThresholdMB);
            ImVec4 memColor = memAlert
                ? ImVec4(1.0f, 0.40f, 0.35f, 1.0f)
                : ImVec4(0.45f, 0.82f, 0.50f, 1.0f);

            // CPU label + thin bar
            ImGui::TextColored(cpuColor, "CPU: %.1f%%", ru.cpuPercent);
            {
                float frac = static_cast<float>(
                    std::min(ru.cpuPercent / 100.0, 1.0));
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, cpuColor);
                ImGui::PushStyleColor(ImGuiCol_FrameBg,
                                      ImVec4(0.10f, 0.10f, 0.15f, 0.7f));
                ImGui::ProgressBar(frac, ImVec2(-1.0f, 4.0f), "");
                ImGui::PopStyleColor(2);
            }

            // Memory label + thin bar
            ImGui::TextColored(memColor, "Mem: %s",
                               formatMemoryMB(ru.memoryBytes).c_str());
            if (server.memAlertThresholdMB > 0.0) {
                float frac = static_cast<float>(
                    std::min(memMB / server.memAlertThresholdMB, 1.0));
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, memColor);
                ImGui::PushStyleColor(ImGuiCol_FrameBg,
                                      ImVec4(0.10f, 0.10f, 0.15f, 0.7f));
                ImGui::ProgressBar(frac, ImVec2(-1.0f, 4.0f), "");
                ImGui::PopStyleColor(2);
            }
        }
    }

    // Pending-update badges
    if (m_manager->hasPendingUpdate(server.name))
        ImGui::TextColored(ImVec4(1.0f, 0.68f, 0.22f, 1.0f), "\xe2\xac\x86 Update Available");
    if (m_manager->hasPendingModUpdate(server.name))
        ImGui::TextColored(ImVec4(0.38f, 0.72f, 1.0f, 1.0f), "\xF0\x9F\x94\xA7 Mod Update");

    // Graceful restart countdown badge
    {
        auto *grm = m_manager->gracefulRestartManager();
        if (grm && grm->isRestarting(server.name)) {
            int minsLeft = grm->minutesRemaining(server.name);
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.20f, 1.0f),
                               "\xF0\x9F\x94\x84 Restarting in %dm", minsLeft);
        }
    }

    // Statistics
    ImGui::TextColored(ImVec4(0.46f, 0.48f, 0.56f, 1.0f), "Total: %s  |  Crashes: %d",
                       formatTotalUptime(server.totalUptimeSeconds).c_str(),
                       server.totalCrashes);

    ImGui::Spacing();

    // ---- Glass-style action buttons ----
    if (glassButton("\xe2\x96\xb6 Start"))
        m_manager->startServer(server);
    ImGui::SameLine();
    if (glassButton("\xe2\x96\xa0 Stop"))
        m_manager->stopServer(server);
    ImGui::SameLine();
    if (glassButton("\xe2\x86\xba Restart"))
        m_manager->restartServer(server);
    ImGui::SameLine();
    if (glassButton("\xF0\x9F\x93\xA6 Backup"))
        m_manager->takeSnapshot(server);

    // Deploy / Update
    {
        bool dirEmpty = true;
        try {
            namespace fs = std::filesystem;
            if (fs::exists(server.dir) && fs::is_directory(server.dir)) {
                auto it = fs::directory_iterator(server.dir);
                dirEmpty = (it == fs::directory_iterator());
            }
        } catch (...) { dirEmpty = true; }

        // Check if a deploy is currently running for this server
        bool deployRunning = false;
        {
            std::lock_guard<std::mutex> lk(m_deployStatesMutex);
            auto it = m_deployStates.find(server.name);
            if (it != m_deployStates.end())
                deployRunning = it->second.running->load();
        }

        if (deployRunning) {
            static const char *spinFrames[] = {"|", "/", "-", "\\"};
            int frame = static_cast<int>(ImGui::GetTime() * 8.0) % 4;
            ImGui::Text("%s Deploying...", spinFrames[frame]);
        } else {
            const char *label = dirEmpty ? "\xe2\xac\x87 Install" : "\xe2\xac\x86 Update";
            if (glassButton(label)) {
                bool canStart = false;
                std::shared_ptr<std::atomic<bool>> runFlag;
                {
                    std::lock_guard<std::mutex> lk(m_deployStatesMutex);
                    auto &state = m_deployStates[server.name];
                    if (!state.running->load()) {
                        if (state.thread.joinable())
                            state.thread.join();
                        state.running->store(true);
                        runFlag = state.running;
                        canStart = true;
                    }
                }
                // Spawn the thread outside the lock to avoid potential deadlock.
                // The runFlag shared_ptr keeps the atomic alive in the thread.
                if (canStart) {
                    std::string serverName = server.name;
                    std::lock_guard<std::mutex> lk(m_deployStatesMutex);
                    m_deployStates[serverName].thread = std::thread([this, &server, runFlag]() {
                        m_manager->deployOrUpdateServer(server);
                        runFlag->store(false);  // atomic – no mutex needed
                    });
                }
            }
        }
    }

    // Context menu
    renderContextMenu(server);

    // ====== Custom glass background (drawn behind content) ======
    // Now that content has been emitted to channel 1, switch to channel 0
    // (back layer) to draw the glass panel, then merge so the background
    // is composited behind the text and buttons.
    ImVec2 cardMax(cardMin.x + cardWidth, cardMin.y + ImGui::GetWindowHeight());
    bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    dl->ChannelsSetCurrent(0);
    drawGlassPanel(dl, cardMin, cardMax, online, hovered);
    drawStatusStripe(dl, cardMin, cardMax, online, players);
    dl->ChannelsMerge();

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

// ---------------------------------------------------------------------------
// renderContextMenu()
// ---------------------------------------------------------------------------

void HomeDashboard::renderContextMenu(ServerConfig &server)
{
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("\xF0\x9F\x92\xBE  Save Config"))
            m_manager->saveConfig();

        char restartLabel[128];
        if (server.restartWarningMinutes > 0 && m_manager->isServerRunning(server))
            std::snprintf(restartLabel, sizeof(restartLabel),
                          "\xe2\x86\xba  Restart (warn %d min)", server.restartWarningMinutes);
        else
            std::snprintf(restartLabel, sizeof(restartLabel), "\xe2\x86\xba  Restart Server");

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

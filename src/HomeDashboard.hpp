#pragma once

#include "ServerManager.hpp"
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/**
 * @brief Home dashboard showing all servers with rich badge cards and quick actions.
 *
 * Each server is displayed as a badge card showing:
 *   - Health status light (🟢/🟡/🔴)
 *   - Uptime
 *   - Pending game / mod update indicators
 *   - Player count / max players
 *   - CPU and memory usage (when running)
 *   - Server statistics (total uptime, crash count)
 *
 * A group filter dropdown allows narrowing the displayed servers to a
 * specific group.
 *
 * Right-click context menu on each card offers Save Config and Restart
 * (with configurable in-game warning countdown).
 *
 * In ImGui immediate mode the UI is rebuilt every frame, so status
 * refreshes automatically without a polling timer.
 */
class HomeDashboard {
public:
    explicit HomeDashboard(ServerManager *manager);
    ~HomeDashboard();

    /** Render the full dashboard.  Call once per frame. */
    void render();

    /** No-op in ImGui – the UI is rebuilt every frame. */
    void refresh();

    /**
     * @brief Callback invoked when the dashboard wants to navigate to a
     *        specific server tab (0-based index into the server list).
     *
     * Set by MainWindow so the Install/Update button can bring the relevant
     * Overview tab into view where deploy progress is displayed.
     */
    std::function<void(int serverIndex)> onNavigateToServer;

    /**
     * @brief Callback invoked when the dashboard wants to start a deploy for
     *        a specific server (0-based index into the server list).
     *
     * Routes through ServerTabWidget::startDeployAsync so the deploy log
     * observer is set up and the Overview tab's Deploy Progress panel is
     * populated.
     */
    std::function<void(int serverIndex)> onRequestDeploy;

private:
    void renderCard(ServerConfig &server, int index, float cardWidth);
    void renderContextMenu(ServerConfig &server);

    ServerManager *m_manager;

    // Dashboard group filter (empty string = show all)
    std::string m_groupFilter;

    // Per-server player count cache (throttled to avoid flooding RCON)
    struct CachedPlayerCount {
        int count = -1;
        std::chrono::steady_clock::time_point lastRefresh;
    };
    std::map<std::string, CachedPlayerCount> m_playerCounts;
    static constexpr int kPlayerCountRefreshSeconds = 30;

    // Delayed restart after in-game warning
    struct PendingWarningRestart {
        std::string serverName;
        std::chrono::steady_clock::time_point restartAt;
    };
    std::vector<PendingWarningRestart> m_pendingRestarts;
};

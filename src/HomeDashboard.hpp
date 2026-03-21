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

    // Per-server deploy in-progress tracking (background thread deploy from dashboard).
    // `running` is stored in a shared_ptr<atomic> so the background thread can safely
    // clear it without acquiring any lock.
    struct DeployState {
        std::thread                            thread;
        std::shared_ptr<std::atomic<bool>>     running;
        DeployState()
            : running(std::make_shared<std::atomic<bool>>(false)) {}
    };
    std::map<std::string, DeployState> m_deployStates;
    std::mutex                         m_deployStatesMutex;  // protects map structure only
};

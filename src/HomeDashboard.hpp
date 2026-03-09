#pragma once

#include "ServerManager.hpp"
#include <chrono>
#include <string>
#include <vector>

/**
 * @brief Home dashboard showing all servers with rich badge cards and quick actions.
 *
 * Each server is displayed as a badge card showing:
 *   - Health status light (🟢/🟡/🔴)
 *   - Uptime
 *   - Pending game / mod update indicators
 *   - Player count / max players
 *   - Server statistics (total uptime, crash count)
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

    /** Render the full dashboard.  Call once per frame. */
    void render();

    /** No-op in ImGui – the UI is rebuilt every frame. */
    void refresh();

private:
    void renderCard(ServerConfig &server, int index);
    void renderContextMenu(ServerConfig &server);

    ServerManager *m_manager;

    // Delayed restart after in-game warning
    struct PendingWarningRestart {
        std::string serverName;
        std::chrono::steady_clock::time_point restartAt;
    };
    std::vector<PendingWarningRestart> m_pendingRestarts;
};

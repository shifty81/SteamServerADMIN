#pragma once

#include "ServerConfig.hpp"

#include <string>
#include <map>
#include <functional>
#include <chrono>

class ServerManager;

/**
 * @brief Manages per-server scheduled tasks (automatic backups and restarts).
 *
 * Uses a tick-based design: the caller invokes tick() periodically from the
 * main loop. The module checks elapsed time since last execution and fires
 * scheduled events when their intervals have elapsed.
 *
 * Each server is driven by:
 *   - ServerConfig::backupIntervalMinutes  -> periodic snapshots
 *   - ServerConfig::restartIntervalHours   -> periodic restarts
 */
class SchedulerModule {
public:
    explicit SchedulerModule(ServerManager *manager);
    ~SchedulerModule();

    /** Create and start timers for every server currently in the manager. */
    void startAll();

    /** Stop and destroy all timers. */
    void stopAll();

    /** Start timers for a single server (e.g. after adding one at runtime). */
    void startScheduler(const std::string &serverName);

    /** Stop timers for a single server. */
    void stopScheduler(const std::string &serverName);

    /** Call this from the main loop to check and fire scheduled events. */
    void tick();

    /** Callbacks for scheduled events. */
    std::function<void(const std::string &serverName)> onScheduledBackup;
    std::function<void(const std::string &serverName)> onScheduledRestart;
    std::function<void(const std::string &serverName)> onScheduledRconCommand;
    std::function<void(const std::string &serverName)> onScheduledUpdateCheck;

private:
    struct TimerState {
        // Intervals in milliseconds (0 = disabled)
        int64_t backupIntervalMs  = 0;
        int64_t restartIntervalMs = 0;
        int64_t rconIntervalMs    = 0;
        int64_t updateCheckIntervalMs = 0;

        // Time points of last execution
        std::chrono::steady_clock::time_point lastBackup;
        std::chrono::steady_clock::time_point lastRestart;
        std::chrono::steady_clock::time_point lastRcon;
        std::chrono::steady_clock::time_point lastUpdateCheck;

        // Restart warning state
        int     restartWarningMinutes = 0;     // configured warning lead time
        int     restartWarningCountdown = 0;   // minutes remaining
        bool    warningActive = false;
        std::chrono::steady_clock::time_point lastWarningTick;
    };

    ServerManager *m_manager;
    std::map<std::string, TimerState> m_timers;
};

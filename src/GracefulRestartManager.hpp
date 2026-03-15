#pragma once

#include <string>
#include <map>
#include <chrono>
#include <functional>
#include <vector>

class ServerManager;
struct ServerConfig;

/**
 * @brief Manages graceful server restarts with countdown broadcasts,
 *        world saves, and backups before the actual restart.
 *
 * Countdown sequence:
 *   10 min -> 5 min -> 4 min -> 3 min -> 2 min -> 1 min -> save + backup -> restart
 *
 * The caller must invoke tick() periodically from the main loop.
 */
class GracefulRestartManager {
public:
    explicit GracefulRestartManager(ServerManager *manager);

    /** Phase of the graceful restart process. */
    enum class Phase {
        Idle,          ///< No restart in progress
        Countdown,     ///< Broadcasting countdown warnings
        Saving,        ///< Performing world save via RCON
        BackingUp,     ///< Taking a backup snapshot
        Restarting     ///< Stopping and starting the server
    };

    /**
     * @brief Initiate a graceful restart for the named server.
     * @param serverName The server to restart.
     * @param countdownMinutes Total countdown duration (default 10).
     * @param saveCommand RCON command to trigger a world save (e.g. "saveworld").
     *                    Empty string skips the save step.
     */
    void beginGracefulRestart(const std::string &serverName,
                              int countdownMinutes = 10,
                              const std::string &saveCommand = "saveworld");

    /**
     * @brief Cancel an in-progress graceful restart.
     */
    void cancelGracefulRestart(const std::string &serverName);

    /**
     * @brief Check if a graceful restart is in progress for the server.
     */
    bool isRestarting(const std::string &serverName) const;

    /**
     * @brief Get the current phase for a server.
     */
    Phase currentPhase(const std::string &serverName) const;

    /**
     * @brief Get the minutes remaining in the countdown.
     * @return Minutes remaining, or -1 if no restart is active.
     */
    int minutesRemaining(const std::string &serverName) const;

    /**
     * @brief Must be called from the main loop to advance countdowns and
     *        trigger saves/backups/restarts.
     */
    void tick();

    /**
     * @brief Get the standard countdown alert minutes (10, 5, 4, 3, 2, 1).
     */
    static std::vector<int> countdownAlertMinutes();

    // Callbacks
    std::function<void(const std::string &serverName, const std::string &message)> onLogMessage;
    std::function<void(const std::string &serverName, Phase phase)> onPhaseChanged;

private:
    struct RestartState {
        int totalCountdownMinutes = 10;
        std::string saveCommand;
        Phase phase = Phase::Idle;
        std::chrono::steady_clock::time_point startTime;
        std::chrono::steady_clock::time_point lastBroadcast;
        int lastBroadcastMinute = -1;     // last minute value that was broadcast
    };

    ServerManager *m_manager;
    std::map<std::string, RestartState> m_states;

    void emitLog(const std::string &serverName, const std::string &msg);
    void setPhase(const std::string &serverName, RestartState &state, Phase phase);
    void performSaveAndRestart(const std::string &serverName, RestartState &state);
};

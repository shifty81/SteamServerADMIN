#include "GracefulRestartManager.hpp"
#include "ServerManager.hpp"

#include <algorithm>
#include <iostream>

// ---------------------------------------------------------------------------
// Sanitize a message for safe RCON broadcast (strip control characters)
// ---------------------------------------------------------------------------
static std::string sanitizeRconMessage(const std::string &msg)
{
    std::string result;
    result.reserve(msg.size());
    for (char c : msg) {
        // Strip newlines, carriage returns, and other control characters
        // that could be interpreted as RCON command separators
        if (c == '\n' || c == '\r' || c == '\0')
            result += ' ';
        else
            result += c;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Standard countdown alert schedule
// ---------------------------------------------------------------------------

std::vector<int> GracefulRestartManager::countdownAlertMinutes()
{
    // Alerts at 10, 5, 4, 3, 2, 1 minutes
    return { 10, 5, 4, 3, 2, 1 };
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

GracefulRestartManager::GracefulRestartManager(ServerManager *manager)
    : m_manager(manager)
{
}

// ---------------------------------------------------------------------------
// Begin / Cancel / Query
// ---------------------------------------------------------------------------

void GracefulRestartManager::beginGracefulRestart(const std::string &serverName,
                                                   int countdownMinutes,
                                                   const std::string &saveCommand)
{
    if (countdownMinutes < 0) countdownMinutes = 0;

    RestartState state;
    state.totalCountdownMinutes = countdownMinutes;
    state.saveCommand = saveCommand;
    state.phase = Phase::Countdown;
    state.startTime = std::chrono::steady_clock::now();
    state.lastBroadcast = state.startTime;
    state.lastBroadcastMinute = -1;

    m_states[serverName] = state;

    emitLog(serverName, "Graceful restart initiated (" +
            std::to_string(countdownMinutes) + " minute countdown).");
    setPhase(serverName, m_states[serverName], Phase::Countdown);

    // Send the initial broadcast immediately
    if (countdownMinutes > 0) {
        // Find the applicable starting server
        for (ServerConfig &s : m_manager->servers()) {
            if (s.name == serverName && m_manager->isServerRunning(s)) {
                std::string msg = sanitizeRconMessage(s.formatRestartWarning(countdownMinutes));
                m_manager->sendRconCommand(s, "broadcast " + msg);
                emitLog(serverName, "Broadcast: " + msg);
                m_states[serverName].lastBroadcastMinute = countdownMinutes;
                break;
            }
        }
    }

    // If countdown is 0, skip directly to save
    if (countdownMinutes == 0) {
        performSaveAndRestart(serverName, m_states[serverName]);
    }
}

void GracefulRestartManager::cancelGracefulRestart(const std::string &serverName)
{
    auto it = m_states.find(serverName);
    if (it != m_states.end()) {
        emitLog(serverName, "Graceful restart cancelled.");
        setPhase(serverName, it->second, Phase::Idle);
        m_states.erase(it);

        // Send cancellation broadcast
        for (ServerConfig &s : m_manager->servers()) {
            if (s.name == serverName && m_manager->isServerRunning(s)) {
                m_manager->sendRconCommand(s, "broadcast Server restart has been cancelled.");
                break;
            }
        }
    }
}

bool GracefulRestartManager::isRestarting(const std::string &serverName) const
{
    auto it = m_states.find(serverName);
    return (it != m_states.end() && it->second.phase != Phase::Idle);
}

GracefulRestartManager::Phase
GracefulRestartManager::currentPhase(const std::string &serverName) const
{
    auto it = m_states.find(serverName);
    if (it != m_states.end())
        return it->second.phase;
    return Phase::Idle;
}

int GracefulRestartManager::minutesRemaining(const std::string &serverName) const
{
    auto it = m_states.find(serverName);
    if (it == m_states.end())
        return -1;

    const RestartState &state = it->second;
    if (state.phase != Phase::Countdown)
        return 0;

    auto now = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - state.startTime).count();
    int64_t totalMs = static_cast<int64_t>(state.totalCountdownMinutes) * 60 * 1000;
    int64_t remainingMs = totalMs - elapsedMs;
    if (remainingMs <= 0) return 0;
    // Ceiling division: (ms + 59999) / 60000 rounds up to the next minute
    static constexpr int64_t kMsPerMinute = 60000;
    return static_cast<int>((remainingMs + kMsPerMinute - 1) / kMsPerMinute);
}

// ---------------------------------------------------------------------------
// tick()
// ---------------------------------------------------------------------------

void GracefulRestartManager::tick()
{
    auto now = std::chrono::steady_clock::now();
    std::vector<std::string> completed;

    for (auto &[serverName, state] : m_states) {
        if (state.phase == Phase::Idle) {
            completed.push_back(serverName);
            continue;
        }

        if (state.phase != Phase::Countdown)
            continue;

        // Calculate elapsed time and minutes remaining
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - state.startTime).count();
        int64_t totalMs = static_cast<int64_t>(state.totalCountdownMinutes) * 60 * 1000;
        int64_t remainingMs = totalMs - elapsedMs;

        if (remainingMs <= 0) {
            // Countdown complete — proceed to save and restart
            performSaveAndRestart(serverName, state);
            continue;
        }

        // Calculate current minute remaining (ceiling division)
        static constexpr int64_t kMsPerMin = 60000;
        int currentMinute = static_cast<int>((remainingMs + kMsPerMin - 1) / kMsPerMin);

        // Check if we should broadcast at this minute
        auto alertMinutes = countdownAlertMinutes();
        for (int alertMin : alertMinutes) {
            if (currentMinute <= alertMin && state.lastBroadcastMinute != currentMinute
                && currentMinute > 0) {
                // Only broadcast if this alert minute applies and we haven't sent it
                if (state.lastBroadcastMinute == -1 || currentMinute < state.lastBroadcastMinute) {
                    for (ServerConfig &s : m_manager->servers()) {
                        if (s.name == serverName && m_manager->isServerRunning(s)) {
                            std::string msg = sanitizeRconMessage(s.formatRestartWarning(currentMinute));
                            m_manager->sendRconCommand(s, "broadcast " + msg);
                            emitLog(serverName, "Broadcast (" + std::to_string(currentMinute) +
                                    " min remaining): " + msg);
                            break;
                        }
                    }
                    state.lastBroadcastMinute = currentMinute;
                }
                break;
            }
        }
    }

    for (const auto &name : completed)
        m_states.erase(name);
}

// ---------------------------------------------------------------------------
// Save and restart sequence
// ---------------------------------------------------------------------------

void GracefulRestartManager::performSaveAndRestart(const std::string &serverName,
                                                    RestartState &state)
{
    // Phase: Saving
    if (!state.saveCommand.empty()) {
        setPhase(serverName, state, Phase::Saving);
        for (ServerConfig &s : m_manager->servers()) {
            if (s.name == serverName) {
                if (m_manager->isServerRunning(s)) {
                    emitLog(serverName, "Sending world save command: " + state.saveCommand);
                    m_manager->sendRconCommand(s, state.saveCommand);
                }
                break;
            }
        }
    }

    // Phase: Backing up
    setPhase(serverName, state, Phase::BackingUp);
    for (ServerConfig &s : m_manager->servers()) {
        if (s.name == serverName) {
            emitLog(serverName, "Taking pre-restart backup...");
            m_manager->takeSnapshot(s);
            break;
        }
    }

    // Phase: Restarting
    setPhase(serverName, state, Phase::Restarting);
    for (ServerConfig &s : m_manager->servers()) {
        if (s.name == serverName) {
            emitLog(serverName, "Restarting server...");
            m_manager->restartServer(s);
            break;
        }
    }

    // Done
    setPhase(serverName, state, Phase::Idle);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void GracefulRestartManager::emitLog(const std::string &serverName, const std::string &msg)
{
    if (onLogMessage) onLogMessage(serverName, msg);
}

void GracefulRestartManager::setPhase(const std::string &serverName,
                                       RestartState &state, Phase phase)
{
    state.phase = phase;
    if (onPhaseChanged) onPhaseChanged(serverName, phase);
}

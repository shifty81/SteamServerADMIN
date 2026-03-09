#include "SchedulerModule.hpp"
#include "ServerManager.hpp"

#include <iostream>
#include <chrono>
#include <ctime>

// Returns true when the current hour falls inside the per-server maintenance
// window (meaning scheduled tasks should be suppressed).
static bool inMaintenanceWindow(const ServerConfig &cfg)
{
    int startH = cfg.maintenanceStartHour;
    int endH   = cfg.maintenanceEndHour;
    if (startH < 0 || endH < 0)
        return false;

    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    int hour = tm.tm_hour;

    if (startH <= endH)
        return hour >= startH && hour < endH;
    else
        return hour >= startH || hour < endH;
}

SchedulerModule::SchedulerModule(ServerManager *manager)
    : m_manager(manager)
{
}

SchedulerModule::~SchedulerModule()
{
    stopAll();
}

// ---------------------------------------------------------------------------

void SchedulerModule::startAll()
{
    for (const ServerConfig &s : m_manager->servers())
        startScheduler(s.name);
}

void SchedulerModule::stopAll()
{
    m_timers.clear();
}

void SchedulerModule::startScheduler(const std::string &serverName)
{
    // Avoid duplicate timers
    if (m_timers.count(serverName))
        stopScheduler(serverName);

    // Find the server config and copy the interval values
    int backupMinutes = 0;
    int restartHours  = 0;
    int rconMinutes   = 0;
    int warningMinutes = 0;
    for (const ServerConfig &s : m_manager->servers()) {
        if (s.name == serverName) {
            backupMinutes  = s.backupIntervalMinutes;
            restartHours   = s.restartIntervalHours;
            rconMinutes    = s.rconCommandIntervalMinutes;
            warningMinutes = s.restartWarningMinutes;
            break;
        }
    }

    TimerState t;
    auto now = std::chrono::steady_clock::now();

    if (backupMinutes > 0) {
        t.backupIntervalMs = static_cast<int64_t>(backupMinutes) * 60 * 1000;
        t.lastBackup = now;
    }

    if (restartHours > 0) {
        t.restartIntervalMs = static_cast<int64_t>(restartHours) * 60 * 60 * 1000;
        t.lastRestart = now;
        t.restartWarningMinutes = warningMinutes;
    }

    if (rconMinutes > 0) {
        t.rconIntervalMs = static_cast<int64_t>(rconMinutes) * 60 * 1000;
        t.lastRcon = now;
    }

    m_timers[serverName] = t;
}

void SchedulerModule::stopScheduler(const std::string &serverName)
{
    m_timers.erase(serverName);
}

void SchedulerModule::tick()
{
    auto now = std::chrono::steady_clock::now();

    for (auto &[serverName, t] : m_timers) {
        // --- Automatic backup ---
        if (t.backupIntervalMs > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - t.lastBackup).count();
            if (elapsed >= t.backupIntervalMs) {
                t.lastBackup = now;
                for (ServerConfig &s : m_manager->servers()) {
                    if (s.name == serverName) {
                        if (!inMaintenanceWindow(s)) {
                            m_manager->takeSnapshot(s);
                            if (onScheduledBackup)
                                onScheduledBackup(serverName);
                        }
                        break;
                    }
                }
            }
        }

        // --- Automatic restart ---
        if (t.restartIntervalMs > 0) {
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - t.lastRestart).count();

            // Check if we should start the warning countdown
            if (t.restartWarningMinutes > 0 && !t.warningActive) {
                int64_t warningStartMs = t.restartIntervalMs
                    - static_cast<int64_t>(t.restartWarningMinutes) * 60 * 1000;
                if (warningStartMs < 0) warningStartMs = 0;
                if (elapsedMs >= warningStartMs) {
                    t.warningActive = true;
                    t.restartWarningCountdown = t.restartWarningMinutes;
                    t.lastWarningTick = now;
                    // Send first warning immediately
                    for (ServerConfig &s : m_manager->servers()) {
                        if (s.name == serverName) {
                            if (!inMaintenanceWindow(s))
                                m_manager->sendRestartWarning(s, t.restartWarningCountdown);
                            break;
                        }
                    }
                    t.restartWarningCountdown--;
                }
            }

            // Send periodic warnings every minute
            if (t.warningActive && t.restartWarningCountdown > 0) {
                auto warnElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - t.lastWarningTick).count();
                if (warnElapsed >= 60000) {
                    t.lastWarningTick = now;
                    for (ServerConfig &s : m_manager->servers()) {
                        if (s.name == serverName) {
                            if (!inMaintenanceWindow(s))
                                m_manager->sendRestartWarning(s, t.restartWarningCountdown);
                            break;
                        }
                    }
                    t.restartWarningCountdown--;
                }
            }

            // Time to restart
            if (elapsedMs >= t.restartIntervalMs) {
                t.lastRestart = now;
                t.warningActive = false;
                t.restartWarningCountdown = t.restartWarningMinutes;
                for (ServerConfig &s : m_manager->servers()) {
                    if (s.name == serverName) {
                        if (!inMaintenanceWindow(s)) {
                            if (m_manager->isServerRunning(s)) {
                                if (s.backupBeforeRestart)
                                    m_manager->takeSnapshot(s);
                                m_manager->restartServer(s);
                            }
                            if (onScheduledRestart)
                                onScheduledRestart(serverName);
                        }
                        break;
                    }
                }
            }
        }

        // --- Scheduled RCON command ---
        if (t.rconIntervalMs > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - t.lastRcon).count();
            if (elapsed >= t.rconIntervalMs) {
                t.lastRcon = now;
                if (onScheduledRconCommand)
                    onScheduledRconCommand(serverName);
            }
        }
    }
}

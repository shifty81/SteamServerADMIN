#pragma once

#include <string>
#include <map>
#include <cstdint>
#include <functional>
#include <chrono>

/**
 * @brief Per-process resource usage snapshot.
 */
struct ResourceUsage {
    double cpuPercent = 0.0;   ///< CPU usage percentage (0-100+)
    int64_t memoryBytes = 0;   ///< Resident memory in bytes
};

/**
 * @brief Monitors CPU and memory usage of managed server processes.
 *
 * Uses a poll()-based design: the caller invokes tick() periodically from the
 * main loop to read platform-specific process statistics (procfs on Linux,
 * NtQueryInformationProcess on Windows, proc_pid_rusage on macOS).
 *
 * Invokes the onUsageUpdated callback once per poll cycle with the latest readings.
 */
class ResourceMonitor {
public:
    ResourceMonitor() = default;

    /** Register a process to track by server name and PID. */
    void trackProcess(const std::string &serverName, int64_t pid);

    /** Stop tracking a server's process. */
    void untrackProcess(const std::string &serverName);

    /** Set the polling interval in milliseconds (default: 5000). */
    void setPollIntervalMs(int ms);
    int pollIntervalMs() const;

    /** Start periodic polling (sets the active flag). */
    void start();
    /** Stop periodic polling (clears the active flag). */
    void stop();

    /** Call this from the main loop. Checks elapsed time and polls if interval has passed. */
    void tick();

    /** Return the most-recent usage for a server, or a zeroed struct. */
    ResourceUsage usage(const std::string &serverName) const;

    /** Return all current readings keyed by server name. */
    std::map<std::string, ResourceUsage> allUsage() const;

    /** One-shot: read the current resource usage for a given PID.
     *  Returns a zeroed struct if the PID is invalid or the read fails. */
    static ResourceUsage readUsage(int64_t pid);

    /** Callback invoked after each poll cycle with the latest per-server readings. */
    std::function<void(const std::map<std::string, ResourceUsage> &usage)> onUsageUpdated;

private:
    void poll();

    struct TrackedProcess {
        int64_t pid = 0;
        int64_t prevCpuTime = -1;
        int64_t prevWallTime = -1;
    };

    std::map<std::string, TrackedProcess> m_tracked;
    std::map<std::string, ResourceUsage>  m_latest;
    int  m_pollIntervalMs = 5000;
    bool m_active = false;
    std::chrono::steady_clock::time_point m_lastPoll;
};

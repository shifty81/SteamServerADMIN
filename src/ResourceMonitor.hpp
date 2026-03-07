#pragma once

#include <QObject>
#include <QMap>
#include <QString>
#include <QTimer>

/**
 * @brief Per-process resource usage snapshot.
 */
struct ResourceUsage {
    double cpuPercent = 0.0;   ///< CPU usage percentage (0-100+)
    qint64 memoryBytes = 0;    ///< Resident memory in bytes
};

/**
 * @brief Monitors CPU and memory usage of managed server processes.
 *
 * Polls at a configurable interval by reading platform-specific process
 * statistics (procfs on Linux, NtQueryInformationProcess on Windows,
 * proc_pid_rusage on macOS).
 *
 * Emits @c usageUpdated once per poll cycle with the latest readings.
 */
class ResourceMonitor : public QObject {
    Q_OBJECT
public:
    explicit ResourceMonitor(QObject *parent = nullptr);

    /** Register a process to track by server name and PID. */
    void trackProcess(const QString &serverName, qint64 pid);

    /** Stop tracking a server's process. */
    void untrackProcess(const QString &serverName);

    /** Set the polling interval in milliseconds (default: 5000). */
    void setPollIntervalMs(int ms);
    int pollIntervalMs() const;

    /** Start periodic polling. */
    void start();
    /** Stop periodic polling. */
    void stop();

    /** Return the most-recent usage for a server, or a zeroed struct. */
    ResourceUsage usage(const QString &serverName) const;

    /** Return all current readings keyed by server name. */
    QMap<QString, ResourceUsage> allUsage() const;

    /** One-shot: read the current resource usage for a given PID.
     *  Returns a zeroed struct if the PID is invalid or the read fails. */
    static ResourceUsage readUsage(qint64 pid);

signals:
    /** Emitted after each poll cycle with the latest per-server readings. */
    void usageUpdated(const QMap<QString, ResourceUsage> &usage);

private slots:
    void poll();

private:
    struct TrackedProcess {
        qint64 pid = 0;
        // Previous CPU time measurement (platform ticks) for delta calculation
        qint64 prevCpuTime = -1;
        qint64 prevWallTime = -1;
    };

    QMap<QString, TrackedProcess> m_tracked;
    QMap<QString, ResourceUsage> m_latest;
    QTimer m_timer;
    int m_pollIntervalMs = 5000;
};

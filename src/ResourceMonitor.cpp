#include "ResourceMonitor.hpp"

#include <QDateTime>
#include <QElapsedTimer>

#ifdef Q_OS_LINUX
#include <QFile>
#include <QStringList>
#include <unistd.h>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

#ifdef Q_OS_MACOS
#include <mach/mach.h>
#endif

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

ResourceMonitor::ResourceMonitor(QObject *parent)
    : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &ResourceMonitor::poll);
}

// ---------------------------------------------------------------------------
// Track / untrack
// ---------------------------------------------------------------------------

void ResourceMonitor::trackProcess(const QString &serverName, qint64 pid)
{
    TrackedProcess tp;
    tp.pid = pid;
    m_tracked[serverName] = tp;
}

void ResourceMonitor::untrackProcess(const QString &serverName)
{
    m_tracked.remove(serverName);
    m_latest.remove(serverName);
}

// ---------------------------------------------------------------------------
// Polling control
// ---------------------------------------------------------------------------

void ResourceMonitor::setPollIntervalMs(int ms)
{
    m_pollIntervalMs = (ms > 0) ? ms : 1000;
    if (m_timer.isActive()) {
        m_timer.stop();
        m_timer.start(m_pollIntervalMs);
    }
}

int ResourceMonitor::pollIntervalMs() const { return m_pollIntervalMs; }

void ResourceMonitor::start()
{
    if (!m_timer.isActive())
        m_timer.start(m_pollIntervalMs);
}

void ResourceMonitor::stop()
{
    m_timer.stop();
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

ResourceUsage ResourceMonitor::usage(const QString &serverName) const
{
    return m_latest.value(serverName, ResourceUsage{});
}

QMap<QString, ResourceUsage> ResourceMonitor::allUsage() const
{
    return m_latest;
}

// ---------------------------------------------------------------------------
// Platform-specific: read resource usage for a PID
// ---------------------------------------------------------------------------

ResourceUsage ResourceMonitor::readUsage(qint64 pid)
{
    ResourceUsage ru;
    if (pid <= 0)
        return ru;

#ifdef Q_OS_LINUX
    // Memory from /proc/<pid>/statm (field 1 = resident pages)
    {
        QFile f(QStringLiteral("/proc/%1/statm").arg(pid));
        if (f.open(QIODevice::ReadOnly)) {
            QByteArray data = f.readAll();
            QStringList parts = QString::fromLatin1(data.trimmed()).split(QLatin1Char(' '));
            if (parts.size() >= 2) {
                long pageSize = sysconf(_SC_PAGESIZE);
                ru.memoryBytes = parts.at(1).toLongLong() * pageSize;
            }
        }
    }

    // CPU snapshot: /proc/<pid>/stat — fields 14 (utime) + 15 (stime) in clock ticks.
    // We cannot compute a percentage in a single read; the caller (poll()) does
    // the delta calculation between two consecutive reads.
    // Here we just return the memory; CPU is handled in poll().
#endif

#ifdef Q_OS_WIN
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                  FALSE, static_cast<DWORD>(pid));
    if (hProcess) {
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc)))
            ru.memoryBytes = static_cast<qint64>(pmc.WorkingSetSize);
        CloseHandle(hProcess);
    }
#endif

#ifdef Q_OS_MACOS
    mach_port_t task;
    if (task_for_pid(mach_task_self(), static_cast<int>(pid), &task) == KERN_SUCCESS) {
        mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
        mach_task_basic_info_data_t info;
        if (task_info(task, MACH_TASK_BASIC_INFO,
                      reinterpret_cast<task_info_t>(&info), &infoCount) == KERN_SUCCESS
            && infoCount == MACH_TASK_BASIC_INFO_COUNT) {
            ru.memoryBytes = static_cast<qint64>(info.resident_size);
        }
        mach_port_deallocate(mach_task_self(), task);
    }
#endif

    return ru;
}

// ---------------------------------------------------------------------------
// Poll cycle: compute CPU deltas and memory
// ---------------------------------------------------------------------------

void ResourceMonitor::poll()
{
    for (auto it = m_tracked.begin(); it != m_tracked.end(); ++it) {
        TrackedProcess &tp = it.value();
        ResourceUsage ru = readUsage(tp.pid);

#ifdef Q_OS_LINUX
        // Read utime + stime from /proc/<pid>/stat
        qint64 cpuTime = 0;
        {
            QFile f(QStringLiteral("/proc/%1/stat").arg(tp.pid));
            if (f.open(QIODevice::ReadOnly)) {
                QString line = QString::fromLatin1(f.readAll().trimmed());
                // Fields are space-separated; the comm field (2) may contain spaces
                // inside parentheses. Find the last ')' then split the rest.
                int closeParenIdx = line.lastIndexOf(QLatin1Char(')'));
                if (closeParenIdx > 0) {
                    QStringList fields = line.mid(closeParenIdx + 2).split(QLatin1Char(' '));
                    // fields index 0 = state (field 3), utime = index 11 (field 14), stime = index 12 (field 15)
                    if (fields.size() > 12)
                        cpuTime = fields.at(11).toLongLong() + fields.at(12).toLongLong();
                }
            }
        }

        long ticksPerSec = sysconf(_SC_CLK_TCK);
        qint64 wallNow = QDateTime::currentMSecsSinceEpoch();

        if (tp.prevCpuTime >= 0 && tp.prevWallTime > 0) {
            qint64 deltaCpu  = cpuTime - tp.prevCpuTime; // in ticks
            qint64 deltaWall = wallNow - tp.prevWallTime; // in ms
            if (deltaWall > 0 && ticksPerSec > 0) {
                double cpuSec  = static_cast<double>(deltaCpu) / ticksPerSec;
                double wallSec = static_cast<double>(deltaWall) / 1000.0;
                ru.cpuPercent = (cpuSec / wallSec) * 100.0;
            }
        }
        tp.prevCpuTime  = cpuTime;
        tp.prevWallTime = wallNow;
#else
        // On non-Linux platforms, CPU% is not computed from procfs.
        // We store 0 and let readUsage populate memory only.
        Q_UNUSED(tp);
#endif

        m_latest[it.key()] = ru;
    }

    emit usageUpdated(m_latest);
}

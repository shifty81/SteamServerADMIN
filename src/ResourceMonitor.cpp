#include "ResourceMonitor.hpp"

#include <chrono>
#include <fstream>
#include <sstream>
#include <string>

#ifdef __linux__
#include <unistd.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

#ifdef __APPLE__
#include <mach/mach.h>
#endif

// ---------------------------------------------------------------------------
// Track / untrack
// ---------------------------------------------------------------------------

void ResourceMonitor::trackProcess(const std::string &serverName, int64_t pid)
{
    TrackedProcess tp;
    tp.pid = pid;
    m_tracked[serverName] = tp;
}

void ResourceMonitor::untrackProcess(const std::string &serverName)
{
    m_tracked.erase(serverName);
    m_latest.erase(serverName);
}

// ---------------------------------------------------------------------------
// Polling control
// ---------------------------------------------------------------------------

void ResourceMonitor::setPollIntervalMs(int ms)
{
    m_pollIntervalMs = (ms > 0) ? ms : 1000;
}

int ResourceMonitor::pollIntervalMs() const { return m_pollIntervalMs; }

void ResourceMonitor::start()
{
    m_active = true;
    m_lastPoll = std::chrono::steady_clock::now();
}

void ResourceMonitor::stop()
{
    m_active = false;
}

void ResourceMonitor::tick()
{
    if (!m_active)
        return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastPoll).count();
    if (elapsed >= m_pollIntervalMs) {
        poll();
        m_lastPoll = now;
    }
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

ResourceUsage ResourceMonitor::usage(const std::string &serverName) const
{
    auto it = m_latest.find(serverName);
    if (it != m_latest.end())
        return it->second;
    return ResourceUsage{};
}

std::map<std::string, ResourceUsage> ResourceMonitor::allUsage() const
{
    return m_latest;
}

// ---------------------------------------------------------------------------
// Platform-specific: read resource usage for a PID
// ---------------------------------------------------------------------------

ResourceUsage ResourceMonitor::readUsage(int64_t pid)
{
    ResourceUsage ru;
    if (pid <= 0)
        return ru;

#ifdef __linux__
    // Memory from /proc/<pid>/statm (field 1 = resident pages)
    {
        std::ifstream f("/proc/" + std::to_string(pid) + "/statm");
        if (f.is_open()) {
            int64_t totalPages = 0, residentPages = 0;
            f >> totalPages >> residentPages;
            long pageSize = sysconf(_SC_PAGESIZE);
            ru.memoryBytes = residentPages * pageSize;
        }
    }
#endif

#ifdef _WIN32
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                  FALSE, static_cast<DWORD>(pid));
    if (hProcess) {
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc)))
            ru.memoryBytes = static_cast<int64_t>(pmc.WorkingSetSize);
        CloseHandle(hProcess);
    }
#endif

#ifdef __APPLE__
    mach_port_t task;
    if (task_for_pid(mach_task_self(), static_cast<int>(pid), &task) == KERN_SUCCESS) {
        mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
        mach_task_basic_info_data_t info;
        if (task_info(task, MACH_TASK_BASIC_INFO,
                      reinterpret_cast<task_info_t>(&info), &infoCount) == KERN_SUCCESS
            && infoCount == MACH_TASK_BASIC_INFO_COUNT) {
            ru.memoryBytes = static_cast<int64_t>(info.resident_size);
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
    for (auto &[name, tp] : m_tracked) {
        ResourceUsage ru = readUsage(tp.pid);

#ifdef __linux__
        // Read utime + stime from /proc/<pid>/stat
        int64_t cpuTime = 0;
        {
            std::ifstream f("/proc/" + std::to_string(tp.pid) + "/stat");
            if (f.is_open()) {
                std::string line;
                std::getline(f, line);
                // Fields are space-separated; the comm field (2) may contain spaces
                // inside parentheses. Find the last ')' then split the rest.
                auto closeParenIdx = line.rfind(')');
                if (closeParenIdx != std::string::npos && closeParenIdx + 2 < line.size()) {
                    std::istringstream iss(line.substr(closeParenIdx + 2));
                    std::string field;
                    std::vector<std::string> fields;
                    while (iss >> field)
                        fields.push_back(field);
                    // fields index 0 = state (field 3), utime = index 11 (field 14), stime = index 12 (field 15)
                    if (fields.size() > 12) {
                        cpuTime = std::stoll(fields[11]) + std::stoll(fields[12]);
                    }
                }
            }
        }

        long ticksPerSec = sysconf(_SC_CLK_TCK);
        auto wallNow = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        if (tp.prevCpuTime >= 0 && tp.prevWallTime > 0) {
            int64_t deltaCpu  = cpuTime - tp.prevCpuTime;  // in ticks
            int64_t deltaWall = wallNow - tp.prevWallTime;  // in ms
            if (deltaWall > 0 && ticksPerSec > 0) {
                double cpuSec  = static_cast<double>(deltaCpu) / ticksPerSec;
                double wallSec = static_cast<double>(deltaWall) / 1000.0;
                ru.cpuPercent = (cpuSec / wallSec) * 100.0;
            }
        }
        tp.prevCpuTime  = cpuTime;
        tp.prevWallTime = wallNow;
#else
        (void)tp;
#endif

        m_latest[name] = ru;
    }

    if (onUsageUpdated)
        onUsageUpdated(m_latest);
}

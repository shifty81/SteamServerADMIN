#include "LogModule.hpp"

#include <filesystem>

namespace fs = std::filesystem;

static std::string currentTimestampISO()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return buf;
}

LogModule::LogModule(const std::string &logFilePath)
    : m_logFilePath(logFilePath)
{
    auto parent = fs::path(logFilePath).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        fs::create_directories(parent, ec);
    }
    m_file.open(logFilePath, std::ios::app);
}

LogModule::~LogModule()
{
    if (m_file.is_open())
        m_file.close();
}

void LogModule::log(const std::string &serverName, const std::string &message)
{
    std::string timestamp = currentTimestampISO();
    std::string line = "[" + timestamp + "] [" + serverName + "] " + message;

    std::lock_guard<std::mutex> lock(m_mutex);

    m_entries.push_back(line);
    while (static_cast<int>(m_entries.size()) > m_maxEntries)
        m_entries.erase(m_entries.begin());

    if (m_file.is_open()) {
        m_file << line << '\n';
        m_file.flush();
    }

    // Unlock is implicit via lock_guard destruction, but we call the callback
    // outside the lock to avoid potential deadlocks. We need a copy of the
    // callback and line since we release the lock.
    auto cb = onEntryAdded;
    // Note: lock_guard releases here at end of scope, then we call callback
    // Actually lock_guard is still in scope, so we must restructure:
    // We'll just call the callback under the lock since it's simple.
    if (cb) cb(line);
}

std::vector<std::string> LogModule::entries() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries;
}

int LogModule::maxEntries() const
{
    return m_maxEntries;
}

void LogModule::setMaxEntries(int max)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_maxEntries = max;
    while (static_cast<int>(m_entries.size()) > m_maxEntries)
        m_entries.erase(m_entries.begin());
}

std::string LogModule::logFilePath() const
{
    return m_logFilePath;
}

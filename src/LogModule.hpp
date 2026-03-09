#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <functional>
#include <filesystem>
#include <chrono>
#include <ctime>

/**
 * @brief Centralized logging module that records all server operations.
 *
 * Writes timestamped log entries to a file and keeps a bounded in-memory
 * buffer so the GUI log-viewer tab can show recent history.
 */
class LogModule {
public:
    explicit LogModule(const std::string &logFilePath);
    ~LogModule();

    /** Append a timestamped entry to the log. Thread-safe. */
    void log(const std::string &serverName, const std::string &message);

    /** Return the in-memory log buffer (most-recent entries). */
    std::vector<std::string> entries() const;

    /** Maximum number of in-memory entries (default 500). */
    int maxEntries() const;
    void setMaxEntries(int max);

    /** Path of the log file on disk. */
    std::string logFilePath() const;

    /** Callback invoked every time a new log line is appended. */
    std::function<void(const std::string &formattedLine)> onEntryAdded;

private:
    std::string                m_logFilePath;
    std::ofstream              m_file;
    std::vector<std::string>   m_entries;
    int                        m_maxEntries = 500;
    mutable std::mutex         m_mutex;
};

#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <algorithm>

namespace fs = std::filesystem;

/**
 * @brief Appends RCON console I/O to per-server timestamped log files.
 *
 * When console logging is enabled for a server, every command sent and every
 * response received is recorded under:
 *   <server.dir>/ConsoleLogs/<YYYYMMDD>.log
 */
class ConsoleLogWriter {
public:
    /**
     * @brief Append a line to the console log file for the given server.
     * @param serverDir  The server installation directory.
     * @param serverName The server name (used in header).
     * @param line       The text to append (command or response).
     */
    static void append(const std::string &serverDir, const std::string &serverName,
                       const std::string &line)
    {
        fs::path logDir = fs::path(serverDir) / "ConsoleLogs";
        fs::create_directories(logDir);

        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char dateBuf[16];
        std::strftime(dateBuf, sizeof(dateBuf), "%Y%m%d", &tm);

        char timeBuf[32];
        std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%S", &tm);

        fs::path logPath = logDir / (std::string(dateBuf) + ".log");

        std::ofstream file(logPath, std::ios::app);
        if (!file.is_open())
            return;

        file << timeBuf << " [" << serverName << "] " << line << "\n";
    }

    /**
     * @brief List available console log files for a server, newest first.
     */
    static std::vector<std::string> listLogs(const std::string &serverDir)
    {
        fs::path dir = fs::path(serverDir) / "ConsoleLogs";
        if (!fs::exists(dir))
            return {};

        std::vector<std::string> result;
        for (const auto &entry : fs::directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".log")
                result.push_back(entry.path().filename().string());
        }
        // Sort newest first (reverse alphabetical for YYYYMMDD names)
        std::sort(result.begin(), result.end(), std::greater<std::string>());
        return result;
    }
};

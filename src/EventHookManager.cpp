#include "EventHookManager.hpp"
#include "ServerConfig.hpp"

#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <string>

#ifdef _WIN32
#include <windows.h>
#define popen  _popen
#define pclose _pclose
#else
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#endif

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Known events
// ---------------------------------------------------------------------------

std::vector<std::string> EventHookManager::knownEvents()
{
    return {
        "onStart",
        "onStop",
        "onCrash",
        "onBackup",
        "onUpdate",
    };
}

// ---------------------------------------------------------------------------
// Fire hook
// ---------------------------------------------------------------------------

void EventHookManager::fireHook(const std::string &serverName,
                                const std::string &serverDir,
                                const std::string &event,
                                const std::string &scriptPath)
{
    if (trimString(scriptPath).empty())
        return;

    // Resolve the script path relative to the server directory if not absolute.
    std::string resolved = scriptPath;
    if (!fs::path(resolved).is_absolute())
        resolved = (fs::path(serverDir) / scriptPath).string();

    if (!fs::exists(resolved)) {
        if (onHookFinished)
            onHookFinished(serverName, event, -1, "Script not found: " + resolved);
        return;
    }

    int timeout = m_timeoutSeconds;
    auto callback = onHookFinished;

    // Run the hook in a detached thread to avoid blocking
    std::thread([resolved, serverName, serverDir, event, timeout, callback]() {
        // Set environment variables and run the script
        std::string cmd;
#ifdef _WIN32
        cmd = "set SSA_SERVER_NAME=" + serverName +
              " && set SSA_EVENT=" + event +
              " && set SSA_SERVER_DIR=" + serverDir +
              " && cd /d " + serverDir +
              " && " + resolved + " 2>&1";
#else
        cmd = "cd " + serverDir +
              " && SSA_SERVER_NAME='" + serverName + "'"
              " SSA_EVENT='" + event + "'"
              " SSA_SERVER_DIR='" + serverDir + "'"
              " " + resolved + " 2>&1";
#endif

        std::string output;
        FILE *pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            if (callback)
                callback(serverName, event, -1, "Failed to execute script");
            return;
        }

        char buffer[512];
        while (fgets(buffer, sizeof(buffer), pipe))
            output += buffer;

        int status = pclose(pipe);
        int exitCode = 0;
#ifdef _WIN32
        exitCode = status;
#else
        if (WIFEXITED(status))
            exitCode = WEXITSTATUS(status);
        else
            exitCode = -1;
#endif

        // Trim trailing whitespace
        while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
            output.pop_back();

        if (callback)
            callback(serverName, event, exitCode, output);
    }).detach();
}

// ---------------------------------------------------------------------------
// Timeout
// ---------------------------------------------------------------------------

void EventHookManager::setTimeoutSeconds(int seconds)
{
    m_timeoutSeconds = (seconds >= 0) ? seconds : 0;
}

int EventHookManager::timeoutSeconds() const
{
    return m_timeoutSeconds;
}

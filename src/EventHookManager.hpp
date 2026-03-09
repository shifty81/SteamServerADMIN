#pragma once

#include <string>
#include <vector>
#include <functional>

/**
 * @brief Runs user-defined scripts on server lifecycle events.
 *
 * Each server may define scripts for any of the following event names:
 *   - "onStart"  -- fired immediately after a server process starts
 *   - "onStop"   -- fired after a server process is stopped
 *   - "onCrash"  -- fired when a crash is detected
 *   - "onBackup" -- fired after a backup snapshot completes
 *   - "onUpdate" -- fired after a mod / game update
 *
 * The script path is resolved relative to the server's install directory
 * unless it is an absolute path.  The following environment variables are
 * set for every hook invocation:
 *   SSA_SERVER_NAME, SSA_EVENT, SSA_SERVER_DIR
 *
 * Scripts are executed asynchronously and do not block the caller.
 */
class EventHookManager {
public:
    EventHookManager() = default;

    /** Known hook event names. */
    static std::vector<std::string> knownEvents();

    /** Fire a hook script for the given server and event.
     *  @param serverName  Display name of the server (for logging).
     *  @param serverDir   Server installation directory.
     *  @param event       Event name (e.g. "onStart").
     *  @param scriptPath  Path to the script / executable to run.
     *
     *  Does nothing if @p scriptPath is empty. */
    void fireHook(const std::string &serverName,
                  const std::string &serverDir,
                  const std::string &event,
                  const std::string &scriptPath);

    /** Set the maximum time in seconds to wait for a hook script.
     *  A value of 0 means fire-and-forget (default). */
    void setTimeoutSeconds(int seconds);
    int timeoutSeconds() const;

    /** Callback invoked when a hook finishes (success or failure). */
    std::function<void(const std::string &serverName, const std::string &event,
                       int exitCode, const std::string &output)> onHookFinished;

private:
    int m_timeoutSeconds = 0;
};

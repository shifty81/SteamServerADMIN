#pragma once

#include <QObject>
#include <QMap>
#include <QString>

/**
 * @brief Runs user-defined scripts on server lifecycle events.
 *
 * Each server may define scripts for any of the following event names:
 *   - "onStart"  — fired immediately after a server process starts
 *   - "onStop"   — fired after a server process is stopped
 *   - "onCrash"  — fired when a crash is detected
 *   - "onBackup" — fired after a backup snapshot completes
 *   - "onUpdate" — fired after a mod / game update
 *
 * The script path is resolved relative to the server's install directory
 * unless it is an absolute path.  The following environment variables are
 * set for every hook invocation:
 *   SSA_SERVER_NAME, SSA_EVENT, SSA_SERVER_DIR
 *
 * Scripts are executed asynchronously and do not block the UI thread.
 */
class EventHookManager : public QObject {
    Q_OBJECT
public:
    explicit EventHookManager(QObject *parent = nullptr);

    /** Known hook event names. */
    static QStringList knownEvents();

    /** Fire a hook script for the given server and event.
     *  @param serverName  Display name of the server (for logging).
     *  @param serverDir   Server installation directory.
     *  @param event       Event name (e.g. "onStart").
     *  @param scriptPath  Path to the script / executable to run.
     *
     *  Does nothing if @p scriptPath is empty. */
    void fireHook(const QString &serverName,
                  const QString &serverDir,
                  const QString &event,
                  const QString &scriptPath);

    /** Set the maximum time in seconds to wait for a hook script.
     *  A value of 0 means fire-and-forget (default). */
    void setTimeoutSeconds(int seconds);
    int timeoutSeconds() const;

signals:
    /** Emitted when a hook finishes (success or failure). */
    void hookFinished(const QString &serverName, const QString &event,
                      int exitCode, const QString &output);

private:
    int m_timeoutSeconds = 0;
};

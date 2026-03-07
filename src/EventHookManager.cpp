#include "EventHookManager.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

EventHookManager::EventHookManager(QObject *parent)
    : QObject(parent)
{
}

// ---------------------------------------------------------------------------
// Known events
// ---------------------------------------------------------------------------

QStringList EventHookManager::knownEvents()
{
    return {
        QStringLiteral("onStart"),
        QStringLiteral("onStop"),
        QStringLiteral("onCrash"),
        QStringLiteral("onBackup"),
        QStringLiteral("onUpdate"),
    };
}

// ---------------------------------------------------------------------------
// Fire hook
// ---------------------------------------------------------------------------

void EventHookManager::fireHook(const QString &serverName,
                                const QString &serverDir,
                                const QString &event,
                                const QString &scriptPath)
{
    if (scriptPath.trimmed().isEmpty())
        return;

    // Resolve the script path relative to the server directory if not absolute.
    QString resolved = scriptPath;
    if (!QFileInfo(resolved).isAbsolute())
        resolved = serverDir + QDir::separator() + scriptPath;

    if (!QFileInfo::exists(resolved)) {
        emit hookFinished(serverName, event, -1,
                          QStringLiteral("Script not found: %1").arg(resolved));
        return;
    }

    // Set up the subprocess
    auto *proc = new QProcess(this);
    proc->setWorkingDirectory(serverDir);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("SSA_SERVER_NAME"), serverName);
    env.insert(QStringLiteral("SSA_EVENT"), event);
    env.insert(QStringLiteral("SSA_SERVER_DIR"), serverDir);
    proc->setProcessEnvironment(env);

    // Capture server name and event for the finished lambda
    QString sName = serverName;
    QString evName = event;
    int timeout = m_timeoutSeconds;

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, sName, evName](int exitCode, QProcess::ExitStatus /*st*/) {
                QString output = QString::fromLocal8Bit(proc->readAllStandardOutput())
                               + QString::fromLocal8Bit(proc->readAllStandardError());
                emit hookFinished(sName, evName, exitCode, output.trimmed());
                proc->deleteLater();
            });

    proc->start(resolved);

    // Optionally enforce a timeout
    if (timeout > 0) {
        QTimer::singleShot(timeout * 1000, proc, [proc]() {
            if (proc->state() != QProcess::NotRunning) {
                proc->terminate();
                if (!proc->waitForFinished(3000))
                    proc->kill();
            }
        });
    }
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

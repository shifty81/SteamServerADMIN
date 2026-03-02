#include "SchedulerModule.hpp"
#include "ServerManager.hpp"

#include <QDebug>

SchedulerModule::SchedulerModule(ServerManager *manager, QObject *parent)
    : QObject(parent), m_manager(manager)
{
}

SchedulerModule::~SchedulerModule()
{
    stopAll();
}

// ---------------------------------------------------------------------------

void SchedulerModule::startAll()
{
    for (const ServerConfig &s : m_manager->servers())
        startScheduler(s.name);
}

void SchedulerModule::stopAll()
{
    const QStringList names = m_timers.keys();
    for (const QString &name : names)
        stopScheduler(name);
}

void SchedulerModule::startScheduler(const QString &serverName)
{
    // Avoid duplicate timers
    if (m_timers.contains(serverName))
        stopScheduler(serverName);

    // Find the server config
    const ServerConfig *cfg = nullptr;
    for (const ServerConfig &s : m_manager->servers()) {
        if (s.name == serverName) {
            cfg = &s;
            break;
        }
    }
    if (!cfg)
        return;

    Timers t;

    // --- Automatic backup timer ---
    if (cfg->backupIntervalMinutes > 0) {
        t.backupTimer = new QTimer(this);
        t.backupTimer->setTimerType(Qt::VeryCoarseTimer);

        QString name = serverName;   // capture by value
        connect(t.backupTimer, &QTimer::timeout, this, [this, name]() {
            // Look up the server fresh – the list may have been reallocated
            for (ServerConfig &s : m_manager->servers()) {
                if (s.name == name) {
                    m_manager->takeSnapshot(s);
                    emit scheduledBackup(name);
                    break;
                }
            }
        });

        t.backupTimer->start(cfg->backupIntervalMinutes * 60 * 1000);
    }

    // --- Automatic restart timer ---
    if (cfg->restartIntervalHours > 0) {
        t.restartTimer = new QTimer(this);
        t.restartTimer->setTimerType(Qt::VeryCoarseTimer);

        QString name = serverName;
        connect(t.restartTimer, &QTimer::timeout, this, [this, name]() {
            for (ServerConfig &s : m_manager->servers()) {
                if (s.name == name) {
                    if (m_manager->isServerRunning(s))
                        m_manager->restartServer(s);
                    emit scheduledRestart(name);
                    break;
                }
            }
        });

        t.restartTimer->start(cfg->restartIntervalHours * 60 * 60 * 1000);
    }

    m_timers[serverName] = t;
}

void SchedulerModule::stopScheduler(const QString &serverName)
{
    auto it = m_timers.find(serverName);
    if (it == m_timers.end())
        return;

    if (it->backupTimer) {
        it->backupTimer->stop();
        it->backupTimer->deleteLater();
    }
    if (it->restartTimer) {
        it->restartTimer->stop();
        it->restartTimer->deleteLater();
    }

    m_timers.erase(it);
}

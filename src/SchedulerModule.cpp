#include "SchedulerModule.hpp"
#include "ServerManager.hpp"

#include <QDebug>
#include <QTime>

// Returns true when the current hour falls inside the per-server maintenance
// window (meaning scheduled tasks should be suppressed).
static bool inMaintenanceWindow(const ServerConfig &cfg)
{
    int startH = cfg.maintenanceStartHour;
    int endH   = cfg.maintenanceEndHour;
    if (startH < 0 || endH < 0)
        return false;                       // disabled

    int now = QTime::currentTime().hour();

    if (startH <= endH)                     // e.g. 02-06
        return now >= startH && now < endH;
    else                                    // wraps midnight, e.g. 22-04
        return now >= startH || now < endH;
}

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

    // Find the server config and copy the interval values
    int backupMinutes = 0;
    int restartHours  = 0;
    int rconMinutes   = 0;
    int updateCheckMinutes = 0;
    for (const ServerConfig &s : m_manager->servers()) {
        if (s.name == serverName) {
            backupMinutes = s.backupIntervalMinutes;
            restartHours  = s.restartIntervalHours;
            rconMinutes   = s.rconCommandIntervalMinutes;
            updateCheckMinutes = s.autoUpdateCheckIntervalMinutes;
            break;
        }
    }

    Timers t;

    // --- Automatic backup timer ---
    if (backupMinutes > 0) {
        t.backupTimer = new QTimer(this);
        t.backupTimer->setTimerType(Qt::VeryCoarseTimer);

        QString name = serverName;   // capture by value
        connect(t.backupTimer, &QTimer::timeout, this, [this, name]() {
            // Look up the server fresh – the list may have been reallocated
            for (ServerConfig &s : m_manager->servers()) {
                if (s.name == name) {
                    if (inMaintenanceWindow(s))
                        return;  // skip during maintenance
                    m_manager->takeSnapshot(s);
                    emit scheduledBackup(name);
                    break;
                }
            }
        });

        t.backupTimer->start(backupMinutes * 60 * 1000);
    }

    // --- Automatic restart timer ---
    if (restartHours > 0) {
        t.restartTimer = new QTimer(this);
        t.restartTimer->setTimerType(Qt::VeryCoarseTimer);

        // Look up the restart warning configuration
        int warningMinutes = 0;
        for (const ServerConfig &s : m_manager->servers()) {
            if (s.name == serverName) {
                warningMinutes = s.restartWarningMinutes;
                break;
            }
        }

        // --- Restart warning countdown timer ---
        // If warnings are enabled, start a warning timer that fires periodically
        // once the restart is approaching.  The warning timer starts
        // (restartHours * 60 - warningMinutes) minutes after the restart timer
        // begins, then fires every minute to broadcast the countdown.
        if (warningMinutes > 0) {
            // Clamp warning to the restart interval so it doesn't exceed it
            int restartTotalMinutes = restartHours * 60;
            if (warningMinutes > restartTotalMinutes) {
                qWarning() << "Restart warning" << warningMinutes
                           << "min exceeds restart interval" << restartTotalMinutes
                           << "min for" << serverName << "; clamping.";
                warningMinutes = restartTotalMinutes;
            }

            int restartMs     = restartHours * 60 * 60 * 1000;
            int warningStartMs = restartMs - warningMinutes * 60 * 1000;
            if (warningStartMs < 0) warningStartMs = 0;

            // Single-shot timer to kick off the countdown once we're inside the
            // warning window.
            t.restartWarningCountdown = warningMinutes;
            QString name2 = serverName;
            QTimer::singleShot(warningStartMs, this, [this, name2]() {
                auto it = m_timers.find(name2);
                if (it == m_timers.end()) return;

                it->restartWarningTimer = new QTimer(this);
                it->restartWarningTimer->setTimerType(Qt::CoarseTimer);

                connect(it->restartWarningTimer, &QTimer::timeout, this, [this, name2]() {
                    auto jt = m_timers.find(name2);
                    if (jt == m_timers.end()) return;

                    int mins = jt->restartWarningCountdown;
                    if (mins <= 0) return;

                    for (ServerConfig &s : m_manager->servers()) {
                        if (s.name == name2) {
                            if (!inMaintenanceWindow(s))
                                m_manager->sendRestartWarning(s, mins);
                            break;
                        }
                    }
                    jt->restartWarningCountdown = mins - 1;
                });
                it->restartWarningTimer->start(60 * 1000);  // fire every minute

                // Send the first warning immediately
                for (ServerConfig &s : m_manager->servers()) {
                    if (s.name == name2) {
                        if (!inMaintenanceWindow(s))
                            m_manager->sendRestartWarning(s, it->restartWarningCountdown);
                        break;
                    }
                }
                it->restartWarningCountdown -= 1;
            });
        }

        QString name = serverName;
        connect(t.restartTimer, &QTimer::timeout, this, [this, name]() {
            for (ServerConfig &s : m_manager->servers()) {
                if (s.name == name) {
                    if (inMaintenanceWindow(s))
                        return;  // skip during maintenance
                    if (m_manager->isServerRunning(s)) {
                        // Take a pre-restart snapshot if enabled
                        if (s.backupBeforeRestart)
                            m_manager->takeSnapshot(s);
                        m_manager->restartServer(s);
                    }
                    emit scheduledRestart(name);
                    break;
                }
            }
        });

        t.restartTimer->start(restartHours * 60 * 60 * 1000);
    }

    // --- Scheduled RCON command timer ---
    if (rconMinutes > 0) {
        t.rconTimer = new QTimer(this);
        t.rconTimer->setTimerType(Qt::VeryCoarseTimer);

        QString name = serverName;
        connect(t.rconTimer, &QTimer::timeout, this, [this, name]() {
            emit scheduledRconCommand(name);
        });

        t.rconTimer->start(rconMinutes * 60 * 1000);
    }

    // --- Automatic update check timer ---
    if (updateCheckMinutes > 0) {
        t.updateCheckTimer = new QTimer(this);
        t.updateCheckTimer->setTimerType(Qt::VeryCoarseTimer);

        QString name = serverName;
        connect(t.updateCheckTimer, &QTimer::timeout, this, [this, name]() {
            for (const ServerConfig &s : m_manager->servers()) {
                if (s.name == name) {
                    if (inMaintenanceWindow(s))
                        return;  // skip during maintenance
                    bool pending = m_manager->checkForUpdate(s);
                    m_manager->setPendingUpdate(name, pending);
                    emit scheduledUpdateCheck(name);
                    break;
                }
            }
        });

        t.updateCheckTimer->start(updateCheckMinutes * 60 * 1000);
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
    if (it->rconTimer) {
        it->rconTimer->stop();
        it->rconTimer->deleteLater();
    }
    if (it->restartWarningTimer) {
        it->restartWarningTimer->stop();
        it->restartWarningTimer->deleteLater();
    }
    if (it->updateCheckTimer) {
        it->updateCheckTimer->stop();
        it->updateCheckTimer->deleteLater();
    }

    m_timers.erase(it);
}

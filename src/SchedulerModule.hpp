#pragma once

#include "ServerConfig.hpp"

#include <QObject>
#include <QTimer>
#include <QMap>
#include <QString>

class ServerManager;

/**
 * @brief Manages per-server scheduled tasks (automatic backups and restarts).
 *
 * Each server gets its own pair of timers driven by:
 *   - ServerConfig::backupIntervalMinutes  → periodic snapshots
 *   - ServerConfig::restartIntervalHours   → periodic restarts
 *
 * Timers are created when startScheduler() is called and cleaned up on stop
 * or destruction.
 */
class SchedulerModule : public QObject {
    Q_OBJECT
public:
    explicit SchedulerModule(ServerManager *manager, QObject *parent = nullptr);
    ~SchedulerModule() override;

    /** Create and start timers for every server currently in the manager. */
    void startAll();

    /** Stop and destroy all timers. */
    void stopAll();

    /** Start timers for a single server (e.g. after adding one at runtime). */
    void startScheduler(const QString &serverName);

    /** Stop timers for a single server. */
    void stopScheduler(const QString &serverName);

signals:
    void scheduledBackup(const QString &serverName);
    void scheduledRestart(const QString &serverName);

private:
    struct Timers {
        QTimer *backupTimer  = nullptr;
        QTimer *restartTimer = nullptr;
    };

    ServerManager *m_manager;
    QMap<QString, Timers> m_timers;
};

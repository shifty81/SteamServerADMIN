#pragma once

#include "ServerManager.hpp"

#include <QWidget>
#include <QLabel>
#include <QList>

/**
 * @brief Home dashboard showing all servers with rich badge cards and quick actions.
 *
 * Each server is displayed as a badge card showing:
 *   - Health status light (🟢/🟡/🔴)
 *   - Uptime
 *   - Pending game update indicator
 *   - Pending mod update indicator
 *   - Player count / max players
 *
 * Right-click context menu on each badge offers Save and Restart (with
 * configurable in-game warning countdown).
 *
 * Automatically polls every 5 seconds to update status indicators.
 */
class HomeDashboard : public QWidget {
    Q_OBJECT
public:
    explicit HomeDashboard(ServerManager *manager, QWidget *parent = nullptr);

    /** Rebuild the server rows (call after adding/removing servers). */
    void refresh();

public slots:
    void updateStatus();

private:
    ServerManager *m_manager;

    struct ServerBadge {
        QLabel *statusLight    = nullptr;
        QLabel *nameLabel      = nullptr;
        QLabel *playersLabel   = nullptr;
        QLabel *uptimeLabel    = nullptr;
        QLabel *updateBadge    = nullptr;  // pending game update indicator
        QLabel *modUpdateBadge = nullptr;  // pending mod update indicator
        QWidget *card          = nullptr;  // the card widget for context menu
    };
    QList<ServerBadge> m_badges;

    QWidget *m_rowContainer = nullptr;

    // Cluster summary labels
    QLabel *m_totalLabel   = nullptr;
    QLabel *m_onlineLabel  = nullptr;
    QLabel *m_offlineLabel = nullptr;
};

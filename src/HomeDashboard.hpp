#pragma once

#include "ServerManager.hpp"

#include <QWidget>
#include <QLabel>
#include <QList>

/**
 * @brief Home dashboard showing all servers with status lights and quick actions.
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

    struct ServerRow {
        QLabel *statusLight = nullptr;
        QLabel *nameLabel   = nullptr;
        QLabel *playersLabel = nullptr;
    };
    QList<ServerRow> m_rows;

    QWidget *m_rowContainer = nullptr;

    // Cluster summary labels
    QLabel *m_totalLabel   = nullptr;
    QLabel *m_onlineLabel  = nullptr;
    QLabel *m_offlineLabel = nullptr;
};

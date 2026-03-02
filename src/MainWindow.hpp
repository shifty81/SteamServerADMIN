#pragma once

#include "ServerManager.hpp"

#include <QMainWindow>
#include <QTabWidget>
#include <QListWidget>
#include <QLineEdit>

class HomeDashboard;
class ServerTabWidget;

/**
 * @brief Main application window.
 *
 * Layout:
 *   ┌──────────┬────────────────────────────────────────┐
 *   │ Sidebar  │  Tab area (Home Dashboard + per-server)│
 *   │ Search   │                                        │
 *   │ List     │                                        │
 *   │ [Add]    │                                        │
 *   │ [Sync]   │                                        │
 *   └──────────┴────────────────────────────────────────┘
 */
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void onAddServer();
    void onSyncMods();
    void onSyncConfigs();
    void onSearchChanged(const QString &text);
    void onServerListItemClicked(QListWidgetItem *item);

private:
    void addServerTab(ServerConfig &server);
    void rebuildSidebarList();

    ServerManager   *m_manager        = nullptr;
    QTabWidget      *m_tabs           = nullptr;
    QListWidget     *m_serverList     = nullptr;
    QLineEdit       *m_searchBox      = nullptr;
    HomeDashboard   *m_dashboard      = nullptr;
};

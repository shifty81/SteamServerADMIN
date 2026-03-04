#pragma once

#include "ServerManager.hpp"
#include <QWidget>
#include <QTabWidget>
#include <QTextEdit>
#include <QTableWidget>
#include <QListWidget>
#include <QLabel>
#include <QTimer>

/**
 * @brief Per-server tabbed widget.
 *
 * Sub-tabs:
 *   Overview  – status light, quick actions (start/stop/restart/deploy), uptime
 *   Config    – text editor for the primary config file
 *   Mods      – mod list with add/remove/update
 *   Backups   – snapshot list with take/restore actions
 *   Console   – live RCON command console with command history
 */
class ServerTabWidget : public QWidget {
    Q_OBJECT
public:
    explicit ServerTabWidget(ServerManager *manager,
                             ServerConfig  &server,
                             QWidget       *parent = nullptr);

    const QString &serverName() const;

    /** Update the status-light label in the Overview tab. */
    void refreshStatus();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onSendCommand();
    void onAddMod();
    void onRemoveMod();
    void onUpdateMods();
    void onTakeSnapshot();
    void onRestoreSnapshot();
    void onSaveConfig();
    void onDeployServer();

private:
    void buildOverviewTab(QTabWidget *tabs);
    void buildConfigTab(QTabWidget *tabs);
    void buildModsTab(QTabWidget *tabs);
    void buildBackupsTab(QTabWidget *tabs);
    void buildConsoleTab(QTabWidget *tabs);

    void appendConsole(const QString &text);
    void populateModTable();
    void populateBackupList();

    ServerManager *m_manager;
    ServerConfig  &m_server;

    // Overview
    QLabel  *m_statusLight  = nullptr;
    QLabel  *m_playersLabel = nullptr;
    QLabel  *m_uptimeLabel  = nullptr;

    // Config
    QTextEdit *m_configEditor = nullptr;
    QString    m_configPath;

    // Mods
    QTableWidget *m_modTable = nullptr;

    // Backups
    QListWidget *m_backupList = nullptr;

    // Console
    QTextEdit *m_consoleOutput = nullptr;
    QWidget   *m_consoleInput  = nullptr;   // QLineEdit stored via cast

    // RCON command history
    QStringList m_commandHistory;
    int         m_historyIndex = -1;

    QTimer *m_statusTimer = nullptr;
};

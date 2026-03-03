#include "MainWindow.hpp"
#include "HomeDashboard.hpp"
#include "ServerTabWidget.hpp"
#include "SchedulerModule.hpp"
#include "LogModule.hpp"
#include "TrayManager.hpp"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QLineEdit>
#include <QInputDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QDir>
#include <QApplication>
#include <QTextEdit>
#include <QFontDatabase>
#include <QTimer>
#include <QMenuBar>
#include <QPalette>
#include <QStyle>

static const QString kConfigFile = QStringLiteral("servers.json");

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(tr("SSA – Steam Server ADMIN"));
    resize(1600, 900);

    // ---- Menu bar ----
    auto *viewMenu = menuBar()->addMenu(tr("&View"));
    auto *darkModeAction = viewMenu->addAction(tr("Toggle Dark Mode"));
    connect(darkModeAction, &QAction::triggered, this, &MainWindow::toggleDarkMode);

    // Apply saved theme preference
    QSettings settings;
    if (settings.value(QStringLiteral("darkMode"), false).toBool())
        toggleDarkMode();

    // ---- Backend ----
    m_manager = new ServerManager(kConfigFile, this);
    m_manager->loadConfig();

    // ---- Logging ----
    m_logModule = new LogModule(QStringLiteral("ssa.log"), this);
    connect(m_manager, &ServerManager::logMessage, m_logModule, &LogModule::log);

    // ---- System tray ----
    m_trayManager = new TrayManager(this, this);
    connect(m_trayManager, &TrayManager::quitRequested, qApp, &QApplication::quit);
    connect(m_manager, &ServerManager::serverCrashed, this, [this](const QString &name) {
        m_trayManager->notify(tr("Server Crashed"),
                              tr("'%1' crashed and is being restarted.").arg(name),
                              QSystemTrayIcon::Warning);
    });

    // ---- Central widget ----
    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // ---- Sidebar ----
    auto *sidebar    = new QWidget(central);
    sidebar->setFixedWidth(220);
    auto *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(0, 0, 0, 0);

    auto *sideTitle = new QLabel(tr("Servers"), sidebar);
    sideTitle->setStyleSheet(QStringLiteral("font-weight:bold; font-size:14px; padding:4px;"));
    sideLayout->addWidget(sideTitle);

    m_searchBox = new QLineEdit(sidebar);
    m_searchBox->setPlaceholderText(tr("Search servers…"));
    sideLayout->addWidget(m_searchBox);

    m_serverList = new QListWidget(sidebar);
    sideLayout->addWidget(m_serverList);

    auto *addBtn      = new QPushButton(tr("＋  Add Server"), sidebar);
    auto *cloneBtn    = new QPushButton(tr("📋  Clone Server"), sidebar);
    auto *removeBtn   = new QPushButton(tr("✕  Remove Server"), sidebar);
    auto *exportBtn   = new QPushButton(tr("📤  Export Server"), sidebar);
    auto *importBtn   = new QPushButton(tr("📥  Import Server"), sidebar);
    auto *syncModsBtn = new QPushButton(tr("⟳  Sync Mods (All)"), sidebar);
    auto *syncCfgBtn  = new QPushButton(tr("⟳  Sync Configs (All)"), sidebar);
    auto *broadcastBtn= new QPushButton(tr("📢  Broadcast Command"), sidebar);
    sideLayout->addWidget(addBtn);
    sideLayout->addWidget(cloneBtn);
    sideLayout->addWidget(removeBtn);
    sideLayout->addWidget(exportBtn);
    sideLayout->addWidget(importBtn);
    sideLayout->addWidget(syncModsBtn);
    sideLayout->addWidget(syncCfgBtn);
    sideLayout->addWidget(broadcastBtn);

    mainLayout->addWidget(sidebar);

    // ---- Tab area ----
    m_tabs = new QTabWidget(central);
    mainLayout->addWidget(m_tabs, 1);

    // Home dashboard is always the first tab
    m_dashboard = new HomeDashboard(m_manager, m_tabs);
    m_tabs->addTab(m_dashboard, tr("🏠 Home"));

    // Create one tab per server from config
    for (ServerConfig &s : m_manager->servers())
        addServerTab(s);

    rebuildSidebarList();

    // Log Viewer tab (always last)
    buildLogViewerTab();

    // Start scheduled tasks (automatic backups and restarts)
    m_scheduler = new SchedulerModule(m_manager, this);
    m_scheduler->startAll();

    // ---- Connections ----
    connect(addBtn,       &QPushButton::clicked, this, &MainWindow::onAddServer);
    connect(cloneBtn,     &QPushButton::clicked, this, &MainWindow::onCloneServer);
    connect(removeBtn,    &QPushButton::clicked, this, &MainWindow::onRemoveServer);
    connect(exportBtn,    &QPushButton::clicked, this, &MainWindow::onExportServer);
    connect(importBtn,    &QPushButton::clicked, this, &MainWindow::onImportServer);
    connect(syncModsBtn,  &QPushButton::clicked, this, &MainWindow::onSyncMods);
    connect(syncCfgBtn,   &QPushButton::clicked, this, &MainWindow::onSyncConfigs);
    connect(broadcastBtn, &QPushButton::clicked, this, &MainWindow::onBroadcastCommand);
    connect(m_searchBox, &QLineEdit::textChanged, this, &MainWindow::onSearchChanged);
    connect(m_serverList, &QListWidget::itemClicked,
            this, &MainWindow::onServerListItemClicked);

    // Periodic tab-status update (every 5 seconds)
    auto *tabStatusTimer = new QTimer(this);
    connect(tabStatusTimer, &QTimer::timeout, this, &MainWindow::updateTabStatusIndicators);
    tabStatusTimer->start(5000);
}

// ---------------------------------------------------------------------------

void MainWindow::addServerTab(ServerConfig &server)
{
    auto *tab = new ServerTabWidget(m_manager, server, m_tabs);
    m_tabs->addTab(tab, server.name);
}

void MainWindow::rebuildSidebarList()
{
    m_serverList->clear();
    for (const ServerConfig &s : m_manager->servers())
        m_serverList->addItem(s.name);
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void MainWindow::onAddServer()
{
    bool ok = false;

    QString name = QInputDialog::getText(
        this, tr("Add Server"), tr("Server Name:"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    name = name.trimmed();

    int appid = QInputDialog::getInt(
        this, tr("Add Server"), tr("Steam AppID:"),
        0, 0, INT_MAX, 1, &ok);
    if (!ok) return;

    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Installation Directory"));
    if (dir.isEmpty()) return;

    QString exe = QInputDialog::getText(
        this, tr("Add Server"),
        tr("Server executable (relative to install dir, e.g. ShooterGameServer.exe):"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok) return;

    // RCON settings (optional – user can skip)
    QString rconHost = QInputDialog::getText(
        this, tr("RCON Settings"), tr("RCON host (leave blank for 127.0.0.1):"),
        QLineEdit::Normal, QStringLiteral("127.0.0.1"), &ok);
    if (!ok) rconHost = QStringLiteral("127.0.0.1");

    int rconPort = QInputDialog::getInt(
        this, tr("RCON Settings"), tr("RCON port:"), 27015, 1, 65535, 1, &ok);
    if (!ok) rconPort = 27015;

    QString rconPass = QInputDialog::getText(
        this, tr("RCON Settings"), tr("RCON password:"),
        QLineEdit::Password, QString(), &ok);
    if (!ok) rconPass.clear();

    ServerConfig s;
    s.name         = name;
    s.appid        = appid;
    s.dir          = dir;
    s.executable   = exe;
    s.backupFolder = dir + QStringLiteral("/Backups");
    s.rcon.host    = rconHost;
    s.rcon.port    = rconPort;
    s.rcon.password= rconPass;

    // Tentatively add the server and validate the full list (catches
    // per-field errors and duplicate names in one place).
    m_manager->servers() << s;
    QStringList errors = m_manager->validateAll();
    if (!errors.isEmpty()) {
        m_manager->servers().removeLast();
        QMessageBox::warning(this, tr("Validation Error"),
                             tr("Cannot add server:\n\n%1").arg(errors.join(QLatin1Char('\n'))));
        return;
    }

    m_manager->saveConfig();
    addServerTab(m_manager->servers().last());
    rebuildSidebarList();
    m_dashboard->refresh();
    m_scheduler->startScheduler(s.name);

    // Offer to immediately deploy via SteamCMD
    auto reply = QMessageBox::question(
        this, tr("Deploy Server"),
        tr("Deploy '%1' (AppID %2) via SteamCMD now?").arg(name).arg(appid));
    if (reply == QMessageBox::Yes)
        m_manager->deployServer(m_manager->servers().last());
}

void MainWindow::onSyncMods()
{
    auto reply = QMessageBox::question(
        this, tr("Sync Mods"),
        tr("Update mods for ALL servers via SteamCMD?"));
    if (reply == QMessageBox::Yes)
        m_manager->syncModsCluster();
}

void MainWindow::onSyncConfigs()
{
    QString zip = QFileDialog::getOpenFileName(
        this, tr("Select Master Config Zip"),
        QString(), tr("Zip archives (*.zip)"));
    if (zip.isEmpty()) return;

    auto reply = QMessageBox::question(
        this, tr("Sync Configs"),
        tr("Distribute config from:\n%1\nto ALL servers?").arg(zip));
    if (reply == QMessageBox::Yes)
        m_manager->syncConfigsCluster(zip);
}

void MainWindow::onSearchChanged(const QString &text)
{
    for (int i = 0; i < m_serverList->count(); ++i) {
        QListWidgetItem *item = m_serverList->item(i);
        item->setHidden(!item->text().contains(text, Qt::CaseInsensitive));
    }
}

void MainWindow::onServerListItemClicked(QListWidgetItem *item)
{
    if (!item) return;
    QString name = item->text();
    // Find matching tab (skip index 0 = Home Dashboard)
    for (int i = 1; i < m_tabs->count(); ++i) {
        auto *stw = qobject_cast<ServerTabWidget *>(m_tabs->widget(i));
        if (stw && stw->serverName() == name) {
            m_tabs->setCurrentIndex(i);
            return;
        }
    }
}

// ---------------------------------------------------------------------------

void MainWindow::onCloneServer()
{
    if (m_manager->servers().isEmpty()) {
        QMessageBox::information(this, tr("Clone Server"),
                                 tr("No servers to clone."));
        return;
    }

    // Build a list of server names for the user to pick from
    QStringList names;
    for (const ServerConfig &s : std::as_const(m_manager->servers()))
        names << s.name;

    bool ok = false;
    QString source = QInputDialog::getItem(
        this, tr("Clone Server"), tr("Select server to clone:"),
        names, 0, false, &ok);
    if (!ok || source.isEmpty()) return;

    QString newName = QInputDialog::getText(
        this, tr("Clone Server"), tr("New server name:"),
        QLineEdit::Normal, source + QStringLiteral(" (Copy)"), &ok);
    if (!ok || newName.trimmed().isEmpty()) return;
    newName = newName.trimmed();

    // Find the source config
    const ServerConfig *src = nullptr;
    for (const ServerConfig &s : std::as_const(m_manager->servers())) {
        if (s.name == source) { src = &s; break; }
    }
    if (!src) return;

    // Deep-copy the config with new name
    ServerConfig cloned = *src;
    cloned.name = newName;

    // Validate before adding
    m_manager->servers() << cloned;
    QStringList errors = m_manager->validateAll();
    if (!errors.isEmpty()) {
        m_manager->servers().removeLast();
        QMessageBox::warning(this, tr("Validation Error"),
                             tr("Cannot clone server:\n\n%1").arg(errors.join(QLatin1Char('\n'))));
        return;
    }

    m_manager->saveConfig();
    addServerTab(m_manager->servers().last());
    rebuildSidebarList();
    m_dashboard->refresh();
    m_scheduler->startScheduler(newName);

    m_logModule->log(newName, QStringLiteral("Cloned from '%1'.").arg(source));
    m_trayManager->notify(tr("Server Cloned"),
                          tr("'%1' cloned from '%2'.").arg(newName, source));
}

// ---------------------------------------------------------------------------

void MainWindow::onRemoveServer()
{
    if (m_manager->servers().isEmpty()) {
        QMessageBox::information(this, tr("Remove Server"),
                                 tr("No servers to remove."));
        return;
    }

    QStringList names;
    for (const ServerConfig &s : std::as_const(m_manager->servers()))
        names << s.name;

    bool ok = false;
    QString target = QInputDialog::getItem(
        this, tr("Remove Server"), tr("Select server to remove:"),
        names, 0, false, &ok);
    if (!ok || target.isEmpty()) return;

    auto reply = QMessageBox::question(
        this, tr("Remove Server"),
        tr("Remove '%1' from configuration?\n\nThis will stop the server if running.\n"
           "Server files on disk are NOT deleted.").arg(target));
    if (reply != QMessageBox::Yes) return;

    m_scheduler->stopScheduler(target);

    // Remove the corresponding tab
    for (int i = 1; i < m_tabs->count(); ++i) {
        auto *stw = qobject_cast<ServerTabWidget *>(m_tabs->widget(i));
        if (stw && stw->serverName() == target) {
            m_tabs->removeTab(i);
            stw->deleteLater();
            break;
        }
    }

    m_manager->removeServer(target);
    m_manager->saveConfig();
    rebuildSidebarList();
    m_dashboard->refresh();

    m_logModule->log(target, QStringLiteral("Server removed."));
    m_trayManager->notify(tr("Server Removed"),
                          tr("'%1' has been removed.").arg(target));
}

// ---------------------------------------------------------------------------

void MainWindow::onExportServer()
{
    if (m_manager->servers().isEmpty()) {
        QMessageBox::information(this, tr("Export Server"),
                                 tr("No servers to export."));
        return;
    }

    QStringList names;
    for (const ServerConfig &s : std::as_const(m_manager->servers()))
        names << s.name;

    bool ok = false;
    QString target = QInputDialog::getItem(
        this, tr("Export Server"), tr("Select server to export:"),
        names, 0, false, &ok);
    if (!ok || target.isEmpty()) return;

    QString path = QFileDialog::getSaveFileName(
        this, tr("Export Server Config"), target + QStringLiteral(".json"),
        tr("JSON files (*.json)"));
    if (path.isEmpty()) return;

    if (m_manager->exportServerConfig(target, path)) {
        QMessageBox::information(this, tr("Export Server"),
                                 tr("'%1' exported to:\n%2").arg(target, path));
        m_logModule->log(target, QStringLiteral("Config exported to ") + path);
    } else {
        QMessageBox::warning(this, tr("Export Server"),
                             tr("Failed to export '%1'.").arg(target));
    }
}

void MainWindow::onImportServer()
{
    QString path = QFileDialog::getOpenFileName(
        this, tr("Import Server Config"), QString(),
        tr("JSON files (*.json);;All files (*)"));
    if (path.isEmpty()) return;

    QString error = m_manager->importServerConfig(path);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Import Server"),
                             tr("Cannot import server:\n\n%1").arg(error));
        return;
    }

    ServerConfig &imported = m_manager->servers().last();
    m_manager->saveConfig();
    addServerTab(imported);
    rebuildSidebarList();
    m_dashboard->refresh();
    m_scheduler->startScheduler(imported.name);

    QMessageBox::information(this, tr("Import Server"),
                             tr("'%1' imported successfully.").arg(imported.name));
    m_logModule->log(imported.name, QStringLiteral("Config imported from ") + path);
    m_trayManager->notify(tr("Server Imported"),
                          tr("'%1' imported.").arg(imported.name));
}

// ---------------------------------------------------------------------------

void MainWindow::onBroadcastCommand()
{
    if (m_manager->servers().isEmpty()) {
        QMessageBox::information(this, tr("Broadcast"),
                                 tr("No servers configured."));
        return;
    }

    bool ok = false;
    QString cmd = QInputDialog::getText(
        this, tr("Broadcast RCON Command"),
        tr("Command to send to ALL servers:"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok || cmd.trimmed().isEmpty()) return;

    QStringList results = m_manager->broadcastRconCommand(cmd.trimmed());
    QMessageBox::information(this, tr("Broadcast Results"),
                             results.join(QLatin1Char('\n')));
}

// ---------------------------------------------------------------------------

void MainWindow::updateTabStatusIndicators()
{
    if (m_manager->servers().isEmpty())
        return;

    for (int i = 1; i < m_tabs->count(); ++i) {
        auto *stw = qobject_cast<ServerTabWidget *>(m_tabs->widget(i));
        if (!stw) continue;

        QString name = stw->serverName();
        // Find the server config
        bool found = false;
        for (const ServerConfig &s : std::as_const(m_manager->servers())) {
            if (s.name == name) {
                bool online = m_manager->isServerRunning(s);
                QString indicator = online ? QStringLiteral("🟢 ") : QStringLiteral("🔴 ");
                m_tabs->setTabText(i, indicator + name);
                found = true;
                break;
            }
        }
        if (!found)
            m_tabs->setTabText(i, name);
    }
}

// ---------------------------------------------------------------------------

void MainWindow::buildLogViewerTab()
{
    auto *logWidget = new QWidget(m_tabs);
    auto *layout    = new QVBoxLayout(logWidget);

    auto *title = new QLabel(tr("Operation Log"), logWidget);
    title->setStyleSheet(QStringLiteral("font-size:16px; font-weight:bold; padding:4px;"));
    layout->addWidget(title);

    // Search/filter bar
    auto *filterEdit = new QLineEdit(logWidget);
    filterEdit->setPlaceholderText(tr("Filter log entries…"));
    layout->addWidget(filterEdit);

    auto *logView = new QTextEdit(logWidget);
    logView->setReadOnly(true);
    logView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(logView);

    // Populate with existing entries
    for (const QString &entry : m_logModule->entries())
        logView->append(entry);

    // Live updates
    connect(m_logModule, &LogModule::entryAdded, logView, &QTextEdit::append);

    // Filter log entries when search text changes
    connect(filterEdit, &QLineEdit::textChanged, this,
            [this, logView](const QString &text) {
                logView->clear();
                for (const QString &entry : m_logModule->entries()) {
                    if (text.isEmpty() || entry.contains(text, Qt::CaseInsensitive))
                        logView->append(entry);
                }
            });

    m_tabs->addTab(logWidget, tr("📝 Log"));
}

// ---------------------------------------------------------------------------

void MainWindow::toggleDarkMode()
{
    QSettings settings;
    bool isDark = settings.value(QStringLiteral("darkMode"), false).toBool();
    isDark = !isDark;
    settings.setValue(QStringLiteral("darkMode"), isDark);

    if (isDark) {
        QPalette dark;
        dark.setColor(QPalette::Window,          QColor(53, 53, 53));
        dark.setColor(QPalette::WindowText,      Qt::white);
        dark.setColor(QPalette::Base,            QColor(35, 35, 35));
        dark.setColor(QPalette::AlternateBase,   QColor(53, 53, 53));
        dark.setColor(QPalette::ToolTipBase,     Qt::white);
        dark.setColor(QPalette::ToolTipText,     Qt::white);
        dark.setColor(QPalette::Text,            Qt::white);
        dark.setColor(QPalette::Button,          QColor(53, 53, 53));
        dark.setColor(QPalette::ButtonText,      Qt::white);
        dark.setColor(QPalette::BrightText,      Qt::red);
        dark.setColor(QPalette::Link,            QColor(42, 130, 218));
        dark.setColor(QPalette::Highlight,       QColor(42, 130, 218));
        dark.setColor(QPalette::HighlightedText, Qt::black);
        qApp->setPalette(dark);
    } else {
        qApp->setPalette(qApp->style()->standardPalette());
    }
}

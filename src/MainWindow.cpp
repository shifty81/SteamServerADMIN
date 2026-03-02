#include "MainWindow.hpp"
#include "HomeDashboard.hpp"
#include "ServerTabWidget.hpp"
#include "SchedulerModule.hpp"

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

static const QString kConfigFile = QStringLiteral("servers.json");

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(tr("SSA – Steam Server ADMIN"));
    resize(1600, 900);

    // ---- Backend ----
    m_manager = new ServerManager(kConfigFile, this);
    m_manager->loadConfig();

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
    auto *syncModsBtn = new QPushButton(tr("⟳  Sync Mods (All)"), sidebar);
    auto *syncCfgBtn  = new QPushButton(tr("⟳  Sync Configs (All)"), sidebar);
    sideLayout->addWidget(addBtn);
    sideLayout->addWidget(syncModsBtn);
    sideLayout->addWidget(syncCfgBtn);

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

    // Start scheduled tasks (automatic backups and restarts)
    m_scheduler = new SchedulerModule(m_manager, this);
    m_scheduler->startAll();

    // ---- Connections ----
    connect(addBtn,      &QPushButton::clicked, this, &MainWindow::onAddServer);
    connect(syncModsBtn, &QPushButton::clicked, this, &MainWindow::onSyncMods);
    connect(syncCfgBtn,  &QPushButton::clicked, this, &MainWindow::onSyncConfigs);
    connect(m_searchBox, &QLineEdit::textChanged, this, &MainWindow::onSearchChanged);
    connect(m_serverList, &QListWidget::itemClicked,
            this, &MainWindow::onServerListItemClicked);
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

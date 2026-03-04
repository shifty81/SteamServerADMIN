#include "ServerTabWidget.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QLineEdit>
#include <QTableWidget>
#include <QListWidget>
#include <QHeaderView>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTimer>
#include <QScrollBar>
#include <QGroupBox>
#include <QKeyEvent>

// ---------------------------------------------------------------------------

ServerTabWidget::ServerTabWidget(ServerManager *manager,
                                 ServerConfig  &server,
                                 QWidget       *parent)
    : QWidget(parent), m_manager(manager), m_server(server)
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    auto *tabs = new QTabWidget(this);
    outerLayout->addWidget(tabs);

    buildOverviewTab(tabs);
    buildConfigTab(tabs);
    buildModsTab(tabs);
    buildBackupsTab(tabs);
    buildConsoleTab(tabs);

    // Connect log messages from manager to console
    connect(m_manager, &ServerManager::logMessage,
            this, [this](const QString &serverName, const QString &msg) {
                if (serverName == m_server.name)
                    appendConsole(msg);
            });

    // Periodic status refresh
    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &ServerTabWidget::refreshStatus);
    m_statusTimer->start(5000);
}

const QString &ServerTabWidget::serverName() const { return m_server.name; }

// ---------------------------------------------------------------------------
// Tab builders
// ---------------------------------------------------------------------------

void ServerTabWidget::buildOverviewTab(QTabWidget *tabs)
{
    auto *w      = new QWidget(tabs);
    auto *layout = new QVBoxLayout(w);

    auto *statusBox = new QGroupBox(tr("Status"), w);
    auto *sbLayout  = new QHBoxLayout(statusBox);

    m_statusLight  = new QLabel(QStringLiteral("⬜"), statusBox);
    m_statusLight->setStyleSheet(QStringLiteral("font-size:24px;"));
    m_playersLabel = new QLabel(tr("Players: –"), statusBox);
    m_playersLabel->setStyleSheet(QStringLiteral("font-size:14px;"));
    m_uptimeLabel  = new QLabel(tr("Uptime: –"), statusBox);
    m_uptimeLabel->setStyleSheet(QStringLiteral("font-size:14px;"));

    sbLayout->addWidget(m_statusLight);
    sbLayout->addWidget(m_playersLabel);
    sbLayout->addWidget(m_uptimeLabel);
    sbLayout->addStretch();
    layout->addWidget(statusBox);

    auto *actBox = new QGroupBox(tr("Server Control"), w);
    auto *actLayout = new QHBoxLayout(actBox);

    auto *startBtn   = new QPushButton(tr("▶  Start"),   actBox);
    auto *stopBtn    = new QPushButton(tr("■  Stop"),    actBox);
    auto *restartBtn = new QPushButton(tr("↺  Restart"), actBox);
    auto *deployBtn  = new QPushButton(tr("⬇  Deploy / Update"), actBox);

    actLayout->addWidget(startBtn);
    actLayout->addWidget(stopBtn);
    actLayout->addWidget(restartBtn);
    actLayout->addWidget(deployBtn);
    actLayout->addStretch();
    layout->addWidget(actBox);

    layout->addStretch();

    connect(startBtn,   &QPushButton::clicked, this, [this]() { m_manager->startServer(m_server);   });
    connect(stopBtn,    &QPushButton::clicked, this, [this]() { m_manager->stopServer(m_server);    });
    connect(restartBtn, &QPushButton::clicked, this, [this]() { m_manager->restartServer(m_server); });
    connect(deployBtn,  &QPushButton::clicked, this, &ServerTabWidget::onDeployServer);

    tabs->addTab(w, tr("Overview"));
}

void ServerTabWidget::buildConfigTab(QTabWidget *tabs)
{
    auto *w      = new QWidget(tabs);
    auto *layout = new QVBoxLayout(w);

    // Try the server's primary config file
    m_configPath = m_server.dir + QStringLiteral("/Configs/GameUserSettings.ini");

    auto *pathLabel = new QLabel(tr("Editing: %1").arg(m_configPath), w);
    pathLabel->setWordWrap(true);
    layout->addWidget(pathLabel);

    m_configEditor = new QTextEdit(w);
    m_configEditor->setFont(QFont(QStringLiteral("Monospace"), 9));

    QFile f(m_configPath);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_configEditor->setPlainText(QString::fromUtf8(f.readAll()));
        f.close();
    } else {
        m_configEditor->setPlaceholderText(
            tr("Config file not found.\nPath: %1").arg(m_configPath));
    }
    layout->addWidget(m_configEditor);

    auto *btnRow  = new QHBoxLayout();
    auto *saveBtn = new QPushButton(tr("Save Config"), w);
    auto *openBtn = new QPushButton(tr("Open Different File…"), w);
    btnRow->addWidget(saveBtn);
    btnRow->addWidget(openBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    connect(saveBtn, &QPushButton::clicked, this, &ServerTabWidget::onSaveConfig);
    connect(openBtn, &QPushButton::clicked, this, [this, pathLabel]() {
        QString path = QFileDialog::getOpenFileName(
            this, tr("Open Config File"), m_server.dir,
            tr("Config files (*.ini *.cfg *.json *.txt);;All files (*)"));
        if (!path.isEmpty()) {
            m_configPath = path;
            pathLabel->setText(tr("Editing: %1").arg(m_configPath));
            QFile f2(m_configPath);
            if (f2.open(QIODevice::ReadOnly | QIODevice::Text)) {
                m_configEditor->setPlainText(QString::fromUtf8(f2.readAll()));
                f2.close();
            }
        }
    });

    tabs->addTab(w, tr("Config"));
}

void ServerTabWidget::buildModsTab(QTabWidget *tabs)
{
    auto *w      = new QWidget(tabs);
    auto *layout = new QVBoxLayout(w);

    m_modTable = new QTableWidget(0, 3, w);
    m_modTable->setHorizontalHeaderLabels({ tr("Workshop Mod ID"), tr("Status"), tr("Enabled") });
    m_modTable->horizontalHeader()->setStretchLastSection(true);
    m_modTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_modTable);

    populateModTable();

    auto *btnRow      = new QHBoxLayout();
    auto *addModBtn   = new QPushButton(tr("Add Mod…"),       w);
    auto *removeModBtn= new QPushButton(tr("Remove Selected"),w);
    auto *updateBtn   = new QPushButton(tr("Update All Mods"),w);
    btnRow->addWidget(addModBtn);
    btnRow->addWidget(removeModBtn);
    btnRow->addWidget(updateBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    connect(addModBtn,    &QPushButton::clicked, this, &ServerTabWidget::onAddMod);
    connect(removeModBtn, &QPushButton::clicked, this, &ServerTabWidget::onRemoveMod);
    connect(updateBtn,    &QPushButton::clicked, this, &ServerTabWidget::onUpdateMods);

    tabs->addTab(w, tr("Mods"));
}

void ServerTabWidget::buildBackupsTab(QTabWidget *tabs)
{
    auto *w      = new QWidget(tabs);
    auto *layout = new QVBoxLayout(w);

    auto *label = new QLabel(tr("Versioned snapshots (newest first):"), w);
    layout->addWidget(label);

    m_backupList = new QListWidget(w);
    layout->addWidget(m_backupList);

    populateBackupList();

    auto *btnRow     = new QHBoxLayout();
    auto *snapshotBtn= new QPushButton(tr("Take Snapshot Now"),    w);
    auto *restoreBtn = new QPushButton(tr("Restore Selected…"),    w);
    auto *refreshBtn = new QPushButton(tr("Refresh List"),          w);
    btnRow->addWidget(snapshotBtn);
    btnRow->addWidget(restoreBtn);
    btnRow->addWidget(refreshBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    connect(snapshotBtn, &QPushButton::clicked, this, &ServerTabWidget::onTakeSnapshot);
    connect(restoreBtn,  &QPushButton::clicked, this, &ServerTabWidget::onRestoreSnapshot);
    connect(refreshBtn,  &QPushButton::clicked, this, [this]() { populateBackupList(); });

    tabs->addTab(w, tr("Backups"));
}

void ServerTabWidget::buildConsoleTab(QTabWidget *tabs)
{
    auto *w      = new QWidget(tabs);
    auto *layout = new QVBoxLayout(w);

    m_consoleOutput = new QTextEdit(w);
    m_consoleOutput->setReadOnly(true);
    m_consoleOutput->setFont(QFont(QStringLiteral("Monospace"), 9));
    m_consoleOutput->setStyleSheet(
        QStringLiteral("background:#1e1e1e; color:#d4d4d4;"));
    layout->addWidget(m_consoleOutput);

    auto *inputRow = new QHBoxLayout();
    auto *lineEdit = new QLineEdit(w);
    lineEdit->setPlaceholderText(tr("Enter RCON command…"));
    m_consoleInput = lineEdit;

    // Install event filter for command history (Up/Down arrow keys)
    lineEdit->installEventFilter(this);

    auto *sendBtn = new QPushButton(tr("Send"), w);
    inputRow->addWidget(lineEdit);
    inputRow->addWidget(sendBtn);
    layout->addLayout(inputRow);

    connect(sendBtn,  &QPushButton::clicked, this, &ServerTabWidget::onSendCommand);
    connect(lineEdit, &QLineEdit::returnPressed, this, &ServerTabWidget::onSendCommand);

    tabs->addTab(w, tr("Console"));
}

// ---------------------------------------------------------------------------
// Slot implementations
// ---------------------------------------------------------------------------

void ServerTabWidget::onSendCommand()
{
    auto *lineEdit = qobject_cast<QLineEdit *>(m_consoleInput);
    if (!lineEdit || lineEdit->text().trimmed().isEmpty()) return;

    QString cmd = lineEdit->text().trimmed();
    lineEdit->clear();
    appendConsole(QStringLiteral("> ") + cmd);

    // Add to command history (avoid consecutive duplicates)
    if (m_commandHistory.isEmpty() || m_commandHistory.last() != cmd)
        m_commandHistory << cmd;
    m_historyIndex = -1;

    QString resp = m_manager->sendRconCommand(m_server, cmd);
    if (!resp.isEmpty())
        appendConsole(resp);
}

void ServerTabWidget::onAddMod()
{
    bool ok = false;
    int modId = QInputDialog::getInt(this, tr("Add Mod"),
                                     tr("Enter Steam Workshop Mod ID:"),
                                     0, 1, INT_MAX, 1, &ok);
    if (!ok) return;

    if (m_server.mods.contains(modId)) {
        QMessageBox::information(this, tr("Add Mod"),
                                 tr("Mod %1 is already in the list.").arg(modId));
        return;
    }

    m_server.mods << modId;
    m_manager->saveConfig();
    populateModTable();
}

void ServerTabWidget::onRemoveMod()
{
    QList<QTableWidgetItem *> selected = m_modTable->selectedItems();
    if (selected.isEmpty()) return;

    int row = m_modTable->row(selected.first());
    bool ok = false;
    int modId = m_modTable->item(row, 0)->text().toInt(&ok);
    if (!ok) return;

    m_server.mods.removeAll(modId);
    m_manager->saveConfig();
    populateModTable();
}

void ServerTabWidget::onUpdateMods()
{
    appendConsole(tr("[SSA] Updating mods via SteamCMD…"));
    m_manager->updateMods(m_server);
}

void ServerTabWidget::onTakeSnapshot()
{
    appendConsole(tr("[SSA] Taking snapshot…"));
    QString ts = m_manager->takeSnapshot(m_server);
    if (!ts.isEmpty()) {
        populateBackupList();
        appendConsole(tr("[SSA] Snapshot: ") + ts);
    }
}

void ServerTabWidget::onRestoreSnapshot()
{
    QListWidgetItem *item = m_backupList->currentItem();
    if (!item) {
        QMessageBox::information(this, tr("Restore"),
                                 tr("Please select a snapshot from the list."));
        return;
    }

    QString zip = item->data(Qt::UserRole).toString();
    auto reply  = QMessageBox::question(
        this, tr("Restore Snapshot"),
        tr("Restore from:\n%1\n\nThis will overwrite current server files. Continue?").arg(zip));
    if (reply != QMessageBox::Yes) return;

    bool ok = m_manager->restoreSnapshot(zip, m_server);
    QMessageBox::information(this, tr("Restore"),
                             ok ? tr("Restore complete.") : tr("Restore failed."));
}

void ServerTabWidget::onSaveConfig()
{
    QDir().mkpath(QFileInfo(m_configPath).absolutePath());
    QFile f(m_configPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Save Config"),
                             tr("Cannot write to:\n%1").arg(m_configPath));
        return;
    }
    f.write(m_configEditor->toPlainText().toUtf8());
    appendConsole(tr("[SSA] Config saved: ") + m_configPath);
}

void ServerTabWidget::onDeployServer()
{
    auto reply = QMessageBox::question(
        this, tr("Deploy / Update Server"),
        tr("Run SteamCMD to install or update '%1'?\n(AppID %2)")
            .arg(m_server.name).arg(m_server.appid));
    if (reply != QMessageBox::Yes) return;

    appendConsole(tr("[SSA] Starting SteamCMD deployment…"));
    m_manager->deployServer(m_server);
}

// ---------------------------------------------------------------------------
// Event filter (RCON command history with Up/Down arrows)
// ---------------------------------------------------------------------------

bool ServerTabWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_consoleInput && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        auto *lineEdit = qobject_cast<QLineEdit *>(m_consoleInput);
        if (!lineEdit || m_commandHistory.isEmpty())
            return QWidget::eventFilter(obj, event);

        if (keyEvent->key() == Qt::Key_Up) {
            if (m_historyIndex < 0)
                m_historyIndex = m_commandHistory.size() - 1;
            else if (m_historyIndex > 0)
                --m_historyIndex;
            lineEdit->setText(m_commandHistory.at(m_historyIndex));
            return true;
        }
        if (keyEvent->key() == Qt::Key_Down) {
            if (m_historyIndex < 0)
                return QWidget::eventFilter(obj, event);
            if (m_historyIndex < m_commandHistory.size() - 1) {
                ++m_historyIndex;
                lineEdit->setText(m_commandHistory.at(m_historyIndex));
            } else {
                m_historyIndex = -1;
                lineEdit->clear();
            }
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void ServerTabWidget::refreshStatus()
{
    if (!m_statusLight) return;

    bool online  = m_manager->isServerRunning(m_server);
    int  players = online ? m_manager->getPlayerCount(m_server) : -1;

    if (!online) {
        m_statusLight->setText(QStringLiteral("🔴"));
        m_playersLabel->setText(tr("Players: –  (Offline)"));
    } else if (players > 0) {
        m_statusLight->setText(QStringLiteral("🟢"));
        m_playersLabel->setText(tr("Players: %1  (Online)").arg(players));
    } else {
        m_statusLight->setText(QStringLiteral("🟡"));
        m_playersLabel->setText(tr("Players: 0  (Idle)"));
    }

    // Update uptime
    if (m_uptimeLabel) {
        qint64 secs = m_manager->serverUptimeSeconds(m_server.name);
        if (secs < 0) {
            m_uptimeLabel->setText(tr("Uptime: –"));
        } else {
            int days  = static_cast<int>(secs / 86400);
            int hours = static_cast<int>((secs % 86400) / 3600);
            int mins  = static_cast<int>((secs % 3600) / 60);
            if (days > 0)
                m_uptimeLabel->setText(tr("Uptime: %1d %2h %3m").arg(days).arg(hours).arg(mins));
            else if (hours > 0)
                m_uptimeLabel->setText(tr("Uptime: %1h %2m").arg(hours).arg(mins));
            else
                m_uptimeLabel->setText(tr("Uptime: %1m").arg(mins));
        }
    }
}

void ServerTabWidget::appendConsole(const QString &text)
{
    if (!m_consoleOutput) return;
    m_consoleOutput->append(text);
    m_consoleOutput->verticalScrollBar()->setValue(
        m_consoleOutput->verticalScrollBar()->maximum());
}

void ServerTabWidget::populateModTable()
{
    m_modTable->setRowCount(0);
    for (int modId : std::as_const(m_server.mods)) {
        int row = m_modTable->rowCount();
        m_modTable->insertRow(row);
        m_modTable->setItem(row, 0, new QTableWidgetItem(QString::number(modId)));

        bool disabled = m_server.disabledMods.contains(modId);
        m_modTable->setItem(row, 1, new QTableWidgetItem(
            disabled ? tr("Disabled") : tr("Installed")));

        auto *toggleBtn = new QPushButton(disabled ? tr("Enable") : tr("Disable"));
        connect(toggleBtn, &QPushButton::clicked, this, [this, modId]() {
            if (m_server.disabledMods.contains(modId))
                m_server.disabledMods.removeAll(modId);
            else
                m_server.disabledMods << modId;
            m_manager->saveConfig();
            populateModTable();
        });
        m_modTable->setCellWidget(row, 2, toggleBtn);
    }
}

void ServerTabWidget::populateBackupList()
{
    m_backupList->clear();
    QStringList snapshots = m_manager->listSnapshots(m_server);
    for (const QString &path : std::as_const(snapshots)) {
        QFileInfo fi(path);
        QString sizeStr;
        qint64 bytes = fi.size();
        if (bytes >= 1024 * 1024)
            sizeStr = QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
        else if (bytes >= 1024)
            sizeStr = QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
        else
            sizeStr = QStringLiteral("%1 B").arg(bytes);

        QString label = QStringLiteral("%1  (%2)").arg(fi.fileName(), sizeStr);
        auto *item = new QListWidgetItem(label, m_backupList);
        item->setData(Qt::UserRole, path);
    }
}

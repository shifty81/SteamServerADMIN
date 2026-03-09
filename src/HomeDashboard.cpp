#include "HomeDashboard.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QFrame>
#include <QScrollArea>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>

HomeDashboard::HomeDashboard(ServerManager *manager, QWidget *parent)
    : QWidget(parent), m_manager(manager)
{
    auto *outerLayout = new QVBoxLayout(this);

    // Title
    auto *title = new QLabel(tr("Server Health Dashboard"), this);
    title->setStyleSheet(QStringLiteral(
        "font-size:20px; font-weight:bold; padding:8px;"));
    outerLayout->addWidget(title);

    // Cluster summary row
    auto *summaryLayout = new QHBoxLayout();
    summaryLayout->setSpacing(16);
    m_totalLabel   = new QLabel(tr("Total: 0"),   this);
    m_onlineLabel  = new QLabel(tr("Online: 0"),  this);
    m_offlineLabel = new QLabel(tr("Offline: 0"), this);
    for (auto *l : { m_totalLabel, m_onlineLabel, m_offlineLabel })
        l->setStyleSheet(QStringLiteral("font-size:14px; padding:4px 8px;"));
    m_onlineLabel->setStyleSheet(m_onlineLabel->styleSheet()
        + QStringLiteral(" color: green;"));
    m_offlineLabel->setStyleSheet(m_offlineLabel->styleSheet()
        + QStringLiteral(" color: red;"));
    summaryLayout->addWidget(m_totalLabel);
    summaryLayout->addWidget(m_onlineLabel);
    summaryLayout->addWidget(m_offlineLabel);
    summaryLayout->addStretch();
    outerLayout->addLayout(summaryLayout);

    // Scrollable container for server badge cards
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    m_rowContainer = new QWidget(scroll);
    scroll->setWidget(m_rowContainer);
    outerLayout->addWidget(scroll);

    refresh();

    // Poll status every 5 seconds
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &HomeDashboard::updateStatus);
    timer->start(5000);
}

void HomeDashboard::refresh()
{
    // Remove old layout
    QLayout *old = m_rowContainer->layout();
    if (old) {
        QLayoutItem *item;
        while ((item = old->takeAt(0)) != nullptr) {
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }
        delete old;
    }
    m_badges.clear();

    auto *gridLayout = new QGridLayout(m_rowContainer);
    gridLayout->setSpacing(12);

    int col = 0;
    int row = 0;
    static constexpr int kCardsPerRow = 3;

    for (ServerConfig &server : m_manager->servers()) {
        // --- Badge card frame ---
        auto *card = new QFrame(m_rowContainer);
        card->setFrameShape(QFrame::StyledPanel);
        card->setStyleSheet(QStringLiteral(
            "QFrame { border:1px solid #888; border-radius:8px; padding:10px;"
            " background:#2d2d2d; }"
            "QLabel { color:#eee; border:none; background:transparent; }"));
        card->setContextMenuPolicy(Qt::CustomContextMenu);

        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setSpacing(6);

        // Row 1: status light + server name
        auto *topRow = new QHBoxLayout();
        auto *statusLight = new QLabel(QStringLiteral("⬜"), card);
        statusLight->setStyleSheet(QStringLiteral("font-size:22px;"));
        auto *nameLabel = new QLabel(server.name, card);
        nameLabel->setStyleSheet(QStringLiteral("font-size:15px; font-weight:bold;"));
        topRow->addWidget(statusLight);
        topRow->addWidget(nameLabel);
        topRow->addStretch();

        // Group label (shown only if group is set)
        auto *groupLabel = new QLabel(card);
        if (!server.group.trimmed().isEmpty()) {
            groupLabel->setText(server.group);
            groupLabel->setStyleSheet(QStringLiteral(
                "font-size:10px; padding:1px 6px; border-radius:3px;"
                " background:#555; color:#ddd;"));
        }
        topRow->addWidget(groupLabel);

        cardLayout->addLayout(topRow);

        // Row 2: players + uptime
        auto *infoRow = new QHBoxLayout();
        auto *playersLabel = new QLabel(QStringLiteral("Players: –"), card);
        playersLabel->setStyleSheet(QStringLiteral("font-size:12px;"));
        auto *uptimeLabel  = new QLabel(QStringLiteral("Uptime: –"), card);
        uptimeLabel->setStyleSheet(QStringLiteral("font-size:12px;"));
        infoRow->addWidget(playersLabel);
        infoRow->addWidget(uptimeLabel);
        infoRow->addStretch();
        cardLayout->addLayout(infoRow);

        // Row 3: pending update badges
        auto *badgeRow = new QHBoxLayout();
        auto *updateBadge = new QLabel(card);
        updateBadge->setStyleSheet(QStringLiteral(
            "font-size:11px; padding:2px 6px; border-radius:4px;"));
        updateBadge->hide();
        auto *modUpdateBadge = new QLabel(card);
        modUpdateBadge->setStyleSheet(QStringLiteral(
            "font-size:11px; padding:2px 6px; border-radius:4px;"));
        modUpdateBadge->hide();
        badgeRow->addWidget(updateBadge);
        badgeRow->addWidget(modUpdateBadge);
        badgeRow->addStretch();
        cardLayout->addLayout(badgeRow);

        // Row 4: quick-action buttons
        auto *btnRow = new QHBoxLayout();
        auto *startBtn   = new QPushButton(tr("▶ Start"),   card);
        auto *stopBtn    = new QPushButton(tr("■ Stop"),    card);
        auto *restartBtn = new QPushButton(tr("↺ Restart"), card);
        auto *backupBtn  = new QPushButton(tr("📦 Backup"), card);
        for (auto *btn : { startBtn, stopBtn, restartBtn, backupBtn })
            btn->setFixedHeight(28);
        btnRow->addWidget(startBtn);
        btnRow->addWidget(stopBtn);
        btnRow->addWidget(restartBtn);
        btnRow->addWidget(backupBtn);
        cardLayout->addLayout(btnRow);

        gridLayout->addWidget(card, row, col);
        ++col;
        if (col >= kCardsPerRow) { col = 0; ++row; }

        // --- Connect buttons (capture by server name) ---
        QString sname = server.name;
        auto findServer = [this, sname]() -> ServerConfig * {
            for (ServerConfig &s : m_manager->servers())
                if (s.name == sname) return &s;
            return nullptr;
        };
        connect(startBtn,   &QPushButton::clicked, this, [this, findServer]() {
            if (auto *s = findServer()) m_manager->startServer(*s); });
        connect(stopBtn,    &QPushButton::clicked, this, [this, findServer]() {
            if (auto *s = findServer()) m_manager->stopServer(*s); });
        connect(restartBtn, &QPushButton::clicked, this, [this, findServer]() {
            if (auto *s = findServer()) m_manager->restartServer(*s); });
        connect(backupBtn,  &QPushButton::clicked, this, [this, findServer]() {
            if (auto *s = findServer()) m_manager->takeSnapshot(*s); });

        // --- Right-click context menu ---
        connect(card, &QWidget::customContextMenuRequested, this,
                [this, findServer](const QPoint &pos) {
            auto *s = findServer();
            if (!s) return;

            QMenu menu;
            QAction *saveAction    = menu.addAction(tr("💾  Save Config"));
            QAction *restartAction = menu.addAction(tr("↺  Restart Server"));

            if (s->restartWarningMinutes > 0 && m_manager->isServerRunning(*s)) {
                restartAction->setText(tr("↺  Restart (warn %1 min)")
                                        .arg(s->restartWarningMinutes));
            }

            QAction *chosen = menu.exec(
                qobject_cast<QWidget *>(sender())->mapToGlobal(pos));

            if (chosen == saveAction) {
                if (m_manager->saveConfig())
                    QMessageBox::information(this, tr("Save"),
                                             tr("Configuration saved."));
                else
                    QMessageBox::warning(this, tr("Save"),
                                         tr("Failed to save configuration."));
            } else if (chosen == restartAction) {
                if (s->restartWarningMinutes > 0 && m_manager->isServerRunning(*s)) {
                    // Send in-game warnings, then restart after delay
                    int warnMin = s->restartWarningMinutes;
                    m_manager->sendRestartWarning(*s, warnMin);

                    QString name = s->name;
                    // Schedule the actual restart after the warning period
                    QTimer::singleShot(warnMin * 60 * 1000, this, [this, name]() {
                        for (ServerConfig &srv : m_manager->servers()) {
                            if (srv.name == name) {
                                m_manager->restartServer(srv);
                                break;
                            }
                        }
                    });
                } else {
                    m_manager->restartServer(*s);
                }
            }
        });

        ServerBadge b;
        b.statusLight    = statusLight;
        b.nameLabel      = nameLabel;
        b.groupLabel     = groupLabel;
        b.playersLabel   = playersLabel;
        b.uptimeLabel    = uptimeLabel;
        b.updateBadge    = updateBadge;
        b.modUpdateBadge = modUpdateBadge;
        b.card           = card;
        m_badges << b;
    }

    // Fill remaining columns so cards don't stretch
    if (col > 0) {
        for (; col < kCardsPerRow; ++col)
            gridLayout->addWidget(new QWidget(m_rowContainer), row, col);
    }
    gridLayout->setRowStretch(row + 1, 1);
}

void HomeDashboard::updateStatus()
{
    const QList<ServerConfig> &srvs = m_manager->servers();
    int onlineCount = 0;

    for (int i = 0; i < m_badges.size() && i < srvs.size(); ++i) {
        const ServerConfig &s = srvs.at(i);
        bool online = m_manager->isServerRunning(s);
        int players = online ? m_manager->getPlayerCount(s) : -1;

        if (online) ++onlineCount;

        // --- Status light ---
        QString light;
        QString tooltip;
        if (!online) {
            light   = QStringLiteral("🔴");
            tooltip = tr("Offline");
        } else if (players > 0) {
            light   = QStringLiteral("🟢");
            tooltip = tr("Online – %1 player(s)").arg(players);
        } else {
            light   = QStringLiteral("🟡");
            tooltip = tr("Online – idle");
        }

        m_badges[i].statusLight->setText(light);
        m_badges[i].statusLight->setToolTip(tooltip);

        // --- Player count (with max if set) ---
        if (players >= 0) {
            if (s.maxPlayers > 0)
                m_badges[i].playersLabel->setText(
                    tr("Players: %1 / %2").arg(players).arg(s.maxPlayers));
            else
                m_badges[i].playersLabel->setText(
                    tr("Players: %1").arg(players));
        } else {
            m_badges[i].playersLabel->setText(QStringLiteral("Players: –"));
        }

        // --- Uptime ---
        qint64 secs = m_manager->serverUptimeSeconds(s.name);
        if (secs < 0) {
            m_badges[i].uptimeLabel->setText(tr("Uptime: –"));
        } else {
            int days  = static_cast<int>(secs / 86400);
            int hours = static_cast<int>((secs % 86400) / 3600);
            int mins  = static_cast<int>((secs % 3600) / 60);
            if (days > 0)
                m_badges[i].uptimeLabel->setText(
                    tr("Uptime: %1d %2h %3m").arg(days).arg(hours).arg(mins));
            else if (hours > 0)
                m_badges[i].uptimeLabel->setText(
                    tr("Uptime: %1h %2m").arg(hours).arg(mins));
            else
                m_badges[i].uptimeLabel->setText(tr("Uptime: %1m").arg(mins));
        }

        // --- Pending update badges ---
        if (m_manager->hasPendingUpdate(s.name)) {
            m_badges[i].updateBadge->setText(tr("⬆ Update Available"));
            m_badges[i].updateBadge->setStyleSheet(
                QStringLiteral("font-size:11px; padding:2px 6px; border-radius:4px;"
                               " background:#e67e22; color:white; border:none;"));
            m_badges[i].updateBadge->show();
        } else {
            m_badges[i].updateBadge->hide();
        }

        if (m_manager->hasPendingModUpdate(s.name)) {
            m_badges[i].modUpdateBadge->setText(tr("🔧 Mod Update Available"));
            m_badges[i].modUpdateBadge->setStyleSheet(
                QStringLiteral("font-size:11px; padding:2px 6px; border-radius:4px;"
                               " background:#3498db; color:white; border:none;"));
            m_badges[i].modUpdateBadge->show();
        } else {
            m_badges[i].modUpdateBadge->hide();
        }
    }

    // Update cluster summary
    int total   = srvs.size();
    int offline = total - onlineCount;
    if (m_totalLabel)   m_totalLabel->setText(tr("Total: %1").arg(total));
    if (m_onlineLabel)  m_onlineLabel->setText(tr("Online: %1").arg(onlineCount));
    if (m_offlineLabel) m_offlineLabel->setText(tr("Offline: %1").arg(offline));
}

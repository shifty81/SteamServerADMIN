#include "HomeDashboard.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QFrame>
#include <QScrollArea>

HomeDashboard::HomeDashboard(ServerManager *manager, QWidget *parent)
    : QWidget(parent), m_manager(manager)
{
    auto *outerLayout = new QVBoxLayout(this);

    // Title
    auto *title = new QLabel(tr("Server Health Dashboard"), this);
    title->setStyleSheet(QStringLiteral(
        "font-size:20px; font-weight:bold; padding:8px;"));
    outerLayout->addWidget(title);

    // Scrollable container for server rows
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
    m_rows.clear();

    auto *layout = new QVBoxLayout(m_rowContainer);
    layout->setSpacing(4);

    // Header row
    {
        auto *hdr = new QHBoxLayout();
        auto *h1 = new QLabel(tr("Status"),  m_rowContainer);
        auto *h2 = new QLabel(tr("Server"),  m_rowContainer);
        auto *h3 = new QLabel(tr("Players"), m_rowContainer);
        auto *h4 = new QLabel(tr("Actions"), m_rowContainer);
        for (auto *l : { h1, h2, h3, h4 })
            l->setStyleSheet(QStringLiteral("font-weight:bold;"));
        h1->setFixedWidth(80);
        h2->setMinimumWidth(200);
        h3->setFixedWidth(100);
        hdr->addWidget(h1);
        hdr->addWidget(h2);
        hdr->addWidget(h3);
        hdr->addWidget(h4);
        hdr->addStretch();
        layout->addLayout(hdr);

        auto *line = new QFrame(m_rowContainer);
        line->setFrameShape(QFrame::HLine);
        layout->addWidget(line);
    }

    for (ServerConfig &server : m_manager->servers()) {
        auto *row = new QHBoxLayout();

        auto *statusLight = new QLabel(QStringLiteral("⬜"), m_rowContainer);
        statusLight->setFixedWidth(80);
        statusLight->setToolTip(tr("Server status"));

        auto *nameLabel = new QLabel(server.name, m_rowContainer);
        nameLabel->setMinimumWidth(200);

        auto *playersLabel = new QLabel(QStringLiteral("–"), m_rowContainer);
        playersLabel->setFixedWidth(100);

        auto *startBtn   = new QPushButton(tr("Start"),   m_rowContainer);
        auto *stopBtn    = new QPushButton(tr("Stop"),    m_rowContainer);
        auto *restartBtn = new QPushButton(tr("Restart"), m_rowContainer);
        auto *backupBtn  = new QPushButton(tr("Backup"),  m_rowContainer);

        for (auto *btn : { startBtn, stopBtn, restartBtn, backupBtn })
            btn->setFixedWidth(70);

        row->addWidget(statusLight);
        row->addWidget(nameLabel);
        row->addWidget(playersLabel);
        row->addWidget(startBtn);
        row->addWidget(stopBtn);
        row->addWidget(restartBtn);
        row->addWidget(backupBtn);
        row->addStretch();
        layout->addLayout(row);

        // Capture by name so the lambda remains valid even if QList reallocates.
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

        ServerRow r;
        r.statusLight  = statusLight;
        r.nameLabel    = nameLabel;
        r.playersLabel = playersLabel;
        m_rows << r;
    }

    layout->addStretch();
}

void HomeDashboard::updateStatus()
{
    const QList<ServerConfig> &srvs = m_manager->servers();
    for (int i = 0; i < m_rows.size() && i < srvs.size(); ++i) {
        const ServerConfig &s = srvs.at(i);
        bool online = m_manager->isServerRunning(s);
        int players = online ? m_manager->getPlayerCount(s) : -1;

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

        m_rows[i].statusLight->setText(light);
        m_rows[i].statusLight->setToolTip(tooltip);
        m_rows[i].playersLabel->setText(players >= 0
                                        ? QString::number(players)
                                        : QStringLiteral("–"));
    }
}

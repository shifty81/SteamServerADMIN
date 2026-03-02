#include "TrayManager.hpp"

#include <QMainWindow>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QStyle>

TrayManager::TrayManager(QMainWindow *mainWindow, QObject *parent)
    : QObject(parent), m_mainWindow(mainWindow)
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(qApp->style()->standardIcon(QStyle::SP_ComputerIcon));
    m_trayIcon->setToolTip(QStringLiteral("SSA – Steam Server ADMIN"));

    // Context menu
    m_menu = new QMenu();
    QAction *showAction = m_menu->addAction(tr("Show"));
    m_menu->addSeparator();
    QAction *quitAction = m_menu->addAction(tr("Quit"));

    m_trayIcon->setContextMenu(m_menu);

    connect(showAction, &QAction::triggered, this, [this]() {
        if (m_mainWindow) {
            m_mainWindow->show();
            m_mainWindow->raise();
            m_mainWindow->activateWindow();
        }
    });
    connect(quitAction, &QAction::triggered, this, &TrayManager::quitRequested);

    // Double-click on tray icon restores the window
    connect(m_trayIcon, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::DoubleClick && m_mainWindow) {
                    m_mainWindow->show();
                    m_mainWindow->raise();
                    m_mainWindow->activateWindow();
                }
            });

    m_trayIcon->show();
}

void TrayManager::notify(const QString &title, const QString &message,
                         QSystemTrayIcon::MessageIcon icon, int durationMs)
{
    if (m_trayIcon)
        m_trayIcon->showMessage(title, message, icon, durationMs);
}

bool TrayManager::isAvailable() const
{
    return m_trayIcon != nullptr;
}

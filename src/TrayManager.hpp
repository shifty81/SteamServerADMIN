#pragma once

#include <QObject>
#include <QSystemTrayIcon>

class QMenu;
class QAction;
class QMainWindow;

/**
 * @brief System-tray integration for SSA.
 *
 * Shows a tray icon with a context menu (Show / Quit), and exposes a
 * convenience method to display balloon notifications for important
 * events (server crashes, backup completions, etc.).
 */
class TrayManager : public QObject {
    Q_OBJECT
public:
    explicit TrayManager(QMainWindow *mainWindow, QObject *parent = nullptr);

    /** Show a balloon notification in the system tray. */
    void notify(const QString &title, const QString &message,
                QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information,
                int durationMs = 5000);

    /** Whether the system tray is available on this platform. */
    bool isAvailable() const;

signals:
    /** Emitted when the user clicks "Quit" in the tray menu. */
    void quitRequested();

private:
    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu           *m_menu     = nullptr;
    QMainWindow     *m_mainWindow = nullptr;
};

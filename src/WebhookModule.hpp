#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>

/**
 * @brief Sends HTTP POST requests to Discord webhook URLs for event notifications.
 *
 * Notifications are non-blocking (fire-and-forget).
 */
class WebhookModule : public QObject {
    Q_OBJECT
public:
    explicit WebhookModule(QObject *parent = nullptr);

    /** Send a notification to a Discord webhook. Non-blocking. */
    void sendNotification(const QString &webhookUrl,
                          const QString &serverName,
                          const QString &message);

private:
    QNetworkAccessManager *m_nam;
};

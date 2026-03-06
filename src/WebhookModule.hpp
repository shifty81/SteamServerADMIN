#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>

/**
 * @brief Sends HTTP POST requests to Discord webhook URLs for event notifications.
 *
 * Notifications are non-blocking (fire-and-forget).
 * An optional message template may contain placeholders:
 *   {server}    – server name
 *   {event}     – event description
 *   {timestamp} – current date/time (ISO 8601)
 */
class WebhookModule : public QObject {
    Q_OBJECT
public:
    explicit WebhookModule(QObject *parent = nullptr);

    /** Send a notification to a Discord webhook. Non-blocking.
     *  @param messageTemplate  Optional custom template.  If empty, the
     *         default format "**[{server}]** {event}" is used. */
    void sendNotification(const QString &webhookUrl,
                          const QString &serverName,
                          const QString &message,
                          const QString &messageTemplate = QString());

    /** Format a message using the template string. Exposed for testing. */
    static QString formatMessage(const QString &tpl,
                                 const QString &serverName,
                                 const QString &event);

private:
    QNetworkAccessManager *m_nam;
};

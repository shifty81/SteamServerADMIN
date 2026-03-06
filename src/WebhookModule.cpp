#include "WebhookModule.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDebug>

WebhookModule::WebhookModule(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this))
{
}

QString WebhookModule::formatMessage(const QString &tpl,
                                     const QString &serverName,
                                     const QString &event)
{
    QString msg = tpl;
    msg.replace(QStringLiteral("{server}"),    serverName);
    msg.replace(QStringLiteral("{event}"),     event);
    msg.replace(QStringLiteral("{timestamp}"), QDateTime::currentDateTime().toString(Qt::ISODate));
    return msg;
}

void WebhookModule::sendNotification(const QString &webhookUrl,
                                     const QString &serverName,
                                     const QString &message,
                                     const QString &messageTemplate)
{
    if (webhookUrl.trimmed().isEmpty())
        return;

    QString content;
    if (!messageTemplate.trimmed().isEmpty())
        content = formatMessage(messageTemplate, serverName, message);
    else
        content = QStringLiteral("**[%1]** %2").arg(serverName, message);

    QJsonObject payload;
    payload[QStringLiteral("content")] = content;

    QNetworkRequest request{QUrl(webhookUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));

    QNetworkReply *reply =
        m_nam->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));

    // Log errors for debugging, then clean up (fire-and-forget)
    connect(reply, &QNetworkReply::finished, reply, [reply]() {
        if (reply->error() != QNetworkReply::NoError)
            qWarning() << "WebhookModule: delivery failed:" << reply->errorString();
        reply->deleteLater();
    });
}

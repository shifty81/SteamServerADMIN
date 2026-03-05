#include "WebhookModule.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

WebhookModule::WebhookModule(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this))
{
}

void WebhookModule::sendNotification(const QString &webhookUrl,
                                     const QString &serverName,
                                     const QString &message)
{
    if (webhookUrl.trimmed().isEmpty())
        return;

    QJsonObject payload;
    payload[QStringLiteral("content")]  =
        QStringLiteral("**[%1]** %2").arg(serverName, message);

    QNetworkRequest request{QUrl(webhookUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));

    QNetworkReply *reply =
        m_nam->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));

    // Clean up the reply when finished (fire-and-forget)
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

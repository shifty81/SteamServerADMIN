#include "SteamCmdModule.hpp"

#include <QDir>
#include <QProcess>
#include <QDebug>

SteamCmdModule::SteamCmdModule(QObject *parent) : QObject(parent)
{
#ifdef Q_OS_WIN
    m_steamCmdPath = QStringLiteral("steamcmd.exe");
#else
    m_steamCmdPath = QStringLiteral("steamcmd");
#endif
}

void SteamCmdModule::setSteamCmdPath(const QString &path)
{
    m_steamCmdPath = path;
}

QString SteamCmdModule::steamCmdPath() const
{
    return m_steamCmdPath;
}

void SteamCmdModule::deployServer(const ServerConfig &server)
{
    QDir().mkpath(server.dir);

    QStringList args = {
        QStringLiteral("+login"),    QStringLiteral("anonymous"),
        QStringLiteral("+force_install_dir"), server.dir,
        QStringLiteral("+app_update"), QString::number(server.appid),
        QStringLiteral("validate"),
        QStringLiteral("+quit")
    };
    runSteamCmd(args);
}

bool SteamCmdModule::updateMods(const ServerConfig &server)
{
    bool allOk = true;
    for (int modId : server.mods) {
        if (!downloadMod(server.appid, modId))
            allOk = false;
    }
    return allOk;
}

bool SteamCmdModule::downloadMod(int appid, int modId)
{
    QStringList args = {
        QStringLiteral("+login"),    QStringLiteral("anonymous"),
        QStringLiteral("+workshop_download_item"),
        QString::number(appid), QString::number(modId),
        QStringLiteral("+quit")
    };
    return runSteamCmd(args);
}

bool SteamCmdModule::runSteamCmd(const QStringList &args)
{
    QProcess process;
    process.setProgram(m_steamCmdPath);
    process.setArguments(args);
    process.setProcessChannelMode(QProcess::MergedChannels);

    process.start();
    while (process.waitForReadyRead(5000)) {
        while (process.canReadLine()) {
            QString line = QString::fromLocal8Bit(process.readLine()).trimmed();
            emit outputLine(line);
        }
    }
    // Drain any remaining output
    QByteArray remaining = process.readAll();
    if (!remaining.isEmpty())
        emit outputLine(QString::fromLocal8Bit(remaining).trimmed());

    process.waitForFinished(-1);
    bool ok = process.exitCode() == 0;
    emit finished(ok);
    return ok;
}

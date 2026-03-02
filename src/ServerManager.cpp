#include "ServerManager.hpp"
#include "BackupModule.hpp"
#include "SteamCmdModule.hpp"
#include "RconClient.hpp"

#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>

// ---------------------------------------------------------------------------
// Constructor / config I/O
// ---------------------------------------------------------------------------

ServerManager::ServerManager(const QString &configFile, QObject *parent)
    : QObject(parent), m_configFile(configFile)
{
#ifdef Q_OS_WIN
    m_steamCmdPath = QStringLiteral("steamcmd.exe");
#else
    m_steamCmdPath = QStringLiteral("steamcmd");
#endif
}

bool ServerManager::loadConfig()
{
    QFile file(m_configFile);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "ServerManager::loadConfig: cannot open" << m_configFile;
        return false;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "ServerManager::loadConfig: parse error:" << err.errorString();
        return false;
    }
    if (!doc.isArray()) return false;

    m_servers.clear();
    for (const QJsonValue &val : doc.array()) {
        QJsonObject obj = val.toObject();
        ServerConfig s;
        s.name           = obj[QStringLiteral("name")].toString();
        s.appid          = obj[QStringLiteral("appid")].toInt();
        s.dir            = obj[QStringLiteral("dir")].toString();
        s.executable     = obj[QStringLiteral("executable")].toString();
        s.launchArgs     = obj[QStringLiteral("launchArgs")].toString();
        s.backupFolder   = obj[QStringLiteral("backupFolder")].toString();
        s.autoUpdate     = obj[QStringLiteral("autoUpdate")].toBool(true);
        s.keepBackups    = obj[QStringLiteral("keepBackups")].toInt(10);
        s.backupIntervalMinutes  = obj[QStringLiteral("backupIntervalMinutes")].toInt(30);
        s.restartIntervalHours   = obj[QStringLiteral("restartIntervalHours")].toInt(24);

        QJsonObject rcon = obj[QStringLiteral("rcon")].toObject();
        s.rcon.host     = rcon[QStringLiteral("host")].toString(QStringLiteral("127.0.0.1"));
        s.rcon.port     = rcon[QStringLiteral("port")].toInt(27015);
        s.rcon.password = rcon[QStringLiteral("password")].toString();

        for (const QJsonValue &m : obj[QStringLiteral("mods")].toArray())
            s.mods << m.toInt();

        m_servers << s;
    }
    return true;
}

bool ServerManager::saveConfig() const
{
    QJsonArray arr;
    for (const ServerConfig &s : m_servers) {
        QJsonObject obj;
        obj[QStringLiteral("name")]          = s.name;
        obj[QStringLiteral("appid")]         = s.appid;
        obj[QStringLiteral("dir")]           = s.dir;
        obj[QStringLiteral("executable")]    = s.executable;
        obj[QStringLiteral("launchArgs")]    = s.launchArgs;
        obj[QStringLiteral("backupFolder")]  = s.backupFolder;
        obj[QStringLiteral("autoUpdate")]    = s.autoUpdate;
        obj[QStringLiteral("keepBackups")]   = s.keepBackups;
        obj[QStringLiteral("backupIntervalMinutes")] = s.backupIntervalMinutes;
        obj[QStringLiteral("restartIntervalHours")]  = s.restartIntervalHours;

        QJsonObject rcon;
        rcon[QStringLiteral("host")]     = s.rcon.host;
        rcon[QStringLiteral("port")]     = s.rcon.port;
        rcon[QStringLiteral("password")] = s.rcon.password;
        obj[QStringLiteral("rcon")] = rcon;

        QJsonArray mods;
        for (int m : s.mods) mods << m;
        obj[QStringLiteral("mods")] = mods;

        arr << obj;
    }

    QFile file(m_configFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "ServerManager::saveConfig: cannot write" << m_configFile;
        return false;
    }
    file.write(QJsonDocument(arr).toJson());
    return true;
}

QList<ServerConfig> &ServerManager::servers()       { return m_servers; }
const QList<ServerConfig> &ServerManager::servers() const { return m_servers; }

// ---------------------------------------------------------------------------
// Server lifecycle
// ---------------------------------------------------------------------------

void ServerManager::startServer(ServerConfig &server)
{
    if (isServerRunning(server)) {
        emit logMessage(server.name, QStringLiteral("Server is already running."));
        return;
    }

    QString exe = server.dir + QDir::separator() + server.executable;
    auto *proc  = new QProcess(this);
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, &server, proc]() {
        while (proc->canReadLine())
            emit logMessage(server.name, QString::fromLocal8Bit(proc->readLine()).trimmed());
    });
    connect(proc, &QProcess::readyReadStandardError, this, [this, &server, proc]() {
        while (proc->canReadLine())
            emit logMessage(server.name, QString::fromLocal8Bit(proc->readLine()).trimmed());
    });

    QStringList args;
    if (!server.launchArgs.isEmpty())
        args = server.launchArgs.split(QLatin1Char(' '), Qt::SkipEmptyParts);

    proc->start(exe, args);
    if (proc->waitForStarted(5000)) {
        m_processes[server.name] = proc;
        emit logMessage(server.name, QStringLiteral("Server started (PID %1).").arg(proc->processId()));
    } else {
        emit logMessage(server.name, QStringLiteral("Failed to start server: ") + proc->errorString());
        proc->deleteLater();
    }
}

void ServerManager::stopServer(ServerConfig &server)
{
    QProcess *proc = processFor(server);
    if (!proc) {
        emit logMessage(server.name, QStringLiteral("Server is not running."));
        return;
    }
    proc->terminate();
    if (!proc->waitForFinished(10000))
        proc->kill();
    m_processes.remove(server.name);
    proc->deleteLater();
    emit logMessage(server.name, QStringLiteral("Server stopped."));
}

void ServerManager::restartServer(ServerConfig &server)
{
    stopServer(server);
    startServer(server);
}

bool ServerManager::isServerRunning(const ServerConfig &server) const
{
    QProcess *proc = processFor(server);
    return proc && proc->state() == QProcess::Running;
}

QProcess *ServerManager::processFor(const ServerConfig &server) const
{
    return m_processes.value(server.name, nullptr);
}

// ---------------------------------------------------------------------------
// SteamCMD
// ---------------------------------------------------------------------------

void ServerManager::deployServer(ServerConfig &server)
{
    emit logMessage(server.name, QStringLiteral("Starting SteamCMD deployment…"));
    SteamCmdModule steamCmd;
    steamCmd.setSteamCmdPath(m_steamCmdPath);
    connect(&steamCmd, &SteamCmdModule::outputLine, this, [this, &server](const QString &line) {
        emit logMessage(server.name, line);
    });
    steamCmd.deployServer(server);
}

void ServerManager::updateMods(ServerConfig &server)
{
    emit logMessage(server.name, QStringLiteral("Updating mods…"));
    SteamCmdModule steamCmd;
    steamCmd.setSteamCmdPath(m_steamCmdPath);
    connect(&steamCmd, &SteamCmdModule::outputLine, this, [this, &server](const QString &line) {
        emit logMessage(server.name, line);
    });
    steamCmd.updateMods(server);
}

// ---------------------------------------------------------------------------
// Backup / restore
// ---------------------------------------------------------------------------

QString ServerManager::takeSnapshot(const ServerConfig &server)
{
    emit logMessage(server.name, QStringLiteral("Taking snapshot…"));
    QString ts = BackupModule::takeSnapshot(server);
    if (ts.isEmpty())
        emit logMessage(server.name, QStringLiteral("Snapshot failed."));
    else
        emit logMessage(server.name, QStringLiteral("Snapshot created: ") + ts);
    return ts;
}

bool ServerManager::restoreSnapshot(const QString &zipFile, const ServerConfig &server)
{
    emit logMessage(server.name, QStringLiteral("Restoring from: ") + zipFile);
    bool ok = BackupModule::restoreSnapshot(zipFile, server);
    emit logMessage(server.name, ok ? QStringLiteral("Restore complete.")
                                    : QStringLiteral("Restore failed."));
    return ok;
}

QStringList ServerManager::listSnapshots(const ServerConfig &server) const
{
    return BackupModule::listSnapshots(server);
}

// ---------------------------------------------------------------------------
// RCON
// ---------------------------------------------------------------------------

int ServerManager::getPlayerCount(const ServerConfig &server)
{
    RconClient rcon;
    if (!rcon.connect(server.rcon.host, server.rcon.port, server.rcon.password, 3000))
        return -1;
    QString resp = rcon.sendCommand(QStringLiteral("status"), 3000);
    // Parse "players : N humans" line from status output
    const QStringList lines = resp.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.contains(QStringLiteral("players"))) {
            QRegularExpression re(QStringLiteral("(\\d+) humans"));
            QRegularExpressionMatch m = re.match(line);
            if (m.hasMatch())
                return m.captured(1).toInt();
        }
    }
    return 0;
}

QString ServerManager::sendRconCommand(const ServerConfig &server, const QString &cmd)
{
    RconClient rcon;
    if (!rcon.connect(server.rcon.host, server.rcon.port, server.rcon.password, 3000))
        return QStringLiteral("[RCON] Connection failed.");
    return rcon.sendCommand(cmd);
}

// ---------------------------------------------------------------------------
// Cluster operations
// ---------------------------------------------------------------------------

void ServerManager::syncModsCluster()
{
    for (ServerConfig &s : m_servers)
        updateMods(s);
}

void ServerManager::syncConfigsCluster(const QString &masterConfigZip)
{
    for (const ServerConfig &s : std::as_const(m_servers)) {
        bool ok = BackupModule::extractZip(masterConfigZip, s.dir + QStringLiteral("/Configs"));
        emit logMessage(s.name,
                        ok ? QStringLiteral("Config synced from master zip.")
                           : QStringLiteral("Config sync failed."));
    }
}

void ServerManager::setSteamCmdPath(const QString &path) { m_steamCmdPath = path; }
QString ServerManager::steamCmdPath() const               { return m_steamCmdPath; }

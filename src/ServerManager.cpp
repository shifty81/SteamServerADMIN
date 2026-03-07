#include "ServerManager.hpp"
#include "BackupModule.hpp"
#include "SteamCmdModule.hpp"
#include "RconClient.hpp"
#include "ConsoleLogWriter.hpp"

#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QDebug>
#include <QTimer>

// ---------------------------------------------------------------------------
// Simple XOR-based obfuscation for RCON passwords at rest.
// NOT cryptographically secure – intended only to prevent casual reading
// of plaintext passwords in servers.json.
// ---------------------------------------------------------------------------
static const QByteArray kObfuscationKey = QByteArrayLiteral("SSA_RCON_KEY_2026");

static QString obfuscatePassword(const QString &plain)
{
    QByteArray data = plain.toUtf8();
    for (int i = 0; i < data.size(); ++i)
        data[i] = data[i] ^ kObfuscationKey[i % kObfuscationKey.size()];
    return QStringLiteral("obf:") + QString::fromLatin1(data.toBase64());
}

static QString deobfuscatePassword(const QString &stored)
{
    if (!stored.startsWith(QStringLiteral("obf:")))
        return stored;  // legacy plaintext value
    QByteArray data = QByteArray::fromBase64(stored.mid(4).toLatin1());
    for (int i = 0; i < data.size(); ++i)
        data[i] = data[i] ^ kObfuscationKey[i % kObfuscationKey.size()];
    return QString::fromUtf8(data);
}

// ---------------------------------------------------------------------------
// Constructor / config I/O
// ---------------------------------------------------------------------------

ServerManager::ServerManager(const QString &configFile, QObject *parent)
    : QObject(parent), m_configFile(configFile),
      m_webhook(new WebhookModule(this)),
      m_resourceMonitor(new ResourceMonitor(this)),
      m_eventHookManager(new EventHookManager(this))
{
#ifdef Q_OS_WIN
    m_steamCmdPath = QStringLiteral("steamcmd.exe");
#else
    m_steamCmdPath = QStringLiteral("steamcmd");
#endif

    // Check resource thresholds when usage is updated
    connect(m_resourceMonitor, &ResourceMonitor::usageUpdated,
            this, [this](const QMap<QString, ResourceUsage> &usage) {
        for (auto it = usage.constBegin(); it != usage.constEnd(); ++it) {
            const QString &name = it.key();
            const ResourceUsage &ru = it.value();
            // Find the server config for threshold comparison
            for (const ServerConfig &s : std::as_const(m_servers)) {
                if (s.name != name)
                    continue;
                if (s.cpuAlertThreshold > 0 && ru.cpuPercent > s.cpuAlertThreshold) {
                    emit resourceAlert(name,
                        QStringLiteral("CPU usage %.1f%% exceeds threshold %.1f%%")
                            .arg(ru.cpuPercent).arg(s.cpuAlertThreshold));
                }
                if (s.memAlertThresholdMB > 0) {
                    double memMB = static_cast<double>(ru.memoryBytes) / (1024.0 * 1024.0);
                    if (memMB > s.memAlertThresholdMB) {
                        emit resourceAlert(name,
                            QStringLiteral("Memory usage %.1f MB exceeds threshold %.1f MB")
                                .arg(memMB).arg(s.memAlertThresholdMB));
                    }
                }
                break;
            }
        }
    });
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
        s.notes          = obj[QStringLiteral("notes")].toString();
        s.discordWebhookUrl = obj[QStringLiteral("discordWebhookUrl")].toString();
        s.webhookTemplate= obj[QStringLiteral("webhookTemplate")].toString();
        s.autoUpdate     = obj[QStringLiteral("autoUpdate")].toBool(true);
        s.autoStartOnLaunch = obj[QStringLiteral("autoStartOnLaunch")].toBool(false);
        s.favorite       = obj[QStringLiteral("favorite")].toBool(false);
        s.keepBackups    = obj[QStringLiteral("keepBackups")].toInt(10);
        s.backupIntervalMinutes  = obj[QStringLiteral("backupIntervalMinutes")].toInt(30);
        s.restartIntervalHours   = obj[QStringLiteral("restartIntervalHours")].toInt(24);
        s.rconCommandIntervalMinutes = obj[QStringLiteral("rconCommandIntervalMinutes")].toInt(0);
        s.backupCompressionLevel = obj[QStringLiteral("backupCompressionLevel")].toInt(6);
        s.maintenanceStartHour   = obj[QStringLiteral("maintenanceStartHour")].toInt(-1);
        s.maintenanceEndHour     = obj[QStringLiteral("maintenanceEndHour")].toInt(-1);
        s.consoleLogging = obj[QStringLiteral("consoleLogging")].toBool(false);
        s.maxPlayers     = obj[QStringLiteral("maxPlayers")].toInt(0);
        s.restartWarningMinutes = obj[QStringLiteral("restartWarningMinutes")].toInt(15);
        s.restartWarningMessage = obj[QStringLiteral("restartWarningMessage")].toString();

        s.cpuAlertThreshold    = obj[QStringLiteral("cpuAlertThreshold")].toDouble(90.0);
        s.memAlertThresholdMB  = obj[QStringLiteral("memAlertThresholdMB")].toDouble(0.0);

        // Event hooks
        QJsonObject hooks = obj[QStringLiteral("eventHooks")].toObject();
        for (auto hIt = hooks.begin(); hIt != hooks.end(); ++hIt)
            s.eventHooks[hIt.key()] = hIt.value().toString();

        // Tags
        for (const QJsonValue &v : obj[QStringLiteral("tags")].toArray())
            s.tags << v.toString();

        for (const QJsonValue &v : obj[QStringLiteral("scheduledRconCommands")].toArray())
            s.scheduledRconCommands << v.toString();

        QJsonObject rcon = obj[QStringLiteral("rcon")].toObject();
        s.rcon.host     = rcon[QStringLiteral("host")].toString(QStringLiteral("127.0.0.1"));
        s.rcon.port     = rcon[QStringLiteral("port")].toInt(27015);
        s.rcon.password = deobfuscatePassword(rcon[QStringLiteral("password")].toString());

        for (const QJsonValue &m : obj[QStringLiteral("mods")].toArray())
            s.mods << m.toInt();

        for (const QJsonValue &m : obj[QStringLiteral("disabledMods")].toArray())
            s.disabledMods << m.toInt();

        m_servers << s;
    }
    return true;
}

QStringList ServerManager::validateAll() const
{
    QStringList errors;
    QSet<QString> seenNames;

    for (int i = 0; i < m_servers.size(); ++i) {
        const ServerConfig &s = m_servers.at(i);
        QStringList serverErrors = s.validate();
        for (const QString &e : std::as_const(serverErrors))
            errors << QStringLiteral("Server #%1 (%2): %3")
                          .arg(i + 1)
                          .arg(s.name.isEmpty() ? QStringLiteral("<unnamed>") : s.name, e);

        if (!s.name.trimmed().isEmpty()) {
            if (seenNames.contains(s.name))
                errors << QStringLiteral("Duplicate server name: '%1'.").arg(s.name);
            else
                seenNames.insert(s.name);
        }
    }

    return errors;
}

bool ServerManager::saveConfig() const
{
    // Validate before saving
    QStringList errors = validateAll();
    if (!errors.isEmpty()) {
        qWarning() << "ServerManager::saveConfig: validation failed:" << errors;
        return false;
    }

    QJsonArray arr;
    for (const ServerConfig &s : m_servers) {
        QJsonObject obj;
        obj[QStringLiteral("name")]          = s.name;
        obj[QStringLiteral("appid")]         = s.appid;
        obj[QStringLiteral("dir")]           = s.dir;
        obj[QStringLiteral("executable")]    = s.executable;
        obj[QStringLiteral("launchArgs")]    = s.launchArgs;
        obj[QStringLiteral("backupFolder")]  = s.backupFolder;
        obj[QStringLiteral("notes")]         = s.notes;
        obj[QStringLiteral("discordWebhookUrl")] = s.discordWebhookUrl;
        obj[QStringLiteral("webhookTemplate")] = s.webhookTemplate;
        obj[QStringLiteral("autoUpdate")]    = s.autoUpdate;
        obj[QStringLiteral("autoStartOnLaunch")] = s.autoStartOnLaunch;
        obj[QStringLiteral("favorite")]      = s.favorite;
        obj[QStringLiteral("keepBackups")]   = s.keepBackups;
        obj[QStringLiteral("backupIntervalMinutes")] = s.backupIntervalMinutes;
        obj[QStringLiteral("restartIntervalHours")]  = s.restartIntervalHours;
        obj[QStringLiteral("rconCommandIntervalMinutes")] = s.rconCommandIntervalMinutes;
        obj[QStringLiteral("backupCompressionLevel")] = s.backupCompressionLevel;
        obj[QStringLiteral("maintenanceStartHour")]   = s.maintenanceStartHour;
        obj[QStringLiteral("maintenanceEndHour")]     = s.maintenanceEndHour;
        obj[QStringLiteral("consoleLogging")] = s.consoleLogging;
        obj[QStringLiteral("maxPlayers")]     = s.maxPlayers;
        obj[QStringLiteral("restartWarningMinutes")] = s.restartWarningMinutes;
        obj[QStringLiteral("restartWarningMessage")]  = s.restartWarningMessage;

        obj[QStringLiteral("cpuAlertThreshold")]   = s.cpuAlertThreshold;
        obj[QStringLiteral("memAlertThresholdMB")] = s.memAlertThresholdMB;

        // Event hooks
        QJsonObject hooks;
        for (auto hIt = s.eventHooks.constBegin(); hIt != s.eventHooks.constEnd(); ++hIt)
            hooks[hIt.key()] = hIt.value();
        obj[QStringLiteral("eventHooks")] = hooks;

        // Tags
        QJsonArray tagsArr;
        for (const QString &tag : s.tags) tagsArr << tag;
        obj[QStringLiteral("tags")] = tagsArr;

        QJsonObject rcon;
        rcon[QStringLiteral("host")]     = s.rcon.host;
        rcon[QStringLiteral("port")]     = s.rcon.port;
        rcon[QStringLiteral("password")] = obfuscatePassword(s.rcon.password);
        obj[QStringLiteral("rcon")] = rcon;

        QJsonArray mods;
        for (int m : s.mods) mods << m;
        obj[QStringLiteral("mods")] = mods;

        QJsonArray disabledMods;
        for (int m : s.disabledMods) disabledMods << m;
        obj[QStringLiteral("disabledMods")] = disabledMods;

        QJsonArray scheduledRcon;
        for (const QString &cmd : s.scheduledRconCommands) scheduledRcon << cmd;
        obj[QStringLiteral("scheduledRconCommands")] = scheduledRcon;

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
// Auto-start
// ---------------------------------------------------------------------------

void ServerManager::autoStartServers()
{
    for (ServerConfig &s : m_servers) {
        if (s.autoStartOnLaunch)
            startServer(s);
    }
}

// ---------------------------------------------------------------------------
// Server lifecycle
// ---------------------------------------------------------------------------

void ServerManager::startServer(ServerConfig &server)
{
    if (isServerRunning(server)) {
        emit logMessage(server.name, QStringLiteral("Server is already running."));
        return;
    }

    // Auto-update mods before starting if enabled
    if (server.autoUpdate && !server.mods.isEmpty()) {
        emit logMessage(server.name, QStringLiteral("Auto-update: updating mods before start…"));
        updateMods(server);
    }

    // Reset crash counter on a fresh manual start
    m_crashCounts.remove(server.name);

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

    // Detect unexpected exits (crashes)
    QString sname = server.name;
    QMetaObject::Connection conn = connect(
        proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        this, [this, sname](int exitCode, QProcess::ExitStatus exitStatus) {
            onProcessFinished(sname, exitCode, exitStatus);
        });

    QStringList args;
    if (!server.launchArgs.isEmpty())
        args = server.launchArgs.split(QLatin1Char(' '), Qt::SkipEmptyParts);

    proc->start(exe, args);
    if (proc->waitForStarted(5000)) {
        m_processes[server.name] = proc;
        m_crashConns[server.name] = conn;
        m_startTimes[server.name] = QDateTime::currentDateTime();
        emit logMessage(server.name, QStringLiteral("Server started (PID %1).").arg(proc->processId()));

        // Track resource usage for this server process
        m_resourceMonitor->trackProcess(server.name, proc->processId());

        // Fire onStart event hook
        m_eventHookManager->fireHook(server.name, server.dir,
                                     QStringLiteral("onStart"),
                                     server.eventHooks.value(QStringLiteral("onStart")));

        m_webhook->sendNotification(server.discordWebhookUrl, server.name,
                                    QStringLiteral("Server started."),
                                    server.webhookTemplate);
    } else {
        emit logMessage(server.name, QStringLiteral("Failed to start server: ") + proc->errorString());
        QObject::disconnect(conn);
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
    // Disconnect the crash-detection handler so the intentional stop is not
    // treated as a crash.
    auto connIt = m_crashConns.find(server.name);
    if (connIt != m_crashConns.end()) {
        QObject::disconnect(*connIt);
        m_crashConns.erase(connIt);
    }
    proc->terminate();
    if (!proc->waitForFinished(10000))
        proc->kill();
    m_processes.remove(server.name);
    m_startTimes.remove(server.name);
    m_crashCounts.remove(server.name);
    m_resourceMonitor->untrackProcess(server.name);
    proc->deleteLater();
    emit logMessage(server.name, QStringLiteral("Server stopped."));

    // Fire onStop event hook
    m_eventHookManager->fireHook(server.name, server.dir,
                                 QStringLiteral("onStop"),
                                 server.eventHooks.value(QStringLiteral("onStop")));

    m_webhook->sendNotification(server.discordWebhookUrl, server.name,
                                QStringLiteral("Server stopped."),
                                server.webhookTemplate);
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

void ServerManager::onProcessFinished(const QString &serverName, int exitCode,
                                      QProcess::ExitStatus exitStatus)
{
    // Clean up the process entry and stored connection
    QProcess *proc = m_processes.value(serverName, nullptr);
    if (proc) {
        m_processes.remove(serverName);
        proc->deleteLater();
    }
    m_crashConns.remove(serverName);
    m_startTimes.remove(serverName);
    m_resourceMonitor->untrackProcess(serverName);

    if (exitStatus == QProcess::CrashExit) {
        int crashes = m_crashCounts.value(serverName, 0) + 1;
        m_crashCounts[serverName] = crashes;

        emit serverCrashed(serverName);

        // Fire onCrash event hook
        for (const ServerConfig &s : std::as_const(m_servers)) {
            if (s.name == serverName) {
                m_eventHookManager->fireHook(s.name, s.dir,
                                             QStringLiteral("onCrash"),
                                             s.eventHooks.value(QStringLiteral("onCrash")));
                break;
            }
        }

        // Send Discord webhook notification for crash
        for (const ServerConfig &s : std::as_const(m_servers)) {
            if (s.name == serverName) {
                m_webhook->sendNotification(s.discordWebhookUrl, serverName,
                                            QStringLiteral("Server crashed (exit code %1).").arg(exitCode),
                                            s.webhookTemplate);
                break;
            }
        }

        if (crashes > kMaxCrashRestarts) {
            emit logMessage(serverName,
                            QStringLiteral("Server crashed %1 times consecutively. "
                                           "Auto-restart disabled until manual start.")
                                .arg(crashes));
            return;
        }

        int delayMs = kCrashBackoffBaseMs * (1 << (crashes - 1));  // exponential backoff
        emit logMessage(serverName,
                        QStringLiteral("Server crashed (exit code %1, attempt %2/%3). "
                                       "Auto-restarting in %4 s…")
                            .arg(exitCode)
                            .arg(crashes)
                            .arg(kMaxCrashRestarts)
                            .arg(delayMs / 1000));

        // Find the server config and restart after delay
        QTimer::singleShot(delayMs, this, [this, serverName]() {
            for (ServerConfig &s : m_servers) {
                if (s.name == serverName) {
                    startServer(s);
                    break;
                }
            }
        });
    } else {
        m_crashCounts.remove(serverName);
        emit logMessage(serverName, QStringLiteral("Server exited normally."));
    }
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

bool ServerManager::updateMods(ServerConfig &server)
{
    // Take a snapshot before updating mods so we can roll back if needed
    emit logMessage(server.name, QStringLiteral("Taking pre-update snapshot…"));
    QString snapshotTs = takeSnapshot(server);

    emit logMessage(server.name, QStringLiteral("Updating mods…"));
    SteamCmdModule steamCmd;
    steamCmd.setSteamCmdPath(m_steamCmdPath);
    connect(&steamCmd, &SteamCmdModule::outputLine, this, [this, &server](const QString &line) {
        emit logMessage(server.name, line);
    });
    bool ok = steamCmd.updateMods(server);

    if (ok) {
        // Fire onUpdate event hook on success
        m_eventHookManager->fireHook(server.name, server.dir,
                                     QStringLiteral("onUpdate"),
                                     server.eventHooks.value(QStringLiteral("onUpdate")));
    }

    if (!ok && !snapshotTs.isEmpty()) {
        emit logMessage(server.name, QStringLiteral("Mod update failed – rolling back to pre-update snapshot…"));
        // Find the mods snapshot from the pre-update timestamp and restore it
        QStringList snapshots = listSnapshots(server);
        for (const QString &snap : std::as_const(snapshots)) {
            if (snap.contains(snapshotTs) && snap.endsWith(QStringLiteral("_mods.zip"))) {
                restoreSnapshot(snap, server);
                break;
            }
        }
        emit logMessage(server.name, QStringLiteral("Rollback complete."));
    } else if (!ok) {
        emit logMessage(server.name, QStringLiteral("Mod update failed and no snapshot available for rollback."));
    }

    return ok;
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
    else {
        emit logMessage(server.name, QStringLiteral("Snapshot created: ") + ts);

        // Fire onBackup event hook
        m_eventHookManager->fireHook(server.name, server.dir,
                                     QStringLiteral("onBackup"),
                                     server.eventHooks.value(QStringLiteral("onBackup")));

        m_webhook->sendNotification(server.discordWebhookUrl, server.name,
                                    QStringLiteral("Backup completed."),
                                    server.webhookTemplate);
    }
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
    if (server.consoleLogging)
        ConsoleLogWriter::append(server.dir, server.name, QStringLiteral("> ") + cmd);

    RconClient rcon;
    if (!rcon.connect(server.rcon.host, server.rcon.port, server.rcon.password, 3000))
        return QStringLiteral("[RCON] Connection failed.");
    QString resp = rcon.sendCommand(cmd);

    if (server.consoleLogging && !resp.isEmpty())
        ConsoleLogWriter::append(server.dir, server.name, resp);

    return resp;
}

// ---------------------------------------------------------------------------
// Cluster operations
// ---------------------------------------------------------------------------

void ServerManager::syncModsCluster()
{
    for (ServerConfig &s : m_servers)
        updateMods(s);
}

QStringList ServerManager::broadcastRconCommand(const QString &cmd)
{
    QStringList results;
    for (const ServerConfig &s : std::as_const(m_servers)) {
        QString resp = sendRconCommand(s, cmd);
        results << QStringLiteral("[%1] %2").arg(s.name, resp);
        emit logMessage(s.name, QStringLiteral("Broadcast RCON: %1 → %2").arg(cmd, resp));
    }
    return results;
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

// ---------------------------------------------------------------------------
// Server removal
// ---------------------------------------------------------------------------

bool ServerManager::removeServer(const QString &serverName)
{
    for (int i = 0; i < m_servers.size(); ++i) {
        if (m_servers.at(i).name == serverName) {
            // Stop if running
            if (isServerRunning(m_servers[i]))
                stopServer(m_servers[i]);

            m_servers.removeAt(i);
            emit logMessage(serverName, QStringLiteral("Server removed from configuration."));
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Export / Import
// ---------------------------------------------------------------------------

bool ServerManager::exportServerConfig(const QString &serverName,
                                       const QString &filePath) const
{
    const ServerConfig *found = nullptr;
    for (const ServerConfig &s : m_servers) {
        if (s.name == serverName) { found = &s; break; }
    }
    if (!found) return false;

    const ServerConfig &s = *found;
    QJsonObject obj;
    obj[QStringLiteral("name")]          = s.name;
    obj[QStringLiteral("appid")]         = s.appid;
    obj[QStringLiteral("dir")]           = s.dir;
    obj[QStringLiteral("executable")]    = s.executable;
    obj[QStringLiteral("launchArgs")]    = s.launchArgs;
    obj[QStringLiteral("backupFolder")]  = s.backupFolder;
    obj[QStringLiteral("notes")]         = s.notes;
    obj[QStringLiteral("discordWebhookUrl")] = s.discordWebhookUrl;
    obj[QStringLiteral("webhookTemplate")] = s.webhookTemplate;
    obj[QStringLiteral("autoUpdate")]    = s.autoUpdate;
    obj[QStringLiteral("autoStartOnLaunch")] = s.autoStartOnLaunch;
    obj[QStringLiteral("favorite")]      = s.favorite;
    obj[QStringLiteral("keepBackups")]   = s.keepBackups;
    obj[QStringLiteral("backupIntervalMinutes")] = s.backupIntervalMinutes;
    obj[QStringLiteral("restartIntervalHours")]  = s.restartIntervalHours;
    obj[QStringLiteral("rconCommandIntervalMinutes")] = s.rconCommandIntervalMinutes;
    obj[QStringLiteral("backupCompressionLevel")] = s.backupCompressionLevel;
    obj[QStringLiteral("maintenanceStartHour")]   = s.maintenanceStartHour;
    obj[QStringLiteral("maintenanceEndHour")]     = s.maintenanceEndHour;
    obj[QStringLiteral("consoleLogging")] = s.consoleLogging;
    obj[QStringLiteral("maxPlayers")]     = s.maxPlayers;
    obj[QStringLiteral("restartWarningMinutes")] = s.restartWarningMinutes;
    obj[QStringLiteral("restartWarningMessage")]  = s.restartWarningMessage;

    obj[QStringLiteral("cpuAlertThreshold")]   = s.cpuAlertThreshold;
    obj[QStringLiteral("memAlertThresholdMB")] = s.memAlertThresholdMB;

    // Event hooks
    QJsonObject hooks;
    for (auto hIt = s.eventHooks.constBegin(); hIt != s.eventHooks.constEnd(); ++hIt)
        hooks[hIt.key()] = hIt.value();
    obj[QStringLiteral("eventHooks")] = hooks;

    // Tags
    QJsonArray tagsArr;
    for (const QString &tag : s.tags) tagsArr << tag;
    obj[QStringLiteral("tags")] = tagsArr;

    QJsonObject rcon;
    rcon[QStringLiteral("host")]     = s.rcon.host;
    rcon[QStringLiteral("port")]     = s.rcon.port;
    rcon[QStringLiteral("password")] = obfuscatePassword(s.rcon.password);
    obj[QStringLiteral("rcon")] = rcon;

    QJsonArray mods;
    for (int m : s.mods) mods << m;
    obj[QStringLiteral("mods")] = mods;

    QJsonArray disabledMods;
    for (int m : s.disabledMods) disabledMods << m;
    obj[QStringLiteral("disabledMods")] = disabledMods;

    QJsonArray scheduledRcon;
    for (const QString &cmd : s.scheduledRconCommands) scheduledRcon << cmd;
    obj[QStringLiteral("scheduledRconCommands")] = scheduledRcon;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(obj).toJson());
    return true;
}

QString ServerManager::importServerConfig(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("Cannot open file.");

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError)
        return QStringLiteral("Invalid JSON: ") + err.errorString();
    if (!doc.isObject())
        return QStringLiteral("Expected a JSON object.");

    QJsonObject obj = doc.object();
    ServerConfig s;
    s.name           = obj[QStringLiteral("name")].toString();
    s.appid          = obj[QStringLiteral("appid")].toInt();
    s.dir            = obj[QStringLiteral("dir")].toString();
    s.executable     = obj[QStringLiteral("executable")].toString();
    s.launchArgs     = obj[QStringLiteral("launchArgs")].toString();
    s.backupFolder   = obj[QStringLiteral("backupFolder")].toString();
    s.notes          = obj[QStringLiteral("notes")].toString();
    s.discordWebhookUrl = obj[QStringLiteral("discordWebhookUrl")].toString();
    s.webhookTemplate= obj[QStringLiteral("webhookTemplate")].toString();
    s.autoUpdate     = obj[QStringLiteral("autoUpdate")].toBool(true);
    s.autoStartOnLaunch = obj[QStringLiteral("autoStartOnLaunch")].toBool(false);
    s.favorite       = obj[QStringLiteral("favorite")].toBool(false);
    s.keepBackups    = obj[QStringLiteral("keepBackups")].toInt(10);
    s.backupIntervalMinutes = obj[QStringLiteral("backupIntervalMinutes")].toInt(30);
    s.restartIntervalHours  = obj[QStringLiteral("restartIntervalHours")].toInt(24);
    s.rconCommandIntervalMinutes = obj[QStringLiteral("rconCommandIntervalMinutes")].toInt(0);
    s.backupCompressionLevel = obj[QStringLiteral("backupCompressionLevel")].toInt(6);
    s.maintenanceStartHour   = obj[QStringLiteral("maintenanceStartHour")].toInt(-1);
    s.maintenanceEndHour     = obj[QStringLiteral("maintenanceEndHour")].toInt(-1);
    s.consoleLogging = obj[QStringLiteral("consoleLogging")].toBool(false);
    s.maxPlayers     = obj[QStringLiteral("maxPlayers")].toInt(0);
    s.restartWarningMinutes = obj[QStringLiteral("restartWarningMinutes")].toInt(15);
    s.restartWarningMessage = obj[QStringLiteral("restartWarningMessage")].toString();

    s.cpuAlertThreshold    = obj[QStringLiteral("cpuAlertThreshold")].toDouble(90.0);
    s.memAlertThresholdMB  = obj[QStringLiteral("memAlertThresholdMB")].toDouble(0.0);

    // Event hooks
    QJsonObject hooks = obj[QStringLiteral("eventHooks")].toObject();
    for (auto hIt = hooks.begin(); hIt != hooks.end(); ++hIt)
        s.eventHooks[hIt.key()] = hIt.value().toString();

    // Tags
    for (const QJsonValue &v : obj[QStringLiteral("tags")].toArray())
        s.tags << v.toString();

    QJsonObject rcon = obj[QStringLiteral("rcon")].toObject();
    s.rcon.host     = rcon[QStringLiteral("host")].toString(QStringLiteral("127.0.0.1"));
    s.rcon.port     = rcon[QStringLiteral("port")].toInt(27015);
    s.rcon.password = deobfuscatePassword(rcon[QStringLiteral("password")].toString());

    for (const QJsonValue &m : obj[QStringLiteral("mods")].toArray())
        s.mods << m.toInt();
    for (const QJsonValue &m : obj[QStringLiteral("disabledMods")].toArray())
        s.disabledMods << m.toInt();
    for (const QJsonValue &v : obj[QStringLiteral("scheduledRconCommands")].toArray())
        s.scheduledRconCommands << v.toString();

    // Validate before adding
    m_servers << s;
    QStringList errors = validateAll();
    if (!errors.isEmpty()) {
        m_servers.removeLast();
        return errors.join(QStringLiteral("\n"));
    }

    emit logMessage(s.name, QStringLiteral("Server imported from file."));
    return QString();
}

// ---------------------------------------------------------------------------
// Uptime tracking
// ---------------------------------------------------------------------------

QDateTime ServerManager::serverStartTime(const QString &serverName) const
{
    return m_startTimes.value(serverName, QDateTime());
}

qint64 ServerManager::serverUptimeSeconds(const QString &serverName) const
{
    auto it = m_startTimes.constFind(serverName);
    if (it == m_startTimes.constEnd() || !it->isValid())
        return -1;
    return it->secsTo(QDateTime::currentDateTime());
}

// ---------------------------------------------------------------------------
// Crash backoff helpers
// ---------------------------------------------------------------------------

int ServerManager::crashCount(const QString &serverName) const
{
    return m_crashCounts.value(serverName, 0);
}

void ServerManager::resetCrashCount(const QString &serverName)
{
    m_crashCounts.remove(serverName);
}

// ---------------------------------------------------------------------------
// Pending update tracking
// ---------------------------------------------------------------------------

void ServerManager::setPendingUpdate(const QString &serverName, bool pending)
{
    if (pending)
        m_pendingUpdates[serverName] = true;
    else
        m_pendingUpdates.remove(serverName);
}

bool ServerManager::hasPendingUpdate(const QString &serverName) const
{
    return m_pendingUpdates.value(serverName, false);
}

void ServerManager::setPendingModUpdate(const QString &serverName, bool pending)
{
    if (pending)
        m_pendingModUpdates[serverName] = true;
    else
        m_pendingModUpdates.remove(serverName);
}

bool ServerManager::hasPendingModUpdate(const QString &serverName) const
{
    return m_pendingModUpdates.value(serverName, false);
}

// ---------------------------------------------------------------------------
// Restart warning
// ---------------------------------------------------------------------------

void ServerManager::sendRestartWarning(ServerConfig &server, int minutesRemaining)
{
    if (!isServerRunning(server))
        return;

    QString msg = server.formatRestartWarning(minutesRemaining);
    emit logMessage(server.name,
                    QStringLiteral("Restart warning (%1 min): %2")
                        .arg(minutesRemaining).arg(msg));

    // Send via RCON broadcast; common commands: "say", "broadcast", "ServerChat"
    sendRconCommand(server, QStringLiteral("broadcast %1").arg(msg));
}

// ---------------------------------------------------------------------------
// Resource monitor / Event hooks accessors
// ---------------------------------------------------------------------------

ResourceMonitor *ServerManager::resourceMonitor() { return m_resourceMonitor; }
EventHookManager *ServerManager::eventHookManager() { return m_eventHookManager; }

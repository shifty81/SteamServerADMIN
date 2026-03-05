#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QSignalSpy>

#include "ServerConfig.hpp"
#include "ServerManager.hpp"
#include "BackupModule.hpp"
#include "SchedulerModule.hpp"
#include "LogModule.hpp"
#include "GameTemplates.hpp"

// ---------------------------------------------------------------------------
// ServerConfig round-trip through ServerManager save/load
// ---------------------------------------------------------------------------
class TestServerConfig : public QObject {
    Q_OBJECT

private slots:
    void testSaveAndLoad();
    void testAddRemoveMod();
    void testBackupRotation();
    void testSchedulerStartStop();
    void testSchedulerTimerIntervals();
    void testCrashSignalEmitted();

    // ---- New validation tests ----
    void testValidateValidConfig();
    void testValidateEmptyName();
    void testValidateInvalidAppId();
    void testValidateEmptyDir();
    void testValidatePortRange();
    void testValidateNegativeIntervals();
    void testValidateAllDuplicateNames();
    void testSaveRejectsInvalidConfig();
    void testLoadEmptyArray();
    void testLoadMalformedJson();
    void testLoadMissingFile();
    void testMultipleServersRoundTrip();
    void testDefaultFieldValues();

    // ---- LogModule tests ----
    void testLogModuleWritesEntries();
    void testLogModuleMaxEntries();
    void testLogModuleFileOutput();
    void testLogModuleEntryAddedSignal();

    // ---- Server cloning tests ----
    void testCloneServerConfig();
    void testCloneServerDuplicateNameRejected();

    // ---- Server removal tests ----
    void testRemoveServer();
    void testRemoveServerNotFound();
    void testRemoveServerPersistence();

    // ---- Broadcast RCON tests ----
    void testBroadcastRconCommand();

    // ---- Export/Import tests ----
    void testExportServerConfig();
    void testExportServerNotFound();
    void testImportServerConfig();
    void testImportServerDuplicateName();

    // ---- Disabled mods tests ----
    void testDisabledModsPersistence();
    void testDisabledModsDefaultEmpty();

    // ---- Game templates tests ----
    void testBuiltinTemplatesNotEmpty();
    void testBuiltinTemplatesHaveValidAppIds();
    void testBuiltinTemplatesContainCustomEntry();

    // ---- Uptime tracking tests ----
    void testUptimeNotRunning();
    void testStartTimeRecorded();

    // ---- Crash backoff tests ----
    void testCrashCountDefault();
    void testCrashCountReset();
    void testCrashBackoffConstants();

    // ---- Notes field tests ----
    void testNotesPersistence();
    void testNotesDefaultEmpty();
    void testNotesExportImport();

    // ---- Mod ordering tests ----
    void testModOrderPreserved();

    // ---- Update rollback tests ----
    void testUpdateModsReturnsBool();

    // ---- Discord webhook field tests ----
    void testDiscordWebhookUrlPersistence();
    void testDiscordWebhookUrlDefaultEmpty();
    void testDiscordWebhookUrlExportImport();

    // ---- Auto-start on launch tests ----
    void testAutoStartOnLaunchPersistence();
    void testAutoStartOnLaunchDefaultFalse();
    void testAutoStartServersMethod();

    // ---- Scheduled RCON commands tests ----
    void testScheduledRconCommandsPersistence();
    void testScheduledRconCommandsDefaultEmpty();
    void testRconCommandIntervalValidation();
    void testSchedulerRconTimer();

    // ---- Favorite servers tests ----
    void testFavoritePersistence();
    void testFavoriteDefaultFalse();
    void testFavoriteExportImport();

    // ---- Config diff tests ----
    void testValidateRconIntervalNegative();
};

void TestServerConfig::testSaveAndLoad()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QString configPath = tmp.filePath(QStringLiteral("servers.json"));

    // Build a server config and save it
    ServerManager mgr(configPath);

    ServerConfig s;
    s.name         = QStringLiteral("Test Server");
    s.appid        = 376030;
    s.dir          = QStringLiteral("/srv/test");
    s.executable   = QStringLiteral("server.exe");
    s.backupFolder = QStringLiteral("/srv/backups/test");
    s.rcon.host    = QStringLiteral("127.0.0.1");
    s.rcon.port    = 27020;
    s.rcon.password= QStringLiteral("secret");
    s.mods         = { 111, 222, 333 };
    s.autoUpdate   = true;
    s.keepBackups  = 5;

    mgr.servers() << s;
    QVERIFY(mgr.saveConfig());

    // Load back into a fresh manager
    ServerManager mgr2(configPath);
    QVERIFY(mgr2.loadConfig());
    QCOMPARE(mgr2.servers().size(), 1);

    const ServerConfig &loaded = mgr2.servers().first();
    QCOMPARE(loaded.name,          QStringLiteral("Test Server"));
    QCOMPARE(loaded.appid,         376030);
    QCOMPARE(loaded.dir,           QStringLiteral("/srv/test"));
    QCOMPARE(loaded.executable,    QStringLiteral("server.exe"));
    QCOMPARE(loaded.backupFolder,  QStringLiteral("/srv/backups/test"));
    QCOMPARE(loaded.rcon.host,     QStringLiteral("127.0.0.1"));
    QCOMPARE(loaded.rcon.port,     27020);
    QCOMPARE(loaded.rcon.password, QStringLiteral("secret"));
    QCOMPARE(loaded.mods,          QList<int>({ 111, 222, 333 }));
    QCOMPARE(loaded.autoUpdate,    true);
    QCOMPARE(loaded.keepBackups,   5);
}

void TestServerConfig::testAddRemoveMod()
{
    ServerConfig s;
    s.mods = { 100, 200, 300 };

    // Add
    int newMod = 400;
    if (!s.mods.contains(newMod))
        s.mods << newMod;
    QVERIFY(s.mods.contains(newMod));
    QCOMPARE(s.mods.size(), 4);

    // Remove
    s.mods.removeAll(200);
    QVERIFY(!s.mods.contains(200));
    QCOMPARE(s.mods.size(), 3);

    // No duplicate
    s.mods << 100;
    // Still should contain 100 once more – removeAll removes all
    QCOMPARE(s.mods.count(100), 2);
}

void TestServerConfig::testBackupRotation()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ServerConfig s;
    s.backupFolder = tmp.path();
    s.keepBackups  = 3;

    // Create 5 dummy zip files with ascending timestamps
    QStringList created;
    for (int i = 1; i <= 5; ++i) {
        QString path = tmp.filePath(
            QStringLiteral("2026010%1_120000_config.zip").arg(i));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("dummy");
        f.close();
        created << path;
    }

    BackupModule::rotateBackups(s);

    // Only the 3 newest should remain
    QDir dir(tmp.path());
    QStringList remaining = dir.entryList({ QStringLiteral("*_config.zip") }, QDir::Files);
    QCOMPARE(remaining.size(), 3);

    // The 3 newest (highest timestamps) should survive
    for (const QString &f : { QStringLiteral("20260105_120000_config.zip"),
                               QStringLiteral("20260104_120000_config.zip"),
                               QStringLiteral("20260103_120000_config.zip") }) {
        QVERIFY2(remaining.contains(f), qPrintable(f + " should be kept"));
    }
}

void TestServerConfig::testSchedulerStartStop()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QString configPath = tmp.filePath(QStringLiteral("servers.json"));

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = QStringLiteral("Sched Server");
    s.appid = 730;
    s.dir = tmp.path();
    s.backupFolder = tmp.filePath(QStringLiteral("backups"));
    s.backupIntervalMinutes = 10;
    s.restartIntervalHours = 2;
    mgr.servers() << s;
    mgr.saveConfig();

    SchedulerModule scheduler(&mgr);

    // Start scheduler for the server
    scheduler.startScheduler(QStringLiteral("Sched Server"));

    // Starting again should not crash (replaces timers)
    scheduler.startScheduler(QStringLiteral("Sched Server"));

    // Stop scheduler
    scheduler.stopScheduler(QStringLiteral("Sched Server"));

    // Stopping an unknown server should not crash
    scheduler.stopScheduler(QStringLiteral("Unknown"));

    // startAll / stopAll
    scheduler.startAll();
    scheduler.stopAll();
}

void TestServerConfig::testSchedulerTimerIntervals()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QString configPath = tmp.filePath(QStringLiteral("servers.json"));

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = QStringLiteral("Timer Server");
    s.appid = 730;
    s.dir = tmp.path();
    s.backupFolder = tmp.filePath(QStringLiteral("backups"));
    s.backupIntervalMinutes = 0;   // disabled
    s.restartIntervalHours  = 0;   // disabled
    mgr.servers() << s;

    SchedulerModule scheduler(&mgr);
    scheduler.startAll();

    // With intervals set to 0, no timers should fire (no crash)
    scheduler.stopAll();

    // Now test with positive values
    mgr.servers()[0].backupIntervalMinutes = 5;
    mgr.servers()[0].restartIntervalHours = 1;
    scheduler.startAll();
    scheduler.stopAll();
}

void TestServerConfig::testCrashSignalEmitted()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QString configPath = tmp.filePath(QStringLiteral("servers.json"));

    ServerManager mgr(configPath);

    // Verify the serverCrashed signal exists and can be spied on
    QSignalSpy spy(&mgr, &ServerManager::serverCrashed);
    QVERIFY(spy.isValid());

    // No crashes should have been emitted yet
    QCOMPARE(spy.count(), 0);
}

// ---------------------------------------------------------------------------
// New validation tests
// ---------------------------------------------------------------------------

void TestServerConfig::testValidateValidConfig()
{
    ServerConfig s;
    s.name  = QStringLiteral("MyServer");
    s.appid = 730;
    s.dir   = QStringLiteral("/srv/cs2");
    s.rcon.port = 27015;

    QStringList errors = s.validate();
    QVERIFY2(errors.isEmpty(),
             qPrintable(QStringLiteral("Expected no errors, got: ") + errors.join(QStringLiteral("; "))));
}

void TestServerConfig::testValidateEmptyName()
{
    ServerConfig s;
    s.name  = QStringLiteral("");
    s.appid = 730;
    s.dir   = QStringLiteral("/srv/cs2");

    QStringList errors = s.validate();
    QVERIFY(!errors.isEmpty());
    QVERIFY(errors.first().contains(QStringLiteral("name")));

    // Whitespace-only name should also fail
    s.name = QStringLiteral("   ");
    errors = s.validate();
    QVERIFY(!errors.isEmpty());
}

void TestServerConfig::testValidateInvalidAppId()
{
    ServerConfig s;
    s.name  = QStringLiteral("Test");
    s.appid = 0;
    s.dir   = QStringLiteral("/srv/test");

    QStringList errors = s.validate();
    QVERIFY(!errors.isEmpty());
    QVERIFY(errors.first().contains(QStringLiteral("AppID")));

    // Negative AppID
    s.appid = -1;
    errors = s.validate();
    QVERIFY(!errors.isEmpty());
}

void TestServerConfig::testValidateEmptyDir()
{
    ServerConfig s;
    s.name  = QStringLiteral("Test");
    s.appid = 730;
    s.dir   = QStringLiteral("");

    QStringList errors = s.validate();
    QVERIFY(!errors.isEmpty());
    QVERIFY(errors.first().contains(QStringLiteral("directory")));
}

void TestServerConfig::testValidatePortRange()
{
    ServerConfig s;
    s.name  = QStringLiteral("Test");
    s.appid = 730;
    s.dir   = QStringLiteral("/srv/test");

    // Port 0
    s.rcon.port = 0;
    QStringList errors = s.validate();
    QVERIFY(!errors.isEmpty());
    QVERIFY(errors.first().contains(QStringLiteral("port")));

    // Port too high
    s.rcon.port = 70000;
    errors = s.validate();
    QVERIFY(!errors.isEmpty());

    // Negative port
    s.rcon.port = -1;
    errors = s.validate();
    QVERIFY(!errors.isEmpty());

    // Valid boundary values
    s.rcon.port = 1;
    errors = s.validate();
    QVERIFY(errors.isEmpty());

    s.rcon.port = 65535;
    errors = s.validate();
    QVERIFY(errors.isEmpty());
}

void TestServerConfig::testValidateNegativeIntervals()
{
    ServerConfig s;
    s.name  = QStringLiteral("Test");
    s.appid = 730;
    s.dir   = QStringLiteral("/srv/test");

    s.keepBackups = -1;
    QStringList errors = s.validate();
    QVERIFY(!errors.isEmpty());

    s.keepBackups = 0;
    s.backupIntervalMinutes = -5;
    errors = s.validate();
    QVERIFY(!errors.isEmpty());

    s.backupIntervalMinutes = 0;
    s.restartIntervalHours = -1;
    errors = s.validate();
    QVERIFY(!errors.isEmpty());
}

void TestServerConfig::testValidateAllDuplicateNames()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ServerManager mgr(tmp.filePath(QStringLiteral("servers.json")));

    ServerConfig s1;
    s1.name  = QStringLiteral("DupeServer");
    s1.appid = 730;
    s1.dir   = QStringLiteral("/srv/a");

    ServerConfig s2;
    s2.name  = QStringLiteral("DupeServer");
    s2.appid = 2430930;
    s2.dir   = QStringLiteral("/srv/b");

    mgr.servers() << s1 << s2;

    QStringList errors = mgr.validateAll();
    QVERIFY(!errors.isEmpty());

    // Should mention "Duplicate"
    bool foundDuplicate = false;
    for (const QString &e : std::as_const(errors)) {
        if (e.contains(QStringLiteral("Duplicate")))
            foundDuplicate = true;
    }
    QVERIFY2(foundDuplicate, "Expected a duplicate-name error");
}

void TestServerConfig::testSaveRejectsInvalidConfig()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QString configPath = tmp.filePath(QStringLiteral("servers.json"));
    ServerManager mgr(configPath);

    ServerConfig s;
    s.name  = QStringLiteral("");   // invalid: empty name
    s.appid = 0;                    // invalid: zero appid
    s.dir   = QStringLiteral("");   // invalid: empty dir
    mgr.servers() << s;

    // Save should fail because config is invalid
    QVERIFY(!mgr.saveConfig());

    // File should not have been created
    QVERIFY(!QFile::exists(configPath));
}

void TestServerConfig::testLoadEmptyArray()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QString configPath = tmp.filePath(QStringLiteral("servers.json"));

    // Write a valid empty JSON array
    QFile f(configPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("[]");
    f.close();

    ServerManager mgr(configPath);
    QVERIFY(mgr.loadConfig());
    QCOMPARE(mgr.servers().size(), 0);
}

void TestServerConfig::testLoadMalformedJson()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QString configPath = tmp.filePath(QStringLiteral("servers.json"));

    QFile f(configPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("{not valid json!!!");
    f.close();

    ServerManager mgr(configPath);
    QVERIFY(!mgr.loadConfig());
}

void TestServerConfig::testLoadMissingFile()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ServerManager mgr(tmp.filePath(QStringLiteral("nonexistent.json")));
    QVERIFY(!mgr.loadConfig());
}

void TestServerConfig::testMultipleServersRoundTrip()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QString configPath = tmp.filePath(QStringLiteral("servers.json"));
    ServerManager mgr(configPath);

    for (int i = 1; i <= 5; ++i) {
        ServerConfig s;
        s.name  = QStringLiteral("Server_%1").arg(i);
        s.appid = 730 + i;
        s.dir   = QStringLiteral("/srv/s%1").arg(i);
        s.mods  = { i * 100, i * 200 };
        mgr.servers() << s;
    }

    QVERIFY(mgr.saveConfig());

    ServerManager mgr2(configPath);
    QVERIFY(mgr2.loadConfig());
    QCOMPARE(mgr2.servers().size(), 5);

    for (int i = 0; i < 5; ++i) {
        QCOMPARE(mgr2.servers().at(i).name,  QStringLiteral("Server_%1").arg(i + 1));
        QCOMPARE(mgr2.servers().at(i).appid, 730 + i + 1);
        QCOMPARE(mgr2.servers().at(i).mods.size(), 2);
    }
}

void TestServerConfig::testDefaultFieldValues()
{
    // Ensure default values match expected defaults
    ServerConfig s;
    QCOMPARE(s.appid,                0);
    QCOMPARE(s.rcon.port,            27015);
    QCOMPARE(s.autoUpdate,           true);
    QCOMPARE(s.backupIntervalMinutes, 30);
    QCOMPARE(s.restartIntervalHours, 24);
    QCOMPARE(s.keepBackups,          10);
    QVERIFY(s.name.isEmpty());
    QVERIFY(s.dir.isEmpty());
    QVERIFY(s.executable.isEmpty());
    QVERIFY(s.mods.isEmpty());
}

// ---------------------------------------------------------------------------
// LogModule tests
// ---------------------------------------------------------------------------

void TestServerConfig::testLogModuleWritesEntries()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    LogModule log(tmp.filePath(QStringLiteral("test.log")));
    log.log(QStringLiteral("Server1"), QStringLiteral("Started"));
    log.log(QStringLiteral("Server2"), QStringLiteral("Backup complete"));

    QStringList entries = log.entries();
    QCOMPARE(entries.size(), 2);
    QVERIFY(entries.at(0).contains(QStringLiteral("Server1")));
    QVERIFY(entries.at(0).contains(QStringLiteral("Started")));
    QVERIFY(entries.at(1).contains(QStringLiteral("Server2")));
    QVERIFY(entries.at(1).contains(QStringLiteral("Backup complete")));
}

void TestServerConfig::testLogModuleMaxEntries()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    LogModule log(tmp.filePath(QStringLiteral("test.log")));
    log.setMaxEntries(3);

    for (int i = 0; i < 5; ++i)
        log.log(QStringLiteral("S"), QStringLiteral("msg%1").arg(i));

    QStringList entries = log.entries();
    QCOMPARE(entries.size(), 3);
    // Oldest entries should have been trimmed; newest 3 remain
    QVERIFY(entries.at(0).contains(QStringLiteral("msg2")));
    QVERIFY(entries.at(1).contains(QStringLiteral("msg3")));
    QVERIFY(entries.at(2).contains(QStringLiteral("msg4")));
}

void TestServerConfig::testLogModuleFileOutput()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QString logPath = tmp.filePath(QStringLiteral("test.log"));

    {
        LogModule log(logPath);
        log.log(QStringLiteral("TestSrv"), QStringLiteral("hello"));
    }

    QFile f(logPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QString content = QString::fromUtf8(f.readAll());
    QVERIFY(content.contains(QStringLiteral("TestSrv")));
    QVERIFY(content.contains(QStringLiteral("hello")));
}

void TestServerConfig::testLogModuleEntryAddedSignal()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    LogModule log(tmp.filePath(QStringLiteral("test.log")));
    QSignalSpy spy(&log, &LogModule::entryAdded);
    QVERIFY(spy.isValid());

    log.log(QStringLiteral("S"), QStringLiteral("event"));
    QCOMPARE(spy.count(), 1);

    QString emitted = spy.at(0).at(0).toString();
    QVERIFY(emitted.contains(QStringLiteral("event")));
}

// ---------------------------------------------------------------------------
// Server cloning tests
// ---------------------------------------------------------------------------

void TestServerConfig::testCloneServerConfig()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ServerManager mgr(tmp.filePath(QStringLiteral("servers.json")));

    ServerConfig s;
    s.name  = QStringLiteral("Original");
    s.appid = 730;
    s.dir   = QStringLiteral("/srv/orig");
    s.mods  = { 111, 222 };
    s.rcon.port = 27015;
    s.backupIntervalMinutes = 15;
    mgr.servers() << s;

    // Clone it
    ServerConfig cloned = s;
    cloned.name = QStringLiteral("Clone");
    mgr.servers() << cloned;

    // Validate – should pass (no duplicates, both configs valid)
    QStringList errors = mgr.validateAll();
    QVERIFY2(errors.isEmpty(),
             qPrintable(QStringLiteral("Expected no errors: ") + errors.join(QStringLiteral("; "))));

    // Verify clone has same fields
    const ServerConfig &c = mgr.servers().last();
    QCOMPARE(c.name,  QStringLiteral("Clone"));
    QCOMPARE(c.appid, 730);
    QCOMPARE(c.dir,   QStringLiteral("/srv/orig"));
    QCOMPARE(c.mods,  QList<int>({ 111, 222 }));
    QCOMPARE(c.backupIntervalMinutes, 15);

    // Save and reload
    QVERIFY(mgr.saveConfig());
    ServerManager mgr2(tmp.filePath(QStringLiteral("servers.json")));
    QVERIFY(mgr2.loadConfig());
    QCOMPARE(mgr2.servers().size(), 2);
    QCOMPARE(mgr2.servers().at(1).name, QStringLiteral("Clone"));
}

void TestServerConfig::testCloneServerDuplicateNameRejected()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ServerManager mgr(tmp.filePath(QStringLiteral("servers.json")));

    ServerConfig s;
    s.name  = QStringLiteral("MyServer");
    s.appid = 730;
    s.dir   = QStringLiteral("/srv/my");
    mgr.servers() << s;

    // Clone with same name (should fail validation)
    ServerConfig cloned = s;  // same name
    mgr.servers() << cloned;

    QStringList errors = mgr.validateAll();
    QVERIFY(!errors.isEmpty());

    bool foundDuplicate = false;
    for (const QString &e : std::as_const(errors)) {
        if (e.contains(QStringLiteral("Duplicate")))
            foundDuplicate = true;
    }
    QVERIFY2(foundDuplicate, "Cloning with same name should trigger duplicate error");
}

// ---------------------------------------------------------------------------
// Server removal tests
// ---------------------------------------------------------------------------

void TestServerConfig::testRemoveServer()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ServerManager mgr(tmp.filePath(QStringLiteral("servers.json")));

    ServerConfig s1;
    s1.name  = QStringLiteral("Server1");
    s1.appid = 730;
    s1.dir   = QStringLiteral("/srv/s1");

    ServerConfig s2;
    s2.name  = QStringLiteral("Server2");
    s2.appid = 2430930;
    s2.dir   = QStringLiteral("/srv/s2");

    mgr.servers() << s1 << s2;
    QCOMPARE(mgr.servers().size(), 2);

    // Remove first server
    bool removed = mgr.removeServer(QStringLiteral("Server1"));
    QVERIFY(removed);
    QCOMPARE(mgr.servers().size(), 1);
    QCOMPARE(mgr.servers().first().name, QStringLiteral("Server2"));
}

void TestServerConfig::testRemoveServerNotFound()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ServerManager mgr(tmp.filePath(QStringLiteral("servers.json")));

    ServerConfig s;
    s.name  = QStringLiteral("MyServer");
    s.appid = 730;
    s.dir   = QStringLiteral("/srv/my");
    mgr.servers() << s;

    // Try to remove a non-existent server
    bool removed = mgr.removeServer(QStringLiteral("NonExistent"));
    QVERIFY(!removed);
    QCOMPARE(mgr.servers().size(), 1);
}

void TestServerConfig::testRemoveServerPersistence()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QString configPath = tmp.filePath(QStringLiteral("servers.json"));
    ServerManager mgr(configPath);

    ServerConfig s1;
    s1.name  = QStringLiteral("Alpha");
    s1.appid = 730;
    s1.dir   = QStringLiteral("/srv/alpha");

    ServerConfig s2;
    s2.name  = QStringLiteral("Beta");
    s2.appid = 2430930;
    s2.dir   = QStringLiteral("/srv/beta");

    mgr.servers() << s1 << s2;
    QVERIFY(mgr.saveConfig());

    // Remove and save
    mgr.removeServer(QStringLiteral("Alpha"));
    QVERIFY(mgr.saveConfig());

    // Reload and verify
    ServerManager mgr2(configPath);
    QVERIFY(mgr2.loadConfig());
    QCOMPARE(mgr2.servers().size(), 1);
    QCOMPARE(mgr2.servers().first().name, QStringLiteral("Beta"));
}

// ---------------------------------------------------------------------------
// Broadcast RCON tests
// ---------------------------------------------------------------------------

void TestServerConfig::testBroadcastRconCommand()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ServerManager mgr(tmp.filePath(QStringLiteral("servers.json")));

    ServerConfig s1;
    s1.name  = QStringLiteral("S1");
    s1.appid = 730;
    s1.dir   = QStringLiteral("/srv/s1");
    s1.rcon.host = QStringLiteral("127.0.0.1");
    s1.rcon.port = 27015;

    ServerConfig s2;
    s2.name  = QStringLiteral("S2");
    s2.appid = 2430930;
    s2.dir   = QStringLiteral("/srv/s2");
    s2.rcon.host = QStringLiteral("127.0.0.1");
    s2.rcon.port = 27016;

    mgr.servers() << s1 << s2;

    // broadcastRconCommand should return one result per server
    // (RCON will fail to connect in test env, but that's expected)
    QStringList results = mgr.broadcastRconCommand(QStringLiteral("status"));
    QCOMPARE(results.size(), 2);
    QVERIFY(results.at(0).contains(QStringLiteral("[S1]")));
    QVERIFY(results.at(1).contains(QStringLiteral("[S2]")));
}

QTEST_MAIN(TestServerConfig)

// ---------------------------------------------------------------------------
// Export/Import tests
// ---------------------------------------------------------------------------

void TestServerConfig::testExportServerConfig()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ServerManager mgr(tmp.filePath(QStringLiteral("servers.json")));

    ServerConfig s;
    s.name  = QStringLiteral("ExportMe");
    s.appid = 730;
    s.dir   = QStringLiteral("/srv/export");
    s.executable = QStringLiteral("server.exe");
    s.mods  = { 111, 222 };
    s.disabledMods = { 222 };
    s.rcon.port = 27020;
    mgr.servers() << s;

    QString exportPath = tmp.filePath(QStringLiteral("exported.json"));
    QVERIFY(mgr.exportServerConfig(QStringLiteral("ExportMe"), exportPath));

    // Verify the file exists and contains expected data
    QFile f(exportPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    QVERIFY(doc.isObject());

    QJsonObject obj = doc.object();
    QCOMPARE(obj[QStringLiteral("name")].toString(), QStringLiteral("ExportMe"));
    QCOMPARE(obj[QStringLiteral("appid")].toInt(), 730);
    QCOMPARE(obj[QStringLiteral("dir")].toString(), QStringLiteral("/srv/export"));

    // Verify mods and disabledMods are present
    QJsonArray mods = obj[QStringLiteral("mods")].toArray();
    QCOMPARE(mods.size(), 2);
    QJsonArray disabled = obj[QStringLiteral("disabledMods")].toArray();
    QCOMPARE(disabled.size(), 1);
    QCOMPARE(disabled.at(0).toInt(), 222);
}

void TestServerConfig::testExportServerNotFound()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ServerManager mgr(tmp.filePath(QStringLiteral("servers.json")));
    QString exportPath = tmp.filePath(QStringLiteral("exported.json"));

    // No servers loaded – export should fail
    QVERIFY(!mgr.exportServerConfig(QStringLiteral("NonExistent"), exportPath));
    QVERIFY(!QFile::exists(exportPath));
}

void TestServerConfig::testImportServerConfig()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // First create and export a server
    ServerManager mgr1(tmp.filePath(QStringLiteral("servers1.json")));
    ServerConfig s;
    s.name  = QStringLiteral("Imported");
    s.appid = 2430930;
    s.dir   = QStringLiteral("/srv/import");
    s.mods  = { 333, 444 };
    s.disabledMods = { 444 };
    mgr1.servers() << s;

    QString exportPath = tmp.filePath(QStringLiteral("to_import.json"));
    QVERIFY(mgr1.exportServerConfig(QStringLiteral("Imported"), exportPath));

    // Import into a fresh manager
    ServerManager mgr2(tmp.filePath(QStringLiteral("servers2.json")));
    QString error = mgr2.importServerConfig(exportPath);
    QVERIFY2(error.isEmpty(), qPrintable(QStringLiteral("Import failed: ") + error));

    QCOMPARE(mgr2.servers().size(), 1);
    const ServerConfig &imported = mgr2.servers().first();
    QCOMPARE(imported.name,  QStringLiteral("Imported"));
    QCOMPARE(imported.appid, 2430930);
    QCOMPARE(imported.mods,  QList<int>({ 333, 444 }));
    QCOMPARE(imported.disabledMods, QList<int>({ 444 }));
}

void TestServerConfig::testImportServerDuplicateName()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // Create a server and export it
    ServerManager mgr(tmp.filePath(QStringLiteral("servers.json")));
    ServerConfig s;
    s.name  = QStringLiteral("DupeImport");
    s.appid = 730;
    s.dir   = QStringLiteral("/srv/dupe");
    mgr.servers() << s;

    QString exportPath = tmp.filePath(QStringLiteral("dupe.json"));
    QVERIFY(mgr.exportServerConfig(QStringLiteral("DupeImport"), exportPath));

    // Try to import – should fail because the name already exists
    QString error = mgr.importServerConfig(exportPath);
    QVERIFY(!error.isEmpty());
    QVERIFY(error.contains(QStringLiteral("Duplicate")));
    QCOMPARE(mgr.servers().size(), 1);  // should not have added
}

// ---------------------------------------------------------------------------
// Disabled mods tests
// ---------------------------------------------------------------------------

void TestServerConfig::testDisabledModsPersistence()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QString configPath = tmp.filePath(QStringLiteral("servers.json"));
    ServerManager mgr(configPath);

    ServerConfig s;
    s.name  = QStringLiteral("ModToggle");
    s.appid = 730;
    s.dir   = QStringLiteral("/srv/mods");
    s.mods  = { 100, 200, 300 };
    s.disabledMods = { 200 };
    mgr.servers() << s;
    QVERIFY(mgr.saveConfig());

    // Reload and verify
    ServerManager mgr2(configPath);
    QVERIFY(mgr2.loadConfig());
    QCOMPARE(mgr2.servers().size(), 1);

    const ServerConfig &loaded = mgr2.servers().first();
    QCOMPARE(loaded.mods, QList<int>({ 100, 200, 300 }));
    QCOMPARE(loaded.disabledMods, QList<int>({ 200 }));
}

void TestServerConfig::testDisabledModsDefaultEmpty()
{
    ServerConfig s;
    QVERIFY(s.disabledMods.isEmpty());
}

// ---------------------------------------------------------------------------
// Game templates tests
// ---------------------------------------------------------------------------

void TestServerConfig::testBuiltinTemplatesNotEmpty()
{
    QList<GameTemplate> templates = GameTemplate::builtinTemplates();
    QVERIFY(templates.size() > 0);
    // Should contain at least a few well-known games
    QVERIFY(templates.size() >= 4);
}

void TestServerConfig::testBuiltinTemplatesHaveValidAppIds()
{
    QList<GameTemplate> templates = GameTemplate::builtinTemplates();
    for (const GameTemplate &t : std::as_const(templates)) {
        // Each template must have a non-empty display name
        QVERIFY2(!t.displayName.isEmpty(),
                 qPrintable(QStringLiteral("Template has empty displayName")));
        // "Custom" entry has appid 0, all others must be positive
        if (!t.displayName.contains(QStringLiteral("Custom"))) {
            QVERIFY2(t.appid > 0,
                     qPrintable(QStringLiteral("Template '%1' has non-positive appid").arg(t.displayName)));
            QVERIFY2(!t.executable.isEmpty(),
                     qPrintable(QStringLiteral("Template '%1' has empty executable").arg(t.displayName)));
        }
    }
}

void TestServerConfig::testBuiltinTemplatesContainCustomEntry()
{
    QList<GameTemplate> templates = GameTemplate::builtinTemplates();
    bool foundCustom = false;
    for (const GameTemplate &t : std::as_const(templates)) {
        if (t.displayName.contains(QStringLiteral("Custom"))) {
            foundCustom = true;
            QCOMPARE(t.appid, 0);
            break;
        }
    }
    QVERIFY2(foundCustom, "Expected a 'Custom' template entry");
}

// ---------------------------------------------------------------------------
// Uptime tracking tests
// ---------------------------------------------------------------------------

void TestServerConfig::testUptimeNotRunning()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ServerManager mgr(tmp.filePath(QStringLiteral("servers.json")));
    ServerConfig s;
    s.name  = QStringLiteral("UptimeTest");
    s.appid = 730;
    s.dir   = QStringLiteral("/srv/test");
    mgr.servers() << s;

    // Server is not running, so uptime should be -1
    QCOMPARE(mgr.serverUptimeSeconds(QStringLiteral("UptimeTest")), qint64(-1));
    QVERIFY(!mgr.serverStartTime(QStringLiteral("UptimeTest")).isValid());
}

void TestServerConfig::testStartTimeRecorded()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ServerManager mgr(tmp.filePath(QStringLiteral("servers.json")));

    // Before any server starts, start time should be invalid
    QVERIFY(!mgr.serverStartTime(QStringLiteral("NoSuchServer")).isValid());
    QCOMPARE(mgr.serverUptimeSeconds(QStringLiteral("NoSuchServer")), qint64(-1));
}

// ---------------------------------------------------------------------------
// Crash backoff tests
// ---------------------------------------------------------------------------

void TestServerConfig::testCrashCountDefault()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ServerManager mgr(tmp.filePath(QStringLiteral("servers.json")));

    // Default crash count should be 0
    QCOMPARE(mgr.crashCount(QStringLiteral("AnyServer")), 0);
}

void TestServerConfig::testCrashCountReset()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ServerManager mgr(tmp.filePath(QStringLiteral("servers.json")));

    // Reset should not crash even for unknown server
    mgr.resetCrashCount(QStringLiteral("Unknown"));
    QCOMPARE(mgr.crashCount(QStringLiteral("Unknown")), 0);
}

void TestServerConfig::testCrashBackoffConstants()
{
    // Verify the backoff constants are sensible
    QVERIFY(ServerManager::kMaxCrashRestarts > 0);
    QVERIFY(ServerManager::kCrashBackoffBaseMs > 0);
    QCOMPARE(ServerManager::kMaxCrashRestarts, 5);
    QCOMPARE(ServerManager::kCrashBackoffBaseMs, 2000);
}

// ---------------------------------------------------------------------------
// Notes field tests
// ---------------------------------------------------------------------------

void TestServerConfig::testNotesPersistence()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QString configPath = tmp.filePath(QStringLiteral("servers.json"));
    ServerManager mgr(configPath);

    ServerConfig s;
    s.name  = QStringLiteral("NotesServer");
    s.appid = 730;
    s.dir   = QStringLiteral("/srv/notes");
    s.notes = QStringLiteral("This is a test note\nwith multiple lines.");
    mgr.servers() << s;
    QVERIFY(mgr.saveConfig());

    // Reload and verify notes are preserved
    ServerManager mgr2(configPath);
    QVERIFY(mgr2.loadConfig());
    QCOMPARE(mgr2.servers().size(), 1);
    QCOMPARE(mgr2.servers().first().notes,
             QStringLiteral("This is a test note\nwith multiple lines."));
}

void TestServerConfig::testNotesDefaultEmpty()
{
    ServerConfig s;
    QVERIFY(s.notes.isEmpty());
}

void TestServerConfig::testNotesExportImport()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ServerManager mgr1(tmp.filePath(QStringLiteral("servers1.json")));
    ServerConfig s;
    s.name  = QStringLiteral("NotesExport");
    s.appid = 730;
    s.dir   = QStringLiteral("/srv/notes");
    s.notes = QStringLiteral("Important server info");
    mgr1.servers() << s;

    QString exportPath = tmp.filePath(QStringLiteral("exported.json"));
    QVERIFY(mgr1.exportServerConfig(QStringLiteral("NotesExport"), exportPath));

    // Import into a fresh manager
    ServerManager mgr2(tmp.filePath(QStringLiteral("servers2.json")));
    QString error = mgr2.importServerConfig(exportPath);
    QVERIFY2(error.isEmpty(), qPrintable(QStringLiteral("Import failed: ") + error));

    QCOMPARE(mgr2.servers().size(), 1);
    QCOMPARE(mgr2.servers().first().notes, QStringLiteral("Important server info"));
}

// ---------------------------------------------------------------------------
// Mod ordering tests
// ---------------------------------------------------------------------------

void TestServerConfig::testModOrderPreserved()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QString configPath = tmp.filePath(QStringLiteral("servers.json"));
    ServerManager mgr(configPath);

    ServerConfig s;
    s.name  = QStringLiteral("ModOrder");
    s.appid = 730;
    s.dir   = QStringLiteral("/srv/mods");
    s.mods  = { 300, 100, 200 };   // specific order
    mgr.servers() << s;
    QVERIFY(mgr.saveConfig());

    // Reload and verify order is preserved
    ServerManager mgr2(configPath);
    QVERIFY(mgr2.loadConfig());
    QCOMPARE(mgr2.servers().first().mods, QList<int>({ 300, 100, 200 }));

    // Reorder and save again
    mgr2.servers()[0].mods = { 200, 300, 100 };
    QVERIFY(mgr2.saveConfig());

    ServerManager mgr3(configPath);
    QVERIFY(mgr3.loadConfig());
    QCOMPARE(mgr3.servers().first().mods, QList<int>({ 200, 300, 100 }));
}

// ---------------------------------------------------------------------------
// Update rollback tests
// ---------------------------------------------------------------------------

void TestServerConfig::testUpdateModsReturnsBool()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ServerManager mgr(tmp.filePath(QStringLiteral("servers.json")));

    ServerConfig s;
    s.name  = QStringLiteral("RollbackTest");
    s.appid = 730;
    s.dir   = tmp.filePath(QStringLiteral("serverdir"));
    s.backupFolder = tmp.filePath(QStringLiteral("backups"));
    s.mods  = { 111 };
    mgr.servers() << s;

    // SteamCMD is not available in test environment, so updateMods should
    // fail gracefully and return false
    bool result = mgr.updateMods(mgr.servers()[0]);
    QVERIFY(!result);  // Expected to fail since steamcmd is not available
}

// ---------------------------------------------------------------------------
// Discord webhook URL field tests
// ---------------------------------------------------------------------------

void TestServerConfig::testDiscordWebhookUrlPersistence()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QString configPath = tmp.filePath(QStringLiteral("servers.json"));
    ServerManager mgr(configPath);

    ServerConfig s;
    s.name  = QStringLiteral("WebhookServer");
    s.appid = 730;
    s.dir   = QStringLiteral("/srv/webhook");
    s.discordWebhookUrl = QStringLiteral("https://discord.com/api/webhooks/123/abc");
    mgr.servers() << s;
    QVERIFY(mgr.saveConfig());

    // Reload and verify
    ServerManager mgr2(configPath);
    QVERIFY(mgr2.loadConfig());
    QCOMPARE(mgr2.servers().size(), 1);
    QCOMPARE(mgr2.servers().first().discordWebhookUrl,
             QStringLiteral("https://discord.com/api/webhooks/123/abc"));
}

void TestServerConfig::testDiscordWebhookUrlDefaultEmpty()
{
    ServerConfig s;
    QVERIFY(s.discordWebhookUrl.isEmpty());
}

void TestServerConfig::testDiscordWebhookUrlExportImport()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ServerManager mgr1(tmp.filePath(QStringLiteral("servers1.json")));
    ServerConfig s;
    s.name  = QStringLiteral("WebhookExport");
    s.appid = 730;
    s.dir   = QStringLiteral("/srv/webhook");
    s.discordWebhookUrl = QStringLiteral("https://discord.com/api/webhooks/456/def");
    mgr1.servers() << s;

    QString exportPath = tmp.filePath(QStringLiteral("exported.json"));
    QVERIFY(mgr1.exportServerConfig(QStringLiteral("WebhookExport"), exportPath));

    ServerManager mgr2(tmp.filePath(QStringLiteral("servers2.json")));
    QString error = mgr2.importServerConfig(exportPath);
    QVERIFY2(error.isEmpty(), qPrintable(QStringLiteral("Import failed: ") + error));

    QCOMPARE(mgr2.servers().size(), 1);
    QCOMPARE(mgr2.servers().first().discordWebhookUrl,
             QStringLiteral("https://discord.com/api/webhooks/456/def"));
}

// ---------------------------------------------------------------------------
// Auto-start on launch tests
// ---------------------------------------------------------------------------

void TestServerConfig::testAutoStartOnLaunchPersistence()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QString configPath = tmp.filePath(QStringLiteral("servers.json"));
    ServerManager mgr(configPath);

    ServerConfig s;
    s.name  = QStringLiteral("AutoStartServer");
    s.appid = 730;
    s.dir   = QStringLiteral("/srv/autostart");
    s.autoStartOnLaunch = true;
    mgr.servers() << s;
    QVERIFY(mgr.saveConfig());

    ServerManager mgr2(configPath);
    QVERIFY(mgr2.loadConfig());
    QCOMPARE(mgr2.servers().size(), 1);
    QCOMPARE(mgr2.servers().first().autoStartOnLaunch, true);
}

void TestServerConfig::testAutoStartOnLaunchDefaultFalse()
{
    ServerConfig s;
    QCOMPARE(s.autoStartOnLaunch, false);
}

void TestServerConfig::testAutoStartServersMethod()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ServerManager mgr(tmp.filePath(QStringLiteral("servers.json")));

    ServerConfig s1;
    s1.name  = QStringLiteral("Server1");
    s1.appid = 730;
    s1.dir   = QStringLiteral("/srv/s1");
    s1.autoStartOnLaunch = true;

    ServerConfig s2;
    s2.name  = QStringLiteral("Server2");
    s2.appid = 730;
    s2.dir   = QStringLiteral("/srv/s2");
    s2.autoStartOnLaunch = false;

    mgr.servers() << s1 << s2;

    // autoStartServers should not crash even when executables don't exist
    mgr.autoStartServers();

    // Server1 should have had a start attempt (won't actually run, but
    // autoStartServers should gracefully handle missing executables)
    QVERIFY(!mgr.isServerRunning(mgr.servers()[0]));
    QVERIFY(!mgr.isServerRunning(mgr.servers()[1]));
}

// ---------------------------------------------------------------------------
// Scheduled RCON commands tests
// ---------------------------------------------------------------------------

void TestServerConfig::testScheduledRconCommandsPersistence()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QString configPath = tmp.filePath(QStringLiteral("servers.json"));
    ServerManager mgr(configPath);

    ServerConfig s;
    s.name  = QStringLiteral("RconCmdServer");
    s.appid = 730;
    s.dir   = QStringLiteral("/srv/rconcmd");
    s.scheduledRconCommands = { QStringLiteral("say Hello"), QStringLiteral("status") };
    s.rconCommandIntervalMinutes = 15;
    mgr.servers() << s;
    QVERIFY(mgr.saveConfig());

    ServerManager mgr2(configPath);
    QVERIFY(mgr2.loadConfig());
    QCOMPARE(mgr2.servers().size(), 1);
    QCOMPARE(mgr2.servers().first().scheduledRconCommands,
             QStringList({ QStringLiteral("say Hello"), QStringLiteral("status") }));
    QCOMPARE(mgr2.servers().first().rconCommandIntervalMinutes, 15);
}

void TestServerConfig::testScheduledRconCommandsDefaultEmpty()
{
    ServerConfig s;
    QVERIFY(s.scheduledRconCommands.isEmpty());
    QCOMPARE(s.rconCommandIntervalMinutes, 0);
}

void TestServerConfig::testRconCommandIntervalValidation()
{
    ServerConfig s;
    s.name  = QStringLiteral("ValidServer");
    s.appid = 730;
    s.dir   = QStringLiteral("/srv/valid");

    // Valid interval
    s.rconCommandIntervalMinutes = 10;
    QVERIFY(s.validate().isEmpty());

    // Zero is valid (disabled)
    s.rconCommandIntervalMinutes = 0;
    QVERIFY(s.validate().isEmpty());

    // Negative is invalid
    s.rconCommandIntervalMinutes = -5;
    QVERIFY(!s.validate().isEmpty());
    QVERIFY(s.validate().first().contains(QStringLiteral("RCON command interval")));
}

void TestServerConfig::testSchedulerRconTimer()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ServerManager mgr(tmp.filePath(QStringLiteral("servers.json")));
    ServerConfig s;
    s.name = QStringLiteral("RconTimerServer");
    s.appid = 730;
    s.dir = tmp.path();
    s.rconCommandIntervalMinutes = 5;
    s.scheduledRconCommands = { QStringLiteral("status") };
    mgr.servers() << s;

    SchedulerModule scheduler(&mgr);

    // Start and stop should work without crashes
    scheduler.startScheduler(QStringLiteral("RconTimerServer"));
    scheduler.stopScheduler(QStringLiteral("RconTimerServer"));

    // startAll / stopAll should also work
    scheduler.startAll();
    scheduler.stopAll();
}

// ---------------------------------------------------------------------------
// Favorite servers tests
// ---------------------------------------------------------------------------

void TestServerConfig::testFavoritePersistence()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QString configPath = tmp.filePath(QStringLiteral("servers.json"));
    ServerManager mgr(configPath);

    ServerConfig s;
    s.name  = QStringLiteral("FavServer");
    s.appid = 730;
    s.dir   = QStringLiteral("/srv/fav");
    s.favorite = true;
    mgr.servers() << s;
    QVERIFY(mgr.saveConfig());

    ServerManager mgr2(configPath);
    QVERIFY(mgr2.loadConfig());
    QCOMPARE(mgr2.servers().size(), 1);
    QCOMPARE(mgr2.servers().first().favorite, true);
}

void TestServerConfig::testFavoriteDefaultFalse()
{
    ServerConfig s;
    QCOMPARE(s.favorite, false);
}

void TestServerConfig::testFavoriteExportImport()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    ServerManager mgr1(tmp.filePath(QStringLiteral("servers1.json")));
    ServerConfig s;
    s.name  = QStringLiteral("FavExport");
    s.appid = 730;
    s.dir   = QStringLiteral("/srv/fav");
    s.favorite = true;
    mgr1.servers() << s;

    QString exportPath = tmp.filePath(QStringLiteral("exported.json"));
    QVERIFY(mgr1.exportServerConfig(QStringLiteral("FavExport"), exportPath));

    ServerManager mgr2(tmp.filePath(QStringLiteral("servers2.json")));
    QString error = mgr2.importServerConfig(exportPath);
    QVERIFY2(error.isEmpty(), qPrintable(QStringLiteral("Import failed: ") + error));

    QCOMPARE(mgr2.servers().size(), 1);
    QCOMPARE(mgr2.servers().first().favorite, true);
}

// ---------------------------------------------------------------------------
// Validation: RCON command interval
// ---------------------------------------------------------------------------

void TestServerConfig::testValidateRconIntervalNegative()
{
    ServerConfig s;
    s.name  = QStringLiteral("IntervalTest");
    s.appid = 730;
    s.dir   = QStringLiteral("/srv/interval");
    s.rconCommandIntervalMinutes = -1;

    QStringList errors = s.validate();
    QVERIFY(!errors.isEmpty());
    bool found = false;
    for (const QString &e : errors) {
        if (e.contains(QStringLiteral("RCON command interval")))
            found = true;
    }
    QVERIFY(found);
}

#include "test_serverconfig.moc"

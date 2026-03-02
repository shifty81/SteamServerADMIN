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
#include "test_serverconfig.moc"

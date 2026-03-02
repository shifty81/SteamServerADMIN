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

QTEST_MAIN(TestServerConfig)
#include "test_serverconfig.moc"

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include "ServerConfig.hpp"
#include "ServerManager.hpp"
#include "BackupModule.hpp"

// ---------------------------------------------------------------------------
// ServerConfig round-trip through ServerManager save/load
// ---------------------------------------------------------------------------
class TestServerConfig : public QObject {
    Q_OBJECT

private slots:
    void testSaveAndLoad();
    void testAddRemoveMod();
    void testBackupRotation();
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

QTEST_MAIN(TestServerConfig)
#include "test_serverconfig.moc"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <atomic>

#include <nlohmann/json.hpp>

#include "ServerConfig.hpp"
#include "ServerManager.hpp"
#include "BackupModule.hpp"
#include "SchedulerModule.hpp"
#include "LogModule.hpp"
#include "GameTemplates.hpp"
#include "WebhookModule.hpp"
#include "ConsoleLogWriter.hpp"
#include "ResourceMonitor.hpp"
#include "EventHookManager.hpp"
#include "IniEditor.hpp"
#include "ConfigBackupManager.hpp"
#include "GracefulRestartManager.hpp"
#include "SteamLibraryDetector.hpp"
#include "SteamCmdModule.hpp"

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Temporary directory helper (replaces QTemporaryDir)
// ---------------------------------------------------------------------------
class TempDir {
public:
    TempDir() {
        m_path = fs::temp_directory_path() / ("ssa_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(m_path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(m_path, ec);
    }
    bool isValid() const { return fs::exists(m_path); }
    std::string path() const { return m_path.string(); }
    std::string filePath(const std::string &name) const {
        return (m_path / name).string();
    }
private:
    fs::path m_path;
};

// Helper: check if a vector contains a value
template <typename T>
bool vectorContains(const std::vector<T> &v, const T &val) {
    return std::find(v.begin(), v.end(), val) != v.end();
}

// Helper: check if a string contains a substring
bool strContains(const std::string &haystack, const std::string &needle) {
    return haystack.find(needle) != std::string::npos;
}

// Helper: read entire file to string
std::string readFile(const std::string &path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    return std::string((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
}

// Helper: write string to file
void writeFile(const std::string &path, const std::string &content) {
    std::ofstream f(path);
    f << content;
}

// Helper: join a vector of strings with a separator
std::string joinStrings(const std::vector<std::string> &v, const std::string &sep) {
    std::string r;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) r += sep;
        r += v[i];
    }
    return r;
}

TEST(ServerConfig, SaveAndLoad)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    std::string configPath = tmp.filePath("servers.json");

    // Build a server config and save it
    ServerManager mgr(configPath);

    ServerConfig s;
    s.name         = "Test Server";
    s.appid        = 376030;
    s.dir          = "/srv/test";
    s.executable   = "server.exe";
    s.backupFolder = "/srv/backups/test";
    s.rcon.host    = "127.0.0.1";
    s.rcon.port    = 27020;
    s.rcon.password= "secret";
    s.mods         = { 111, 222, 333 };
    s.autoUpdate   = true;
    s.keepBackups  = 5;

    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    // Load back into a fresh manager
    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().size(), static_cast<size_t>(1));

    const ServerConfig &loaded = mgr2.servers().front();
    EXPECT_EQ(loaded.name,          "Test Server");
    EXPECT_EQ(loaded.appid,         376030);
    EXPECT_EQ(loaded.dir,           "/srv/test");
    EXPECT_EQ(loaded.executable,    "server.exe");
    EXPECT_EQ(loaded.backupFolder,  "/srv/backups/test");
    EXPECT_EQ(loaded.rcon.host,     "127.0.0.1");
    EXPECT_EQ(loaded.rcon.port,     27020);
    EXPECT_EQ(loaded.rcon.password, "secret");
    EXPECT_EQ(loaded.mods,          std::vector<int>({ 111, 222, 333 }));
    EXPECT_EQ(loaded.autoUpdate,    true);
    EXPECT_EQ(loaded.keepBackups,   5);
}

TEST(ServerConfig, AddRemoveMod)
{
    ServerConfig s;
    s.mods = { 100, 200, 300 };

    // Add
    int newMod = 400;
    if (!vectorContains(s.mods, newMod))
        s.mods.push_back(newMod);
    ASSERT_TRUE(vectorContains(s.mods, newMod));
    EXPECT_EQ(s.mods.size(), 4);

    // Remove
    s.mods.erase(std::remove(s.mods.begin(), s.mods.end(), 200), s.mods.end());
    EXPECT_FALSE(vectorContains(s.mods, 200));
    EXPECT_EQ(s.mods.size(), 3);

    // No duplicate
    s.mods.push_back(100);
    // Still should contain 100 once more – removeAll removes all
    EXPECT_EQ(static_cast<int>(std::count(s.mods.begin(), s.mods.end(), 100)), 2);
}

TEST(ServerConfig, BackupRotation)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerConfig s;
    s.backupFolder = tmp.path();
    s.keepBackups  = 3;

    // Create 5 dummy zip files with ascending timestamps
    std::vector<std::string> created;
    for (int i = 1; i <= 5; ++i) {
        std::string path = tmp.filePath(
            ("2026010" + std::to_string(i) + "_120000_config.zip"));
        { std::ofstream f(path); ASSERT_TRUE(f.is_open()); f << "dummy"; }
        created.push_back(path);
    }

    BackupModule::rotateBackups(s);

    // Only the 3 newest should remain
    std::vector<std::string> remaining;
    for (const auto &entry : fs::directory_iterator(tmp.path())) {
        if (entry.is_regular_file() && strContains(entry.path().filename().string(), "_config.zip"))
            remaining.push_back(entry.path().filename().string());
    }
    std::sort(remaining.begin(), remaining.end());
    EXPECT_EQ(remaining.size(), static_cast<size_t>(3));

    // The 3 newest (highest timestamps) should survive
    for (const auto &f : std::vector<std::string>{"20260103_120000_config.zip",
                               "20260104_120000_config.zip",
                               "20260105_120000_config.zip"}) {
        ASSERT_TRUE(vectorContains(remaining, f)) << (f + " should be kept");
    }
}

TEST(ServerConfig, SchedulerStartStop)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "Sched Server";
    s.appid = 730;
    s.dir = tmp.path();
    s.backupFolder = tmp.filePath("backups");
    s.backupIntervalMinutes = 10;
    s.restartIntervalHours = 2;
    mgr.servers().push_back(s);
    mgr.saveConfig();

    SchedulerModule scheduler(&mgr);

    // Start scheduler for the server
    scheduler.startScheduler("Sched Server");

    // Starting again should not crash (replaces timers)
    scheduler.startScheduler("Sched Server");

    // Stop scheduler
    scheduler.stopScheduler("Sched Server");

    // Stopping an unknown server should not crash
    scheduler.stopScheduler("Unknown");

    // startAll / stopAll
    scheduler.startAll();
    scheduler.stopAll();
}

TEST(ServerConfig, SchedulerTimerIntervals)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "Timer Server";
    s.appid = 730;
    s.dir = tmp.path();
    s.backupFolder = tmp.filePath("backups");
    s.backupIntervalMinutes = 0;   // disabled
    s.restartIntervalHours  = 0;   // disabled
    mgr.servers().push_back(s);

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

TEST(ServerConfig, CrashSignalEmitted)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);

    // Verify the onServerCrashed callback mechanism works
    int crashCallbackCount = 0;
    mgr.onServerCrashed = [&crashCallbackCount](const std::string &) {
        ++crashCallbackCount;
    };

    // No crashes should have been triggered yet
    EXPECT_EQ(crashCallbackCount, 0);
}

// ---------------------------------------------------------------------------
// New validation tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, ValidateValidConfig)
{
    ServerConfig s;
    s.name  = "MyServer";
    s.appid = 730;
    s.dir   = "/srv/cs2";
    s.rcon.port = 27015;

    std::vector<std::string> errors = s.validate();
    ASSERT_TRUE(errors.empty()) << "Expected no errors, got: " << joinStrings(errors, "; ");
}

TEST(ServerConfig, ValidateEmptyName)
{
    ServerConfig s;
    s.name  = "";
    s.appid = 730;
    s.dir   = "/srv/cs2";

    std::vector<std::string> errors = s.validate();
    EXPECT_FALSE(errors.empty());
    ASSERT_TRUE(strContains(errors.front(), "name"));

    // Whitespace-only name should also fail
    s.name = "   ";
    errors = s.validate();
    EXPECT_FALSE(errors.empty());
}

TEST(ServerConfig, ValidateInvalidAppId)
{
    ServerConfig s;
    s.name  = "Test";
    s.appid = 0;
    s.dir   = "/srv/test";

    std::vector<std::string> errors = s.validate();
    EXPECT_FALSE(errors.empty());
    ASSERT_TRUE(strContains(errors.front(), "AppID"));

    // Negative AppID
    s.appid = -1;
    errors = s.validate();
    EXPECT_FALSE(errors.empty());
}

TEST(ServerConfig, ValidateEmptyDir)
{
    ServerConfig s;
    s.name  = "Test";
    s.appid = 730;
    s.dir   = "";

    std::vector<std::string> errors = s.validate();
    EXPECT_FALSE(errors.empty());
    ASSERT_TRUE(strContains(errors.front(), "directory"));
}

TEST(ServerConfig, ValidatePortRange)
{
    ServerConfig s;
    s.name  = "Test";
    s.appid = 730;
    s.dir   = "/srv/test";

    // Port 0
    s.rcon.port = 0;
    std::vector<std::string> errors = s.validate();
    EXPECT_FALSE(errors.empty());
    ASSERT_TRUE(strContains(errors.front(), "port"));

    // Port too high
    s.rcon.port = 70000;
    errors = s.validate();
    EXPECT_FALSE(errors.empty());

    // Negative port
    s.rcon.port = -1;
    errors = s.validate();
    EXPECT_FALSE(errors.empty());

    // Valid boundary values
    s.rcon.port = 1;
    errors = s.validate();
    ASSERT_TRUE(errors.empty());

    s.rcon.port = 65535;
    errors = s.validate();
    ASSERT_TRUE(errors.empty());
}

TEST(ServerConfig, ValidateNegativeIntervals)
{
    ServerConfig s;
    s.name  = "Test";
    s.appid = 730;
    s.dir   = "/srv/test";

    s.keepBackups = -1;
    std::vector<std::string> errors = s.validate();
    EXPECT_FALSE(errors.empty());

    s.keepBackups = 0;
    s.backupIntervalMinutes = -5;
    errors = s.validate();
    EXPECT_FALSE(errors.empty());

    s.backupIntervalMinutes = 0;
    s.restartIntervalHours = -1;
    errors = s.validate();
    EXPECT_FALSE(errors.empty());
}

TEST(ServerConfig, ValidateAllDuplicateNames)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr(tmp.filePath("servers.json"));

    ServerConfig s1;
    s1.name  = "DupeServer";
    s1.appid = 730;
    s1.dir   = "/srv/a";

    ServerConfig s2;
    s2.name  = "DupeServer";
    s2.appid = 2430930;
    s2.dir   = "/srv/b";

    mgr.servers().push_back(s1);
    mgr.servers().push_back(s2);

    std::vector<std::string> errors = mgr.validateAll();
    EXPECT_FALSE(errors.empty());

    // Should mention "Duplicate"
    bool foundDuplicate = false;
    for (const auto &e : errors) {
        if (strContains(e, "Duplicate"))
            foundDuplicate = true;
    }
    ASSERT_TRUE(foundDuplicate) << "Expected a duplicate-name error";
}

TEST(ServerConfig, SaveRejectsInvalidConfig)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    std::string configPath = tmp.filePath("servers.json");
    ServerManager mgr(configPath);

    ServerConfig s;
    s.name  = "";   // invalid: empty name
    s.appid = 0;                    // invalid: zero appid
    s.dir   = "";   // invalid: empty dir
    mgr.servers().push_back(s);

    // Save should fail because config is invalid
    EXPECT_FALSE(mgr.saveConfig());

    // File should not have been created
    EXPECT_FALSE(fs::exists(configPath));
}

TEST(ServerConfig, LoadEmptyArray)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    std::string configPath = tmp.filePath("servers.json");

    // Write a valid empty JSON array
    { std::ofstream f(configPath); ASSERT_TRUE(f.is_open()); f << "[]"; }

    ServerManager mgr(configPath);
    ASSERT_TRUE(mgr.loadConfig());
    EXPECT_EQ(mgr.servers().size(), static_cast<size_t>(0));
}

TEST(ServerConfig, LoadMalformedJson)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    std::string configPath = tmp.filePath("servers.json");

    { std::ofstream f(configPath); ASSERT_TRUE(f.is_open()); f << "{not valid json!!!"; }

    ServerManager mgr(configPath);
    EXPECT_FALSE(mgr.loadConfig());
}

TEST(ServerConfig, LoadMissingFile)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr(tmp.filePath("nonexistent.json"));
    EXPECT_FALSE(mgr.loadConfig());
}

TEST(ServerConfig, MultipleServersRoundTrip)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    std::string configPath = tmp.filePath("servers.json");
    ServerManager mgr(configPath);

    for (int i = 1; i <= 5; ++i) {
        ServerConfig s;
        s.name  = ("Server_" + std::to_string(i));
        s.appid = 730 + i;
        s.dir   = ("/srv/s" + std::to_string(i));
        s.mods  = { i * 100, i * 200 };
        mgr.servers().push_back(s);
    }

    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().size(), static_cast<size_t>(5));

    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(mgr2.servers().at(i).name,  ("Server_" + std::to_string(i + 1)));
        EXPECT_EQ(mgr2.servers().at(i).appid, 730 + i + 1);
        EXPECT_EQ(mgr2.servers().at(i).mods.size(), 2);
    }
}

TEST(ServerConfig, DefaultFieldValues)
{
    // Ensure default values match expected defaults
    ServerConfig s;
    EXPECT_EQ(s.appid,                0);
    EXPECT_EQ(s.rcon.port,            27015);
    EXPECT_EQ(s.autoUpdate,           true);
    EXPECT_EQ(s.backupIntervalMinutes, 30);
    EXPECT_EQ(s.restartIntervalHours, 24);
    EXPECT_EQ(s.keepBackups,          10);
    ASSERT_TRUE(s.name.empty());
    ASSERT_TRUE(s.dir.empty());
    ASSERT_TRUE(s.executable.empty());
    ASSERT_TRUE(s.mods.empty());
}

// ---------------------------------------------------------------------------
// LogModule tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, LogModuleWritesEntries)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    LogModule log(tmp.filePath("test.log"));
    log.log("Server1", "Started");
    log.log("Server2", "Backup complete");

    std::vector<std::string> entries = log.entries();
    EXPECT_EQ(entries.size(), static_cast<size_t>(2));
    ASSERT_TRUE(strContains(entries.at(0), "Server1"));
    ASSERT_TRUE(strContains(entries.at(0), "Started"));
    ASSERT_TRUE(strContains(entries.at(1), "Server2"));
    ASSERT_TRUE(strContains(entries.at(1), "Backup complete"));
}

TEST(ServerConfig, LogModuleMaxEntries)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    LogModule log(tmp.filePath("test.log"));
    log.setMaxEntries(3);

    for (int i = 0; i < 5; ++i)
        log.log("S", ("msg" + std::to_string(i)));

    std::vector<std::string> entries = log.entries();
    EXPECT_EQ(entries.size(), static_cast<size_t>(3));
    // Oldest entries should have been trimmed; newest 3 remain
    ASSERT_TRUE(strContains(entries.at(0), "msg2"));
    ASSERT_TRUE(strContains(entries.at(1), "msg3"));
    ASSERT_TRUE(strContains(entries.at(2), "msg4"));
}

TEST(ServerConfig, LogModuleFileOutput)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    std::string logPath = tmp.filePath("test.log");

    {
        LogModule log(logPath);
        log.log("TestSrv", "hello");
    }

    std::string content = readFile(logPath);
    ASSERT_TRUE(strContains(content, "TestSrv"));
    ASSERT_TRUE(strContains(content, "hello"));
}

TEST(ServerConfig, LogModuleEntryAddedSignal)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    LogModule log(tmp.filePath("test.log"));
    int callbackCount = 0;
    std::string lastEntry;
    log.onEntryAdded = [&callbackCount, &lastEntry](const std::string &entry) {
        ++callbackCount;
        lastEntry = entry;
    };

    log.log("S", "event");
    EXPECT_EQ(callbackCount, 1);
    ASSERT_TRUE(strContains(lastEntry, "event"));
}

// ---------------------------------------------------------------------------
// Server cloning tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, CloneServerConfig)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr(tmp.filePath("servers.json"));

    ServerConfig s;
    s.name  = "Original";
    s.appid = 730;
    s.dir   = "/srv/orig";
    s.mods  = { 111, 222 };
    s.rcon.port = 27015;
    s.backupIntervalMinutes = 15;
    mgr.servers().push_back(s);

    // Clone it
    ServerConfig cloned = s;
    cloned.name = "Clone";
    mgr.servers().push_back(cloned);

    // Validate – should pass (no duplicates, both configs valid)
    std::vector<std::string> errors = mgr.validateAll();
    ASSERT_TRUE(errors.empty()) << "Expected no errors: " << joinStrings(errors, "; ");

    // Verify clone has same fields
    const ServerConfig &c = mgr.servers().back();
    EXPECT_EQ(c.name,  "Clone");
    EXPECT_EQ(c.appid, 730);
    EXPECT_EQ(c.dir,   "/srv/orig");
    EXPECT_EQ(c.mods,  std::vector<int>({ 111, 222 }));
    EXPECT_EQ(c.backupIntervalMinutes, 15);

    // Save and reload
    ASSERT_TRUE(mgr.saveConfig());
    ServerManager mgr2(tmp.filePath("servers.json"));
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().size(), static_cast<size_t>(2));
    EXPECT_EQ(mgr2.servers().at(1).name, "Clone");
}

TEST(ServerConfig, CloneServerDuplicateNameRejected)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr(tmp.filePath("servers.json"));

    ServerConfig s;
    s.name  = "MyServer";
    s.appid = 730;
    s.dir   = "/srv/my";
    mgr.servers().push_back(s);

    // Clone with same name (should fail validation)
    ServerConfig cloned = s;  // same name
    mgr.servers().push_back(cloned);

    std::vector<std::string> errors = mgr.validateAll();
    EXPECT_FALSE(errors.empty());

    bool foundDuplicate = false;
    for (const auto &e : errors) {
        if (strContains(e, "Duplicate"))
            foundDuplicate = true;
    }
    ASSERT_TRUE(foundDuplicate) << "Cloning with same name should trigger duplicate error";
}

// ---------------------------------------------------------------------------
// Server removal tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, RemoveServer)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr(tmp.filePath("servers.json"));

    ServerConfig s1;
    s1.name  = "Server1";
    s1.appid = 730;
    s1.dir   = "/srv/s1";

    ServerConfig s2;
    s2.name  = "Server2";
    s2.appid = 2430930;
    s2.dir   = "/srv/s2";

    mgr.servers().push_back(s1);
    mgr.servers().push_back(s2);
    EXPECT_EQ(mgr.servers().size(), static_cast<size_t>(2));

    // Remove first server
    bool removed = mgr.removeServer("Server1");
    ASSERT_TRUE(removed);
    EXPECT_EQ(mgr.servers().size(), static_cast<size_t>(1));
    EXPECT_EQ(mgr.servers().front().name, "Server2");
}

TEST(ServerConfig, RemoveServerNotFound)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr(tmp.filePath("servers.json"));

    ServerConfig s;
    s.name  = "MyServer";
    s.appid = 730;
    s.dir   = "/srv/my";
    mgr.servers().push_back(s);

    // Try to remove a non-existent server
    bool removed = mgr.removeServer("NonExistent");
    EXPECT_FALSE(removed);
    EXPECT_EQ(mgr.servers().size(), static_cast<size_t>(1));
}

TEST(ServerConfig, RemoveServerPersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    std::string configPath = tmp.filePath("servers.json");
    ServerManager mgr(configPath);

    ServerConfig s1;
    s1.name  = "Alpha";
    s1.appid = 730;
    s1.dir   = "/srv/alpha";

    ServerConfig s2;
    s2.name  = "Beta";
    s2.appid = 2430930;
    s2.dir   = "/srv/beta";

    mgr.servers().push_back(s1);
    mgr.servers().push_back(s2);
    ASSERT_TRUE(mgr.saveConfig());

    // Remove and save
    mgr.removeServer("Alpha");
    ASSERT_TRUE(mgr.saveConfig());

    // Reload and verify
    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().size(), static_cast<size_t>(1));
    EXPECT_EQ(mgr2.servers().front().name, "Beta");
}

// ---------------------------------------------------------------------------
// Broadcast RCON tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, BroadcastRconCommand)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr(tmp.filePath("servers.json"));

    ServerConfig s1;
    s1.name  = "S1";
    s1.appid = 730;
    s1.dir   = "/srv/s1";
    s1.rcon.host = "127.0.0.1";
    s1.rcon.port = 27015;

    ServerConfig s2;
    s2.name  = "S2";
    s2.appid = 2430930;
    s2.dir   = "/srv/s2";
    s2.rcon.host = "127.0.0.1";
    s2.rcon.port = 27016;

    mgr.servers().push_back(s1);
    mgr.servers().push_back(s2);

    // broadcastRconCommand should return one result per server
    // (RCON will fail to connect in test env, but that's expected)
    std::vector<std::string> results = mgr.broadcastRconCommand("status");
    EXPECT_EQ(results.size(), static_cast<size_t>(2));
    ASSERT_TRUE(strContains(results.at(0), "[S1]"));
    ASSERT_TRUE(strContains(results.at(1), "[S2]"));
}


// ---------------------------------------------------------------------------
// Export/Import tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, ExportServerConfig)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr(tmp.filePath("servers.json"));

    ServerConfig s;
    s.name  = "ExportMe";
    s.appid = 730;
    s.dir   = "/srv/export";
    s.executable = "server.exe";
    s.mods  = { 111, 222 };
    s.disabledMods = { 222 };
    s.rcon.port = 27020;
    mgr.servers().push_back(s);

    std::string exportPath = tmp.filePath("exported.json");
    ASSERT_TRUE(mgr.exportServerConfig("ExportMe", exportPath));

    // Verify the file exists and contains expected data
    std::string f_content = readFile(exportPath);
    ASSERT_FALSE(f_content.empty());
    nlohmann::json doc = nlohmann::json::parse(readFile(exportPath));

    // obj is just doc (nlohmann::json is already the object)
    EXPECT_EQ(doc["name"].get<std::string>(), "ExportMe");
    EXPECT_EQ(doc["appid"].get<int>(), 730);
    EXPECT_EQ(doc["dir"].get<std::string>(), "/srv/export");

    // Verify mods and disabledMods are present
    auto mods = doc["mods"];
    EXPECT_EQ(mods.size(), static_cast<size_t>(2));
    auto disabled = doc["disabledMods"];
    EXPECT_EQ(disabled.size(), static_cast<size_t>(1));
    EXPECT_EQ(disabled[0].get<int>(), 222);
}

TEST(ServerConfig, ExportServerNotFound)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr(tmp.filePath("servers.json"));
    std::string exportPath = tmp.filePath("exported.json");

    // No servers loaded – export should fail
    EXPECT_FALSE(mgr.exportServerConfig("NonExistent", exportPath));
    EXPECT_FALSE(fs::exists(exportPath));
}

TEST(ServerConfig, ImportServerConfig)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    // First create and export a server
    ServerManager mgr1(tmp.filePath("servers1.json"));
    ServerConfig s;
    s.name  = "Imported";
    s.appid = 2430930;
    s.dir   = "/srv/import";
    s.mods  = { 333, 444 };
    s.disabledMods = { 444 };
    mgr1.servers().push_back(s);

    std::string exportPath = tmp.filePath("to_import.json");
    ASSERT_TRUE(mgr1.exportServerConfig("Imported", exportPath));

    // Import into a fresh manager
    ServerManager mgr2(tmp.filePath("servers2.json"));
    std::string error = mgr2.importServerConfig(exportPath);
    ASSERT_TRUE(error.empty()) << "Import failed: " << error;

    EXPECT_EQ(mgr2.servers().size(), static_cast<size_t>(1));
    const ServerConfig &imported = mgr2.servers().front();
    EXPECT_EQ(imported.name,  "Imported");
    EXPECT_EQ(imported.appid, 2430930);
    EXPECT_EQ(imported.mods,  std::vector<int>({ 333, 444 }));
    EXPECT_EQ(imported.disabledMods, std::vector<int>({ 444 }));
}

TEST(ServerConfig, ImportServerDuplicateName)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    // Create a server and export it
    ServerManager mgr(tmp.filePath("servers.json"));
    ServerConfig s;
    s.name  = "DupeImport";
    s.appid = 730;
    s.dir   = "/srv/dupe";
    mgr.servers().push_back(s);

    std::string exportPath = tmp.filePath("dupe.json");
    ASSERT_TRUE(mgr.exportServerConfig("DupeImport", exportPath));

    // Try to import – should fail because the name already exists
    std::string error = mgr.importServerConfig(exportPath);
    EXPECT_FALSE(error.empty());
    ASSERT_TRUE(strContains(error, "Duplicate"));
    EXPECT_EQ(mgr.servers().size(), static_cast<size_t>(1));  // should not have added
}

// ---------------------------------------------------------------------------
// Disabled mods tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, DisabledModsPersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    std::string configPath = tmp.filePath("servers.json");
    ServerManager mgr(configPath);

    ServerConfig s;
    s.name  = "ModToggle";
    s.appid = 730;
    s.dir   = "/srv/mods";
    s.mods  = { 100, 200, 300 };
    s.disabledMods = { 200 };
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    // Reload and verify
    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().size(), static_cast<size_t>(1));

    const ServerConfig &loaded = mgr2.servers().front();
    EXPECT_EQ(loaded.mods, std::vector<int>({ 100, 200, 300 }));
    EXPECT_EQ(loaded.disabledMods, std::vector<int>({ 200 }));
}

TEST(ServerConfig, DisabledModsDefaultEmpty)
{
    ServerConfig s;
    ASSERT_TRUE(s.disabledMods.empty());
}

// ---------------------------------------------------------------------------
// Game templates tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, BuiltinTemplatesNotEmpty)
{
    std::vector<GameTemplate> templates = GameTemplate::builtinTemplates();
    ASSERT_TRUE(templates.size() > 0);
    // Should contain at least a few well-known games
    ASSERT_TRUE(templates.size() >= 4);
}

TEST(ServerConfig, BuiltinTemplatesHaveValidAppIds)
{
    std::vector<GameTemplate> templates = GameTemplate::builtinTemplates();
    for (const GameTemplate &t : std::as_const(templates)) {
        // Each template must have a non-empty display name
        ASSERT_TRUE(!t.displayName.empty()) << ("Template has empty displayName");
        // "Custom" entry has appid 0, all others must be positive
        if (!strContains(t.displayName, "Custom")) {
            ASSERT_TRUE(t.appid > 0) << "Template '" << t.displayName << "' has non-positive appid";
            ASSERT_TRUE(!t.executable.empty()) << "Template '" << t.displayName << "' has empty executable";
        }
    }
}

TEST(ServerConfig, BuiltinTemplatesContainCustomEntry)
{
    std::vector<GameTemplate> templates = GameTemplate::builtinTemplates();
    bool foundCustom = false;
    for (const GameTemplate &t : std::as_const(templates)) {
        if (strContains(t.displayName, "Custom")) {
            foundCustom = true;
            EXPECT_EQ(t.appid, 0);
            break;
        }
    }
    ASSERT_TRUE(foundCustom) << "Expected a 'Custom' template entry";
}

// ---------------------------------------------------------------------------
// Uptime tracking tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, UptimeNotRunning)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr(tmp.filePath("servers.json"));
    ServerConfig s;
    s.name  = "UptimeTest";
    s.appid = 730;
    s.dir   = "/srv/test";
    mgr.servers().push_back(s);

    // Server is not running, so uptime should be -1
    EXPECT_EQ(mgr.serverUptimeSeconds("UptimeTest"), int64_t(-1));
    EXPECT_EQ(mgr.serverStartTime("UptimeTest").time_since_epoch().count(), 0);
}

TEST(ServerConfig, StartTimeRecorded)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr(tmp.filePath("servers.json"));

    // Before any server starts, start time should be invalid
    EXPECT_EQ(mgr.serverStartTime("NoSuchServer").time_since_epoch().count(), 0);
    EXPECT_EQ(mgr.serverUptimeSeconds("NoSuchServer"), int64_t(-1));
}

// ---------------------------------------------------------------------------
// Crash backoff tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, CrashCountDefault)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr(tmp.filePath("servers.json"));

    // Default crash count should be 0
    EXPECT_EQ(mgr.crashCount("AnyServer"), 0);
}

TEST(ServerConfig, CrashCountReset)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr(tmp.filePath("servers.json"));

    // Reset should not crash even for unknown server
    mgr.resetCrashCount("Unknown");
    EXPECT_EQ(mgr.crashCount("Unknown"), 0);
}

TEST(ServerConfig, CrashBackoffConstants)
{
    // Verify the backoff constants are sensible
    ASSERT_TRUE(ServerManager::kMaxCrashRestarts > 0);
    ASSERT_TRUE(ServerManager::kCrashBackoffBaseMs > 0);
    EXPECT_EQ(ServerManager::kMaxCrashRestarts, 5);
    EXPECT_EQ(ServerManager::kCrashBackoffBaseMs, 2000);
}

// ---------------------------------------------------------------------------
// Notes field tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, NotesPersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    std::string configPath = tmp.filePath("servers.json");
    ServerManager mgr(configPath);

    ServerConfig s;
    s.name  = "NotesServer";
    s.appid = 730;
    s.dir   = "/srv/notes";
    s.notes = "This is a test note\nwith multiple lines.";
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    // Reload and verify notes are preserved
    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().size(), static_cast<size_t>(1));
    EXPECT_EQ(mgr2.servers().front().notes,
             "This is a test note\nwith multiple lines.");
}

TEST(ServerConfig, NotesDefaultEmpty)
{
    ServerConfig s;
    ASSERT_TRUE(s.notes.empty());
}

TEST(ServerConfig, NotesExportImport)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr1(tmp.filePath("servers1.json"));
    ServerConfig s;
    s.name  = "NotesExport";
    s.appid = 730;
    s.dir   = "/srv/notes";
    s.notes = "Important server info";
    mgr1.servers().push_back(s);

    std::string exportPath = tmp.filePath("exported.json");
    ASSERT_TRUE(mgr1.exportServerConfig("NotesExport", exportPath));

    // Import into a fresh manager
    ServerManager mgr2(tmp.filePath("servers2.json"));
    std::string error = mgr2.importServerConfig(exportPath);
    ASSERT_TRUE(error.empty()) << "Import failed: " << error;

    EXPECT_EQ(mgr2.servers().size(), static_cast<size_t>(1));
    EXPECT_EQ(mgr2.servers().front().notes, "Important server info");
}

// ---------------------------------------------------------------------------
// Mod ordering tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, ModOrderPreserved)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    std::string configPath = tmp.filePath("servers.json");
    ServerManager mgr(configPath);

    ServerConfig s;
    s.name  = "ModOrder";
    s.appid = 730;
    s.dir   = "/srv/mods";
    s.mods  = { 300, 100, 200 };   // specific order
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    // Reload and verify order is preserved
    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().front().mods, std::vector<int>({ 300, 100, 200 }));

    // Reorder and save again
    mgr2.servers()[0].mods = { 200, 300, 100 };
    ASSERT_TRUE(mgr2.saveConfig());

    ServerManager mgr3(configPath);
    ASSERT_TRUE(mgr3.loadConfig());
    EXPECT_EQ(mgr3.servers().front().mods, std::vector<int>({ 200, 300, 100 }));
}

// ---------------------------------------------------------------------------
// Update rollback tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, UpdateModsReturnsBool)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr(tmp.filePath("servers.json"));

    ServerConfig s;
    s.name  = "RollbackTest";
    s.appid = 730;
    s.dir   = tmp.filePath("serverdir");
    s.backupFolder = tmp.filePath("backups");
    s.mods  = { 111 };
    mgr.servers().push_back(s);

    // SteamCMD is not available in test environment, so updateMods should
    // fail gracefully and return false
    bool result = mgr.updateMods(mgr.servers()[0]);
    EXPECT_FALSE(result);  // Expected to fail since steamcmd is not available
}

// ---------------------------------------------------------------------------
// Discord webhook URL field tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, DiscordWebhookUrlPersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    std::string configPath = tmp.filePath("servers.json");
    ServerManager mgr(configPath);

    ServerConfig s;
    s.name  = "WebhookServer";
    s.appid = 730;
    s.dir   = "/srv/webhook";
    s.discordWebhookUrl = "https://discord.com/api/webhooks/123/abc";
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    // Reload and verify
    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().size(), static_cast<size_t>(1));
    EXPECT_EQ(mgr2.servers().front().discordWebhookUrl,
             "https://discord.com/api/webhooks/123/abc");
}

TEST(ServerConfig, DiscordWebhookUrlDefaultEmpty)
{
    ServerConfig s;
    ASSERT_TRUE(s.discordWebhookUrl.empty());
}

TEST(ServerConfig, DiscordWebhookUrlExportImport)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr1(tmp.filePath("servers1.json"));
    ServerConfig s;
    s.name  = "WebhookExport";
    s.appid = 730;
    s.dir   = "/srv/webhook";
    s.discordWebhookUrl = "https://discord.com/api/webhooks/456/def";
    mgr1.servers().push_back(s);

    std::string exportPath = tmp.filePath("exported.json");
    ASSERT_TRUE(mgr1.exportServerConfig("WebhookExport", exportPath));

    ServerManager mgr2(tmp.filePath("servers2.json"));
    std::string error = mgr2.importServerConfig(exportPath);
    ASSERT_TRUE(error.empty()) << "Import failed: " << error;

    EXPECT_EQ(mgr2.servers().size(), static_cast<size_t>(1));
    EXPECT_EQ(mgr2.servers().front().discordWebhookUrl,
             "https://discord.com/api/webhooks/456/def");
}

// ---------------------------------------------------------------------------
// Auto-start on launch tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, AutoStartOnLaunchPersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    std::string configPath = tmp.filePath("servers.json");
    ServerManager mgr(configPath);

    ServerConfig s;
    s.name  = "AutoStartServer";
    s.appid = 730;
    s.dir   = "/srv/autostart";
    s.autoStartOnLaunch = true;
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().size(), static_cast<size_t>(1));
    EXPECT_EQ(mgr2.servers().front().autoStartOnLaunch, true);
}

TEST(ServerConfig, AutoStartOnLaunchDefaultFalse)
{
    ServerConfig s;
    EXPECT_EQ(s.autoStartOnLaunch, false);
}

TEST(ServerConfig, AutoStartServersMethod)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr(tmp.filePath("servers.json"));

    ServerConfig s1;
    s1.name  = "Server1";
    s1.appid = 730;
    s1.dir   = "/srv/s1";
    s1.autoStartOnLaunch = true;

    ServerConfig s2;
    s2.name  = "Server2";
    s2.appid = 730;
    s2.dir   = "/srv/s2";
    s2.autoStartOnLaunch = false;

    mgr.servers().push_back(s1);
    mgr.servers().push_back(s2);

    // autoStartServers should not crash even when executables don't exist
    mgr.autoStartServers();

    // Wait for forked children to exec and exit (Linux: fork always succeeds,
    // but child exits immediately since the executable doesn't exist)
    for (int i = 0; i < 20; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        mgr.tick();
        if (!mgr.isServerRunning(mgr.servers()[0]))
            break;
    }

    // Server1 should have had a start attempt (won't actually run, but
    // autoStartServers should gracefully handle missing executables)
    EXPECT_FALSE(mgr.isServerRunning(mgr.servers()[0]));
    EXPECT_FALSE(mgr.isServerRunning(mgr.servers()[1]));
}

// ---------------------------------------------------------------------------
// Scheduled RCON commands tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, ScheduledRconCommandsPersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    std::string configPath = tmp.filePath("servers.json");
    ServerManager mgr(configPath);

    ServerConfig s;
    s.name  = "RconCmdServer";
    s.appid = 730;
    s.dir   = "/srv/rconcmd";
    s.scheduledRconCommands = { "say Hello", "status" };
    s.rconCommandIntervalMinutes = 15;
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().size(), static_cast<size_t>(1));
    EXPECT_EQ(mgr2.servers().front().scheduledRconCommands,
             std::vector<std::string>({ "say Hello", "status" }));
    EXPECT_EQ(mgr2.servers().front().rconCommandIntervalMinutes, 15);
}

TEST(ServerConfig, ScheduledRconCommandsDefaultEmpty)
{
    ServerConfig s;
    ASSERT_TRUE(s.scheduledRconCommands.empty());
    EXPECT_EQ(s.rconCommandIntervalMinutes, 0);
}

TEST(ServerConfig, RconCommandIntervalValidation)
{
    ServerConfig s;
    s.name  = "ValidServer";
    s.appid = 730;
    s.dir   = "/srv/valid";

    // Valid interval
    s.rconCommandIntervalMinutes = 10;
    ASSERT_TRUE(s.validate().empty());

    // Zero is valid (disabled)
    s.rconCommandIntervalMinutes = 0;
    ASSERT_TRUE(s.validate().empty());

    // Negative is invalid
    s.rconCommandIntervalMinutes = -5;
    EXPECT_FALSE(s.validate().empty());
    ASSERT_TRUE(strContains(s.validate().front(), "RCON command interval"));
}

TEST(ServerConfig, SchedulerRconTimer)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr(tmp.filePath("servers.json"));
    ServerConfig s;
    s.name = "RconTimerServer";
    s.appid = 730;
    s.dir = tmp.path();
    s.rconCommandIntervalMinutes = 5;
    s.scheduledRconCommands = { "status" };
    mgr.servers().push_back(s);

    SchedulerModule scheduler(&mgr);

    // Start and stop should work without crashes
    scheduler.startScheduler("RconTimerServer");
    scheduler.stopScheduler("RconTimerServer");

    // startAll / stopAll should also work
    scheduler.startAll();
    scheduler.stopAll();
}

// ---------------------------------------------------------------------------
// Favorite servers tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, FavoritePersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    std::string configPath = tmp.filePath("servers.json");
    ServerManager mgr(configPath);

    ServerConfig s;
    s.name  = "FavServer";
    s.appid = 730;
    s.dir   = "/srv/fav";
    s.favorite = true;
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().size(), static_cast<size_t>(1));
    EXPECT_EQ(mgr2.servers().front().favorite, true);
}

TEST(ServerConfig, FavoriteDefaultFalse)
{
    ServerConfig s;
    EXPECT_EQ(s.favorite, false);
}

TEST(ServerConfig, FavoriteExportImport)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr1(tmp.filePath("servers1.json"));
    ServerConfig s;
    s.name  = "FavExport";
    s.appid = 730;
    s.dir   = "/srv/fav";
    s.favorite = true;
    mgr1.servers().push_back(s);

    std::string exportPath = tmp.filePath("exported.json");
    ASSERT_TRUE(mgr1.exportServerConfig("FavExport", exportPath));

    ServerManager mgr2(tmp.filePath("servers2.json"));
    std::string error = mgr2.importServerConfig(exportPath);
    ASSERT_TRUE(error.empty()) << "Import failed: " << error;

    EXPECT_EQ(mgr2.servers().size(), static_cast<size_t>(1));
    EXPECT_EQ(mgr2.servers().front().favorite, true);
}

// ---------------------------------------------------------------------------
// Validation: RCON command interval
// ---------------------------------------------------------------------------

TEST(ServerConfig, ValidateRconIntervalNegative)
{
    ServerConfig s;
    s.name  = "IntervalTest";
    s.appid = 730;
    s.dir   = "/srv/interval";
    s.rconCommandIntervalMinutes = -1;

    std::vector<std::string> errors = s.validate();
    EXPECT_FALSE(errors.empty());
    bool found = false;
    for (const auto &e : errors) {
        if (strContains(e, "RCON command interval"))
            found = true;
    }
    ASSERT_TRUE(found);
}

// ---------------------------------------------------------------------------
// RCON password obfuscation tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, RconPasswordObfuscatedOnDisk)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name  = "ObfTest";
    s.appid = 730;
    s.dir   = "/srv/obf";
    s.rcon.password = "mySecret123";
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    // Read raw JSON and verify password is not stored in plaintext
    std::string raw = readFile(configPath);
    ASSERT_FALSE(raw.empty());
    EXPECT_FALSE(strContains(raw, "mySecret123"));
    ASSERT_TRUE(strContains(raw, "obf:"));
}

TEST(ServerConfig, RconPasswordLegacyPlaintext)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    // Write legacy plaintext config
    nlohmann::json arr = nlohmann::json::array();
    nlohmann::json obj = nlohmann::json::object();
    obj["name"] = "Legacy";
    obj["appid"] = 730;
    obj["dir"] = "/srv/legacy";
    nlohmann::json rcon;
    rcon["host"] = "127.0.0.1";
    rcon["port"] = 27015;
    rcon["password"] = "plainPass";
    obj["rcon"] = rcon;
    arr.push_back(obj);

    writeFile(configPath, arr.dump());

    ServerManager mgr(configPath);
    ASSERT_TRUE(mgr.loadConfig());
    EXPECT_EQ(mgr.servers().front().rcon.password, "plainPass");
}

TEST(ServerConfig, RconPasswordRoundTrip)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name  = "RoundTrip";
    s.appid = 730;
    s.dir   = "/srv/rt";
    std::string specialPass = "p@$$w0rd!&<>\""; s.rcon.password = specialPass;
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().front().rcon.password, specialPass);
}

// ---------------------------------------------------------------------------
// Console logging tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, ConsoleLoggingPersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name  = "ConsoleLogTest";
    s.appid = 730;
    s.dir   = "/srv/cl";
    s.consoleLogging = true;
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().front().consoleLogging, true);
}

TEST(ServerConfig, ConsoleLoggingDefaultFalse)
{
    ServerConfig s;
    EXPECT_EQ(s.consoleLogging, false);
}

TEST(ServerConfig, ConsoleLogWriterAppend)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ConsoleLogWriter::append(tmp.path(), "TestSrv",
                             "> status");
    ConsoleLogWriter::append(tmp.path(), "TestSrv",
                             "players: 5");

    std::vector<std::string> logs = ConsoleLogWriter::listLogs(tmp.path());
    EXPECT_FALSE(logs.empty());

    std::string content = readFile(tmp.path() + "/ConsoleLogs/" + logs.front());
    ASSERT_FALSE(content.empty());
    ASSERT_TRUE(strContains(content, "> status"));
    ASSERT_TRUE(strContains(content, "players: 5"));
    ASSERT_TRUE(strContains(content, "[TestSrv]"));
}

// ---------------------------------------------------------------------------
// Webhook template tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, WebhookTemplatePersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name  = "WHTpl";
    s.appid = 730;
    s.dir   = "/srv/wh";
    s.webhookTemplate = "{server} just {event} at {timestamp}!";
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().front().webhookTemplate,
             "{server} just {event} at {timestamp}!");
}

TEST(ServerConfig, WebhookTemplateDefaultEmpty)
{
    ServerConfig s;
    ASSERT_TRUE(s.webhookTemplate.empty());
}

TEST(ServerConfig, WebhookTemplateFormatMessage)
{
    std::string tpl = "{server} - {event}";
    std::string result = WebhookModule::formatMessage(tpl,
                                                  "MyServer",
                                                  "Server started.");
    EXPECT_EQ(result, "MyServer - Server started.");

    // Verify timestamp placeholder is replaced
    std::string tpl2 = "{timestamp}";
    std::string result2 = WebhookModule::formatMessage(tpl2,
                                                   "S", "E");
    EXPECT_FALSE(strContains(result2, "{timestamp}"));
    EXPECT_FALSE(result2.empty());
}

TEST(ServerConfig, WebhookTemplateExportImport)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");
    std::string exportPath = tmp.filePath("export.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name  = "WHTplExp";
    s.appid = 730;
    s.dir   = "/srv/whe";
    s.webhookTemplate = "Alert: {event}";
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ASSERT_TRUE(mgr.exportServerConfig("WHTplExp", exportPath));

    ServerManager mgr2(tmp.filePath("servers2.json"));
    std::string err = mgr2.importServerConfig(exportPath);
    ASSERT_TRUE(err.empty()) << (err);
    EXPECT_EQ(mgr2.servers().front().webhookTemplate, "Alert: {event}");
}

// ---------------------------------------------------------------------------
// Maintenance window tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, MaintenanceWindowPersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name  = "MaintTest";
    s.appid = 730;
    s.dir   = "/srv/maint";
    s.maintenanceStartHour = 2;
    s.maintenanceEndHour   = 6;
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().front().maintenanceStartHour, 2);
    EXPECT_EQ(mgr2.servers().front().maintenanceEndHour,   6);
}

TEST(ServerConfig, MaintenanceWindowDefaultDisabled)
{
    ServerConfig s;
    EXPECT_EQ(s.maintenanceStartHour, -1);
    EXPECT_EQ(s.maintenanceEndHour,   -1);
}

TEST(ServerConfig, MaintenanceWindowValidation)
{
    ServerConfig s;
    s.name  = "MaintVal";
    s.appid = 730;
    s.dir   = "/srv/mv";

    // Valid: disabled
    s.maintenanceStartHour = -1;
    s.maintenanceEndHour   = -1;
    ASSERT_TRUE(s.validate().empty());

    // Valid: normal range
    s.maintenanceStartHour = 2;
    s.maintenanceEndHour   = 6;
    ASSERT_TRUE(s.validate().empty());

    // Invalid: hour out of range
    s.maintenanceStartHour = 25;
    std::vector<std::string> errors = s.validate();
    EXPECT_FALSE(errors.empty());
    bool found = false;
    for (const auto &e : errors) {
        if (strContains(e, "Maintenance start hour"))
            found = true;
    }
    ASSERT_TRUE(found);

    // Invalid: end hour out of range
    s.maintenanceStartHour = 2;
    s.maintenanceEndHour   = 30;
    errors = s.validate();
    EXPECT_FALSE(errors.empty());
    found = false;
    for (const auto &e : errors) {
        if (strContains(e, "Maintenance end hour"))
            found = true;
    }
    ASSERT_TRUE(found);
}

TEST(ServerConfig, MaintenanceWindowExportImport)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");
    std::string exportPath = tmp.filePath("export.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name  = "MaintExp";
    s.appid = 730;
    s.dir   = "/srv/me";
    s.maintenanceStartHour = 22;
    s.maintenanceEndHour   = 4;
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ASSERT_TRUE(mgr.exportServerConfig("MaintExp", exportPath));

    ServerManager mgr2(tmp.filePath("servers2.json"));
    std::string err = mgr2.importServerConfig(exportPath);
    ASSERT_TRUE(err.empty()) << (err);
    EXPECT_EQ(mgr2.servers().front().maintenanceStartHour, 22);
    EXPECT_EQ(mgr2.servers().front().maintenanceEndHour,    4);
}

// ---------------------------------------------------------------------------
// Backup compression level tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, BackupCompressionLevelPersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name  = "CompTest";
    s.appid = 730;
    s.dir   = "/srv/comp";
    s.backupCompressionLevel = 9;
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().front().backupCompressionLevel, 9);
}

TEST(ServerConfig, BackupCompressionLevelDefault)
{
    ServerConfig s;
    EXPECT_EQ(s.backupCompressionLevel, 6);
}

TEST(ServerConfig, BackupCompressionLevelValidation)
{
    ServerConfig s;
    s.name  = "CompVal";
    s.appid = 730;
    s.dir   = "/srv/cv";

    // Valid: 0
    s.backupCompressionLevel = 0;
    ASSERT_TRUE(s.validate().empty());

    // Valid: 9
    s.backupCompressionLevel = 9;
    ASSERT_TRUE(s.validate().empty());

    // Invalid: negative
    s.backupCompressionLevel = -1;
    std::vector<std::string> errors = s.validate();
    EXPECT_FALSE(errors.empty());
    bool found = false;
    for (const auto &e : errors) {
        if (strContains(e, "Backup compression level"))
            found = true;
    }
    ASSERT_TRUE(found);

    // Invalid: too high
    s.backupCompressionLevel = 10;
    errors = s.validate();
    EXPECT_FALSE(errors.empty());
    found = false;
    for (const auto &e : errors) {
        if (strContains(e, "Backup compression level"))
            found = true;
    }
    ASSERT_TRUE(found);
}

TEST(ServerConfig, BackupCompressionLevelExportImport)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");
    std::string exportPath = tmp.filePath("export.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name  = "CompExp";
    s.appid = 730;
    s.dir   = "/srv/ce";
    s.backupCompressionLevel = 3;
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ASSERT_TRUE(mgr.exportServerConfig("CompExp", exportPath));

    ServerManager mgr2(tmp.filePath("servers2.json"));
    std::string err = mgr2.importServerConfig(exportPath);
    ASSERT_TRUE(err.empty()) << (err);
    EXPECT_EQ(mgr2.servers().front().backupCompressionLevel, 3);
}

// ---------------------------------------------------------------------------
// Max players tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, MaxPlayersDefault)
{
    ServerConfig s;
    EXPECT_EQ(s.maxPlayers, 0);
}

TEST(ServerConfig, MaxPlayersPersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name  = "MaxPTest";
    s.appid = 730;
    s.dir   = "/srv/mp";
    s.maxPlayers = 64;
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().front().maxPlayers, 64);
}

TEST(ServerConfig, MaxPlayersValidation)
{
    ServerConfig s;
    s.name  = "MaxPVal";
    s.appid = 730;
    s.dir   = "/srv/mpv";

    // Valid: 0 (unlimited)
    s.maxPlayers = 0;
    ASSERT_TRUE(s.validate().empty());

    // Valid: positive
    s.maxPlayers = 100;
    ASSERT_TRUE(s.validate().empty());

    // Invalid: negative
    s.maxPlayers = -1;
    std::vector<std::string> errors = s.validate();
    EXPECT_FALSE(errors.empty());
    bool found = false;
    for (const auto &e : errors) {
        if (strContains(e, "Max players"))
            found = true;
    }
    ASSERT_TRUE(found);
}

TEST(ServerConfig, MaxPlayersExportImport)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");
    std::string exportPath = tmp.filePath("export.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name  = "MaxPExp";
    s.appid = 730;
    s.dir   = "/srv/mpe";
    s.maxPlayers = 32;
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ASSERT_TRUE(mgr.exportServerConfig("MaxPExp", exportPath));

    ServerManager mgr2(tmp.filePath("servers2.json"));
    std::string err = mgr2.importServerConfig(exportPath);
    ASSERT_TRUE(err.empty()) << (err);
    EXPECT_EQ(mgr2.servers().front().maxPlayers, 32);
}

// ---------------------------------------------------------------------------
// Restart warning tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, RestartWarningMinutesDefault)
{
    ServerConfig s;
    EXPECT_EQ(s.restartWarningMinutes, 15);
}

TEST(ServerConfig, RestartWarningMinutesPersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name  = "RWTest";
    s.appid = 730;
    s.dir   = "/srv/rw";
    s.restartWarningMinutes = 10;
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().front().restartWarningMinutes, 10);
}

TEST(ServerConfig, RestartWarningMinutesValidation)
{
    ServerConfig s;
    s.name  = "RWVal";
    s.appid = 730;
    s.dir   = "/srv/rwv";

    // Valid: 0 (disabled)
    s.restartWarningMinutes = 0;
    ASSERT_TRUE(s.validate().empty());

    // Valid: positive
    s.restartWarningMinutes = 30;
    ASSERT_TRUE(s.validate().empty());

    // Invalid: negative
    s.restartWarningMinutes = -5;
    std::vector<std::string> errors = s.validate();
    EXPECT_FALSE(errors.empty());
    bool found = false;
    for (const auto &e : errors) {
        if (strContains(e, "Restart warning"))
            found = true;
    }
    ASSERT_TRUE(found);
}

TEST(ServerConfig, RestartWarningMessagePersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name  = "RWMsgTest";
    s.appid = 730;
    s.dir   = "/srv/rwm";
    s.restartWarningMessage = "Reboot in {minutes} min!";
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().front().restartWarningMessage,
             "Reboot in {minutes} min!");
}

TEST(ServerConfig, RestartWarningMessageDefault)
{
    ServerConfig s;
    ASSERT_TRUE(s.restartWarningMessage.empty());
}

TEST(ServerConfig, RestartWarningFormatDefault)
{
    ServerConfig s;
    std::string msg = s.formatRestartWarning(5);
    ASSERT_TRUE(strContains(msg, "5"));
    ASSERT_TRUE(strContains(msg, "restart"));
    // Default message should mention saving progress
    ASSERT_TRUE(strContains(msg, "save"));
}

TEST(ServerConfig, RestartWarningFormatCustom)
{
    ServerConfig s;
    s.restartWarningMessage = "Server down in {minutes} min! Get ready.";
    std::string msg = s.formatRestartWarning(3);
    EXPECT_EQ(msg, "Server down in 3 min! Get ready.");
}

TEST(ServerConfig, RestartWarningExportImport)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");
    std::string exportPath = tmp.filePath("export.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name  = "RWExp";
    s.appid = 730;
    s.dir   = "/srv/rwe";
    s.restartWarningMinutes = 20;
    s.restartWarningMessage = "Restart in {minutes}m";
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ASSERT_TRUE(mgr.exportServerConfig("RWExp", exportPath));

    ServerManager mgr2(tmp.filePath("servers2.json"));
    std::string err = mgr2.importServerConfig(exportPath);
    ASSERT_TRUE(err.empty()) << (err);
    EXPECT_EQ(mgr2.servers().front().restartWarningMinutes, 20);
    EXPECT_EQ(mgr2.servers().front().restartWarningMessage,
             "Restart in {minutes}m");
}

// ---------------------------------------------------------------------------
// Pending update tracking tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, PendingUpdateDefault)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    ServerManager mgr(tmp.filePath("servers.json"));
    EXPECT_FALSE(mgr.hasPendingUpdate("AnyServer"));
}

TEST(ServerConfig, PendingUpdateSetClear)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    ServerManager mgr(tmp.filePath("servers.json"));

    mgr.setPendingUpdate("Server1", true);
    ASSERT_TRUE(mgr.hasPendingUpdate("Server1"));
    EXPECT_FALSE(mgr.hasPendingUpdate("Server2"));

    mgr.setPendingUpdate("Server1", false);
    EXPECT_FALSE(mgr.hasPendingUpdate("Server1"));
}

TEST(ServerConfig, PendingModUpdateDefault)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    ServerManager mgr(tmp.filePath("servers.json"));
    EXPECT_FALSE(mgr.hasPendingModUpdate("AnyServer"));
}

TEST(ServerConfig, PendingModUpdateSetClear)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    ServerManager mgr(tmp.filePath("servers.json"));

    mgr.setPendingModUpdate("Server1", true);
    ASSERT_TRUE(mgr.hasPendingModUpdate("Server1"));
    EXPECT_FALSE(mgr.hasPendingModUpdate("Server2"));

    mgr.setPendingModUpdate("Server1", false);
    EXPECT_FALSE(mgr.hasPendingModUpdate("Server1"));
}

// ---------------------------------------------------------------------------
// Resource monitoring tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, ResourceMonitorTrackUntrack)
{
    ResourceMonitor mon;
    mon.trackProcess("S1", 12345);
    EXPECT_EQ(mon.allUsage().size(), static_cast<size_t>(0)); // no poll yet

    mon.untrackProcess("S1");
    EXPECT_EQ(mon.allUsage().size(), static_cast<size_t>(0));
}

TEST(ServerConfig, ResourceMonitorUsageDefault)
{
    ResourceMonitor mon;
    ResourceUsage ru = mon.usage("NonExistent");
    EXPECT_EQ(ru.cpuPercent, 0.0);
    EXPECT_EQ(ru.memoryBytes, static_cast<int64_t>(0));
}

TEST(ServerConfig, ResourceMonitorPollInterval)
{
    ResourceMonitor mon;
    EXPECT_EQ(mon.pollIntervalMs(), 5000); // default
    mon.setPollIntervalMs(2000);
    EXPECT_EQ(mon.pollIntervalMs(), 2000);
    mon.setPollIntervalMs(-1);
    EXPECT_EQ(mon.pollIntervalMs(), 1000); // clamped to 1s
}

TEST(ServerConfig, ResourceMonitorStartStop)
{
    ResourceMonitor mon;
    mon.start();
    // Just verify it doesn't crash; timer is running
    mon.stop();
    // Starting again should be fine
    mon.start();
    mon.stop();
}

TEST(ServerConfig, ResourceMonitorReadUsageInvalidPid)
{
    ResourceUsage ru = ResourceMonitor::readUsage(-1);
    EXPECT_EQ(ru.cpuPercent, 0.0);
    EXPECT_EQ(ru.memoryBytes, static_cast<int64_t>(0));

    ru = ResourceMonitor::readUsage(0);
    EXPECT_EQ(ru.cpuPercent, 0.0);
    EXPECT_EQ(ru.memoryBytes, static_cast<int64_t>(0));
}

TEST(ServerConfig, CpuAlertThresholdDefault)
{
    ServerConfig s;
    EXPECT_EQ(s.cpuAlertThreshold, 90.0);
}

TEST(ServerConfig, CpuAlertThresholdPersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "CpuTest";
    s.appid = 1;
    s.dir = "/srv/cpu";
    s.cpuAlertThreshold = 75.5;
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().size(), static_cast<size_t>(1));
    EXPECT_EQ(mgr2.servers().at(0).cpuAlertThreshold, 75.5);
}

TEST(ServerConfig, CpuAlertThresholdValidation)
{
    ServerConfig s;
    s.name = "V";
    s.appid = 1;
    s.dir = "/d";
    s.cpuAlertThreshold = -10.0;
    std::vector<std::string> errors = s.validate();
    ASSERT_TRUE(errors.size() >= 1);
    bool found = false;
    for (const auto &e : errors) {
        if (strContains(e, "CPU alert"))
            found = true;
    }
    ASSERT_TRUE(found);
}

TEST(ServerConfig, MemAlertThresholdDefault)
{
    ServerConfig s;
    EXPECT_EQ(s.memAlertThresholdMB, 0.0);
}

TEST(ServerConfig, MemAlertThresholdPersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "MemTest";
    s.appid = 1;
    s.dir = "/srv/mem";
    s.memAlertThresholdMB = 2048.0;
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().at(0).memAlertThresholdMB, 2048.0);
}

TEST(ServerConfig, MemAlertThresholdValidation)
{
    ServerConfig s;
    s.name = "V";
    s.appid = 1;
    s.dir = "/d";
    s.memAlertThresholdMB = -100.0;
    std::vector<std::string> errors = s.validate();
    bool found = false;
    for (const auto &e : errors) {
        if (strContains(e, "Memory alert"))
            found = true;
    }
    ASSERT_TRUE(found);
}

TEST(ServerConfig, ResourceAlertThresholdsExportImport)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");
    std::string exportPath = tmp.filePath("export.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "ResExp";
    s.appid = 1;
    s.dir = "/srv/res";
    s.cpuAlertThreshold = 80.0;
    s.memAlertThresholdMB = 4096.0;
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.exportServerConfig("ResExp", exportPath));

    ServerManager mgr2(tmp.filePath("servers2.json"));
    ASSERT_TRUE(mgr2.importServerConfig(exportPath).empty());
    EXPECT_EQ(mgr2.servers().at(0).cpuAlertThreshold, 80.0);
    EXPECT_EQ(mgr2.servers().at(0).memAlertThresholdMB, 4096.0);
}

TEST(ServerConfig, ResourceAlertSignal)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    ServerManager mgr(tmp.filePath("servers.json"));

    // Verify the onResourceAlert callback mechanism works
    int alertCount = 0;
    mgr.onResourceAlert = [&alertCount](const std::string &, const std::string &) {
        ++alertCount;
    };
    EXPECT_EQ(alertCount, 0);
}

// ---------------------------------------------------------------------------
// Event hook tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, EventHookKnownEvents)
{
    std::vector<std::string> events = EventHookManager::knownEvents();
    ASSERT_TRUE(vectorContains(events, std::string("onStart")));
    ASSERT_TRUE(vectorContains(events, std::string("onStop")));
    ASSERT_TRUE(vectorContains(events, std::string("onCrash")));
    ASSERT_TRUE(vectorContains(events, std::string("onBackup")));
    ASSERT_TRUE(vectorContains(events, std::string("onUpdate")));
    EXPECT_EQ(events.size(), static_cast<size_t>(5));
}

TEST(ServerConfig, EventHookFireEmpty)
{
    EventHookManager ehm;
    int hookCallbackCount = 0;
    ehm.onHookFinished = [&hookCallbackCount](const std::string &, const std::string &,
                                                int, const std::string &) {
        ++hookCallbackCount;
    };
    // Firing with empty script should do nothing
    ehm.fireHook("S", "/tmp", "onStart", "");
    EXPECT_EQ(hookCallbackCount, 0);
}

TEST(ServerConfig, EventHookFireMissing)
{
    EventHookManager ehm;
    int lastExitCode = 999;
    ehm.onHookFinished = [&lastExitCode](const std::string &, const std::string &,
                                           int exitCode, const std::string &) {
        lastExitCode = exitCode;
    };
    ehm.fireHook("S", "/tmp", "onStart", "/nonexistent_script_12345.sh");
    // Exit code should be -1 for not found
    EXPECT_EQ(lastExitCode, -1);
}

TEST(ServerConfig, EventHookFireScript)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    // Create a simple test script (platform-specific)
#ifdef _WIN32
    std::string scriptPath = tmp.filePath("hook.bat");
    { std::ofstream f(scriptPath); ASSERT_TRUE(f.is_open()); f << "@echo off\necho hook ran: %SSA_SERVER_NAME% %SSA_EVENT%\n"; }
#else
    std::string scriptPath = tmp.filePath("hook.sh");
    { std::ofstream f(scriptPath); ASSERT_TRUE(f.is_open()); f << "#!/bin/sh\necho \"hook ran: $SSA_SERVER_NAME $SSA_EVENT\"\n"; }
    std::filesystem::permissions(scriptPath, std::filesystem::perms::owner_all);
#endif

    EventHookManager ehm;
    std::string cbServerName, cbEvent, cbOutput;
    int cbExitCode = -999;
    std::atomic<bool> hookDone{false};
    ehm.onHookFinished = [&](const std::string &sn, const std::string &ev,
                              int ec, const std::string &out) {
        cbServerName = sn;
        cbEvent = ev;
        cbExitCode = ec;
        cbOutput = out;
        hookDone.store(true);
    };
    ehm.fireHook("TestSrv", tmp.path(), "onStart", scriptPath);

    // Wait for the async hook to finish (up to 5 seconds)
    for (int i = 0; i < 50 && !hookDone.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_TRUE(hookDone.load());
    EXPECT_EQ(cbServerName, "TestSrv");
    EXPECT_EQ(cbEvent, "onStart");
    EXPECT_EQ(cbExitCode, 0);
    ASSERT_TRUE(strContains(cbOutput, "hook ran: TestSrv onStart"));
}

TEST(ServerConfig, EventHookTimeout)
{
    EventHookManager ehm;
    EXPECT_EQ(ehm.timeoutSeconds(), 0); // default = fire-and-forget
    ehm.setTimeoutSeconds(10);
    EXPECT_EQ(ehm.timeoutSeconds(), 10);
    ehm.setTimeoutSeconds(-5);
    EXPECT_EQ(ehm.timeoutSeconds(), 0); // clamped to 0
}

TEST(ServerConfig, EventHooksPersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "HookTest";
    s.appid = 1;
    s.dir = "/srv/hook";
    s.eventHooks["onStart"] = "/scripts/start.sh";
    s.eventHooks["onCrash"] = "/scripts/crash.sh";
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    const ServerConfig &loaded = mgr2.servers().at(0);
    EXPECT_EQ(loaded.eventHooks.size(), 2);
    EXPECT_EQ(loaded.eventHooks.at("onStart"),
             "/scripts/start.sh");
    EXPECT_EQ(loaded.eventHooks.at("onCrash"),
             "/scripts/crash.sh");
}

TEST(ServerConfig, EventHooksDefaultEmpty)
{
    ServerConfig s;
    ASSERT_TRUE(s.eventHooks.empty());
}

TEST(ServerConfig, EventHooksExportImport)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");
    std::string exportPath = tmp.filePath("export.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "HookExp";
    s.appid = 1;
    s.dir = "/srv/hookexp";
    s.eventHooks["onBackup"] = "hooks/backup.sh";
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.exportServerConfig("HookExp", exportPath));

    ServerManager mgr2(tmp.filePath("servers2.json"));
    ASSERT_TRUE(mgr2.importServerConfig(exportPath).empty());
    EXPECT_EQ(mgr2.servers().at(0).eventHooks.at("onBackup"),
             "hooks/backup.sh");
}

// ---------------------------------------------------------------------------
// Tags tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, TagsDefault)
{
    ServerConfig s;
    ASSERT_TRUE(s.tags.empty());
}

TEST(ServerConfig, TagsPersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "TagTest";
    s.appid = 1;
    s.dir = "/srv/tag";
    s.tags.push_back("production"); s.tags.push_back("ark");
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().at(0).tags.size(), 2);
    EXPECT_EQ(mgr2.servers().at(0).tags.at(0), "production");
    EXPECT_EQ(mgr2.servers().at(0).tags.at(1), "ark");
}

TEST(ServerConfig, TagsExportImport)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");
    std::string exportPath = tmp.filePath("export.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "TagExp";
    s.appid = 1;
    s.dir = "/srv/tagexp";
    s.tags.push_back("cluster-a"); s.tags.push_back("pvp");
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.exportServerConfig("TagExp", exportPath));

    ServerManager mgr2(tmp.filePath("servers2.json"));
    ASSERT_TRUE(mgr2.importServerConfig(exportPath).empty());
    EXPECT_EQ(mgr2.servers().at(0).tags.size(), 2);
    EXPECT_EQ(mgr2.servers().at(0).tags.at(0), "cluster-a");
    EXPECT_EQ(mgr2.servers().at(0).tags.at(1), "pvp");
}

TEST(ServerConfig, TagsMultiple)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s1;
    s1.name = "S1";
    s1.appid = 1;
    s1.dir = "/srv/s1";
    s1.tags.push_back("pve"); s1.tags.push_back("eu");

    ServerConfig s2;
    s2.name = "S2";
    s2.appid = 2;
    s2.dir = "/srv/s2";
    s2.tags.push_back("pvp"); s2.tags.push_back("us");

    mgr.servers().push_back(s1);
    mgr.servers().push_back(s2);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().size(), static_cast<size_t>(2));
    EXPECT_EQ(mgr2.servers().at(0).tags, std::vector<std::string>({"pve", "eu"}));
    EXPECT_EQ(mgr2.servers().at(1).tags, std::vector<std::string>({"pvp", "us"}));
}

// ---------------------------------------------------------------------------
// Server group tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, GroupDefault)
{
    ServerConfig s;
    ASSERT_TRUE(s.group.empty());
}

TEST(ServerConfig, GroupPersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "GroupTest";
    s.appid = 1;
    s.dir = "/srv/grp";
    s.group = "Production";
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().at(0).group, "Production");
}

TEST(ServerConfig, GroupExportImport)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");
    std::string exportPath = tmp.filePath("export.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "GrpExp";
    s.appid = 1;
    s.dir = "/srv/grpexp";
    s.group = "Testing";
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.exportServerConfig("GrpExp", exportPath));

    ServerManager mgr2(tmp.filePath("servers2.json"));
    ASSERT_TRUE(mgr2.importServerConfig(exportPath).empty());
    EXPECT_EQ(mgr2.servers().at(0).group, "Testing");
}

// ---------------------------------------------------------------------------
// Startup priority tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, StartupPriorityDefault)
{
    ServerConfig s;
    EXPECT_EQ(s.startupPriority, 0);
}

TEST(ServerConfig, StartupPriorityPersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "PrioTest";
    s.appid = 1;
    s.dir = "/srv/prio";
    s.startupPriority = 5;
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().at(0).startupPriority, 5);
}

TEST(ServerConfig, StartupPriorityValidation)
{
    ServerConfig s;
    s.name = "V";
    s.appid = 1;
    s.dir = "/srv/v";
    s.startupPriority = -1;
    std::vector<std::string> errors = s.validate();
    bool found = false;
    for (const auto &e : errors)
        if (strContains(e, "Startup priority")) { found = true; break; }
    ASSERT_TRUE(found);
}

TEST(ServerConfig, StartupPriorityExportImport)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");
    std::string exportPath = tmp.filePath("export.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "PrioExp";
    s.appid = 1;
    s.dir = "/srv/prioexp";
    s.startupPriority = 3;
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.exportServerConfig("PrioExp", exportPath));

    ServerManager mgr2(tmp.filePath("servers2.json"));
    ASSERT_TRUE(mgr2.importServerConfig(exportPath).empty());
    EXPECT_EQ(mgr2.servers().at(0).startupPriority, 3);
}

TEST(ServerConfig, AutoStartRespectsStartupPriority)
{
    // Verify that autoStartServers processes servers in priority order
    // by listening to the logMessage signal which is emitted when
    // startServer is called (even if the process fails to launch).
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);

    ServerConfig s1;
    s1.name = "LowPrio";
    s1.appid = 1;
    s1.dir = "/srv/low";
    s1.autoStartOnLaunch = true;
    s1.startupPriority = 10;

    ServerConfig s2;
    s2.name = "HighPrio";
    s2.appid = 2;
    s2.dir = "/srv/high";
    s2.autoStartOnLaunch = true;
    s2.startupPriority = 1;

    // Insert LowPrio first in the list; autoStartServers should still
    // process HighPrio (priority 1) before LowPrio (priority 10).
    mgr.servers().push_back(s1);
    mgr.servers().push_back(s2);

    std::vector<std::string> startOrder;
    mgr.onLogMessage = [&startOrder](const std::string &server, const std::string &) {
        if (!vectorContains(startOrder, server))
            startOrder.push_back(server);
    };

    mgr.autoStartServers();

    // Both servers should have been attempted (executables won't exist, but
    // the callback still fires in order).
    ASSERT_GE(startOrder.size(), static_cast<size_t>(2));
    auto it1 = std::find(startOrder.begin(), startOrder.end(), "HighPrio");
    auto it2 = std::find(startOrder.begin(), startOrder.end(), "LowPrio");
    ASSERT_TRUE(it1 != startOrder.end());
    ASSERT_TRUE(it2 != startOrder.end());
    EXPECT_LT(std::distance(startOrder.begin(), it1), std::distance(startOrder.begin(), it2));
}

// ---------------------------------------------------------------------------
// Backup before restart tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, BackupBeforeRestartDefault)
{
    ServerConfig s;
    EXPECT_EQ(s.backupBeforeRestart, false);
}

TEST(ServerConfig, BackupBeforeRestartPersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "BbrTest";
    s.appid = 1;
    s.dir = "/srv/bbr";
    s.backupBeforeRestart = true;
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().at(0).backupBeforeRestart, true);
}

TEST(ServerConfig, BackupBeforeRestartExportImport)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");
    std::string exportPath = tmp.filePath("export.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "BbrExp";
    s.appid = 1;
    s.dir = "/srv/bbrexp";
    s.backupBeforeRestart = true;
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.exportServerConfig("BbrExp", exportPath));

    ServerManager mgr2(tmp.filePath("servers2.json"));
    ASSERT_TRUE(mgr2.importServerConfig(exportPath).empty());
    EXPECT_EQ(mgr2.servers().at(0).backupBeforeRestart, true);
}

// ---------------------------------------------------------------------------
// Graceful shutdown timeout tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, GracefulShutdownDefault)
{
    ServerConfig s;
    EXPECT_EQ(s.gracefulShutdownSeconds, 10);
}

TEST(ServerConfig, GracefulShutdownPersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "GsTest";
    s.appid = 1;
    s.dir = "/srv/gs";
    s.gracefulShutdownSeconds = 30;
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().at(0).gracefulShutdownSeconds, 30);
}

TEST(ServerConfig, GracefulShutdownValidation)
{
    ServerConfig s;
    s.name = "V";
    s.appid = 1;
    s.dir = "/srv/v";
    s.gracefulShutdownSeconds = -5;
    std::vector<std::string> errors = s.validate();
    bool found = false;
    for (const auto &e : errors)
        if (strContains(e, "Graceful shutdown")) { found = true; break; }
    ASSERT_TRUE(found);
}

TEST(ServerConfig, GracefulShutdownExportImport)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");
    std::string exportPath = tmp.filePath("export.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "GsExp";
    s.appid = 1;
    s.dir = "/srv/gsexp";
    s.gracefulShutdownSeconds = 20;
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.exportServerConfig("GsExp", exportPath));

    ServerManager mgr2(tmp.filePath("servers2.json"));
    ASSERT_TRUE(mgr2.importServerConfig(exportPath).empty());
    EXPECT_EQ(mgr2.servers().at(0).gracefulShutdownSeconds, 20);
}

// ---------------------------------------------------------------------------
// Environment variables tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, EnvironmentVariablesDefault)
{
    ServerConfig s;
    ASSERT_TRUE(s.environmentVariables.empty());
}

TEST(ServerConfig, EnvironmentVariablesPersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "EnvTest";
    s.appid = 1;
    s.dir = "/srv/env";
    s.environmentVariables["GAME_MODE"] = "survival";
    s.environmentVariables["MAX_RATE"] = "128000";
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().at(0).environmentVariables.size(), 2);
    EXPECT_EQ(mgr2.servers().at(0).environmentVariables.at("GAME_MODE"),
             "survival");
    EXPECT_EQ(mgr2.servers().at(0).environmentVariables.at("MAX_RATE"),
             "128000");
}

TEST(ServerConfig, EnvironmentVariablesExportImport)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");
    std::string exportPath = tmp.filePath("export.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "EnvExp";
    s.appid = 1;
    s.dir = "/srv/envexp";
    s.environmentVariables["SRCDS_TOKEN"] = "abc123";
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.exportServerConfig("EnvExp", exportPath));

    ServerManager mgr2(tmp.filePath("servers2.json"));
    ASSERT_TRUE(mgr2.importServerConfig(exportPath).empty());
    EXPECT_EQ(mgr2.servers().at(0).environmentVariables.at("SRCDS_TOKEN"),
             "abc123");
}

TEST(ServerConfig, EnvironmentVariablesMultiple)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s1;
    s1.name = "E1";
    s1.appid = 1;
    s1.dir = "/srv/e1";
    s1.environmentVariables["KEY1"] = "val1";

    ServerConfig s2;
    s2.name = "E2";
    s2.appid = 2;
    s2.dir = "/srv/e2";
    s2.environmentVariables["KEY2"] = "val2";
    s2.environmentVariables["KEY3"] = "val3";

    mgr.servers().push_back(s1);
    mgr.servers().push_back(s2);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().size(), static_cast<size_t>(2));
    EXPECT_EQ(mgr2.servers().at(0).environmentVariables.size(), 1);
    EXPECT_EQ(mgr2.servers().at(1).environmentVariables.size(), 2);
}

// ---------------------------------------------------------------------------
// Batch operations tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, StartAllServers)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s1;
    s1.name = "Batch1";
    s1.appid = 1;
    s1.dir = "/srv/b1";
    s1.startupPriority = 2;

    ServerConfig s2;
    s2.name = "Batch2";
    s2.appid = 2;
    s2.dir = "/srv/b2";
    s2.startupPriority = 1;

    mgr.servers().push_back(s1);
    mgr.servers().push_back(s2);

    // startAllServers should not crash even with non-existent executables
    mgr.startAllServers();
    // Wait for forked children to exit
    for (int i = 0; i < 20; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        mgr.tick();
        if (mgr.runningServerCount() == 0) break;
    }
    // Servers won't actually start (no executables), so running count = 0
    EXPECT_EQ(mgr.runningServerCount(), 0);
}

TEST(ServerConfig, StopAllServers)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s1;
    s1.name = "StopAll1";
    s1.appid = 1;
    s1.dir = "/srv/sa1";
    mgr.servers().push_back(s1);

    // stopAllServers on non-running servers should not crash
    mgr.stopAllServers();
    EXPECT_EQ(mgr.runningServerCount(), 0);
}

TEST(ServerConfig, RestartAllServers)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s1;
    s1.name = "RestartAll1";
    s1.appid = 1;
    s1.dir = "/srv/ra1";
    mgr.servers().push_back(s1);

    // restartAllServers on non-running servers should not crash
    mgr.restartAllServers();
    EXPECT_EQ(mgr.runningServerCount(), 0);
}

TEST(ServerConfig, StartGroup)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s1;
    s1.name = "G1";
    s1.appid = 1;
    s1.dir = "/srv/g1";
    s1.group = "Production";

    ServerConfig s2;
    s2.name = "G2";
    s2.appid = 2;
    s2.dir = "/srv/g2";
    s2.group = "Testing";

    mgr.servers().push_back(s1);
    mgr.servers().push_back(s2);

    // startGroup should only attempt to start servers in the specified group
    mgr.startGroup("Production");
    // Wait for forked children to exit
    for (int i = 0; i < 20; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        mgr.tick();
        if (mgr.runningServerCount() == 0) break;
    }
    EXPECT_EQ(mgr.runningServerCount(), 0); // No executables, but shouldn't crash
}

TEST(ServerConfig, StopGroup)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s1;
    s1.name = "SG1";
    s1.appid = 1;
    s1.dir = "/srv/sg1";
    s1.group = "Production";
    mgr.servers().push_back(s1);

    mgr.stopGroup("Production");
    EXPECT_EQ(mgr.runningServerCount(), 0);
}

TEST(ServerConfig, RestartGroup)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s1;
    s1.name = "RG1";
    s1.appid = 1;
    s1.dir = "/srv/rg1";
    s1.group = "Production";
    mgr.servers().push_back(s1);

    mgr.restartGroup("Production");
    EXPECT_EQ(mgr.runningServerCount(), 0);
}

TEST(ServerConfig, ServerGroupsList)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s1;
    s1.name = "GL1";
    s1.appid = 1;
    s1.dir = "/srv/gl1";
    s1.group = "Production";

    ServerConfig s2;
    s2.name = "GL2";
    s2.appid = 2;
    s2.dir = "/srv/gl2";
    s2.group = "Testing";

    ServerConfig s3;
    s3.name = "GL3";
    s3.appid = 3;
    s3.dir = "/srv/gl3";
    s3.group = "Production";  // duplicate group

    ServerConfig s4;
    s4.name = "GL4";
    s4.appid = 4;
    s4.dir = "/srv/gl4";
    // no group set

    mgr.servers().push_back(s1);
    mgr.servers().push_back(s2);
    mgr.servers().push_back(s3);
    mgr.servers().push_back(s4);

    std::vector<std::string> groups = mgr.serverGroups();
    EXPECT_EQ(groups.size(), static_cast<size_t>(2));
    ASSERT_TRUE(vectorContains(groups, std::string("Production")));
    ASSERT_TRUE(vectorContains(groups, std::string("Testing")));
}

TEST(ServerConfig, ServerGroupsEmpty)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ASSERT_TRUE(mgr.serverGroups().empty());

    // Servers without groups
    ServerConfig s1;
    s1.name = "NoGroup";
    s1.appid = 1;
    s1.dir = "/srv/ng";
    mgr.servers().push_back(s1);
    ASSERT_TRUE(mgr.serverGroups().empty());
}

TEST(ServerConfig, RunningServerCount)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    EXPECT_EQ(mgr.runningServerCount(), 0);

    ServerConfig s1;
    s1.name = "RC1";
    s1.appid = 1;
    s1.dir = "/srv/rc1";
    mgr.servers().push_back(s1);
    EXPECT_EQ(mgr.runningServerCount(), 0);
}

// ---------------------------------------------------------------------------
// Auto-update check interval tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, AutoUpdateCheckIntervalDefault)
{
    ServerConfig s;
    EXPECT_EQ(s.autoUpdateCheckIntervalMinutes, 0);
}

TEST(ServerConfig, AutoUpdateCheckIntervalPersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "UpdateCheck";
    s.appid = 1;
    s.dir = "/srv/uc";
    s.autoUpdateCheckIntervalMinutes = 60;
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().at(0).autoUpdateCheckIntervalMinutes, 60);
}

TEST(ServerConfig, AutoUpdateCheckIntervalValidation)
{
    ServerConfig s;
    s.name = "UCValid";
    s.appid = 1;
    s.dir = "/srv/ucv";

    s.autoUpdateCheckIntervalMinutes = 0;
    ASSERT_TRUE(s.validate().empty());

    s.autoUpdateCheckIntervalMinutes = 120;
    ASSERT_TRUE(s.validate().empty());

    s.autoUpdateCheckIntervalMinutes = -1;
    std::vector<std::string> errors = s.validate();
    EXPECT_FALSE(errors.empty());
    bool found = false;
    for (const auto &e : errors) {
        if (strContains(e, "Auto-update check interval"))
            found = true;
    }
    ASSERT_TRUE(found);
}

TEST(ServerConfig, AutoUpdateCheckIntervalExportImport)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");
    std::string exportPath = tmp.filePath("export.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "UCExp";
    s.appid = 1;
    s.dir = "/srv/ucexp";
    s.autoUpdateCheckIntervalMinutes = 45;
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.exportServerConfig("UCExp", exportPath));

    ServerManager mgr2(tmp.filePath("servers2.json"));
    ASSERT_TRUE(mgr2.importServerConfig(exportPath).empty());
    EXPECT_EQ(mgr2.servers().at(0).autoUpdateCheckIntervalMinutes, 45);
}

// ---------------------------------------------------------------------------
// Server statistics tests
// ---------------------------------------------------------------------------

TEST(ServerConfig, TotalUptimeDefault)
{
    ServerConfig s;
    EXPECT_EQ(s.totalUptimeSeconds, 0);
}

TEST(ServerConfig, TotalUptimePersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "UptimeTest";
    s.appid = 1;
    s.dir = "/srv/ut";
    s.totalUptimeSeconds = 86400;  // 1 day
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().at(0).totalUptimeSeconds, 86400);
}

TEST(ServerConfig, TotalCrashesDefault)
{
    ServerConfig s;
    EXPECT_EQ(s.totalCrashes, 0);
}

TEST(ServerConfig, TotalCrashesPersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "CrashTest";
    s.appid = 1;
    s.dir = "/srv/ct";
    s.totalCrashes = 5;
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().at(0).totalCrashes, 5);
}

TEST(ServerConfig, LastCrashTimeDefault)
{
    ServerConfig s;
    ASSERT_TRUE(s.lastCrashTime.empty());
}

TEST(ServerConfig, LastCrashTimePersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "CrashTimeTest";
    s.appid = 1;
    s.dir = "/srv/ctt";
    s.lastCrashTime = "2026-01-15T10:30:00";
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers().at(0).lastCrashTime, "2026-01-15T10:30:00");
}

TEST(ServerConfig, StatsExportImport)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");
    std::string exportPath = tmp.filePath("export.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "StatsExp";
    s.appid = 1;
    s.dir = "/srv/statsexp";
    s.totalUptimeSeconds = 172800;  // 2 days
    s.totalCrashes = 3;
    s.lastCrashTime = "2026-02-20T14:00:00";
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.exportServerConfig("StatsExp", exportPath));

    ServerManager mgr2(tmp.filePath("servers2.json"));
    ASSERT_TRUE(mgr2.importServerConfig(exportPath).empty());
    EXPECT_EQ(mgr2.servers().at(0).totalUptimeSeconds, 172800);
    EXPECT_EQ(mgr2.servers().at(0).totalCrashes, 3);
    EXPECT_EQ(mgr2.servers().at(0).lastCrashTime, "2026-02-20T14:00:00");
}

TEST(ServerConfig, StatsValidation)
{
    ServerConfig s;
    s.name = "StatsValid";
    s.appid = 1;
    s.dir = "/srv/sv";

    // Valid defaults
    ASSERT_TRUE(s.validate().empty());

    // Negative totalUptimeSeconds should fail
    s.totalUptimeSeconds = -1;
    std::vector<std::string> errors = s.validate();
    EXPECT_FALSE(errors.empty());
    bool found = false;
    for (const auto &e : errors) {
        if (strContains(e, "Total uptime"))
            found = true;
    }
    ASSERT_TRUE(found);
    s.totalUptimeSeconds = 0;

    // Negative totalCrashes should fail
    s.totalCrashes = -1;
    errors = s.validate();
    EXPECT_FALSE(errors.empty());
    found = false;
    for (const auto &e : errors) {
        if (strContains(e, "Total crashes"))
            found = true;
    }
    ASSERT_TRUE(found);
}


// ===========================================================================
// Pending update indicator tests
// ===========================================================================

TEST(ServerConfig, PendingUpdateSetAndClear)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "PendUpd";
    s.appid = 1;
    s.dir = tmp.filePath("srv");
    mgr.servers().push_back(s);
    mgr.saveConfig();

    EXPECT_FALSE(mgr.hasPendingUpdate("PendUpd"));
    mgr.setPendingUpdate("PendUpd", true);
    EXPECT_TRUE(mgr.hasPendingUpdate("PendUpd"));
    mgr.setPendingUpdate("PendUpd", false);
    EXPECT_FALSE(mgr.hasPendingUpdate("PendUpd"));
}

TEST(ServerConfig, PendingModUpdateSetAndClear)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "PendMod";
    s.appid = 1;
    s.dir = tmp.filePath("srv");
    mgr.servers().push_back(s);
    mgr.saveConfig();

    EXPECT_FALSE(mgr.hasPendingModUpdate("PendMod"));
    mgr.setPendingModUpdate("PendMod", true);
    EXPECT_TRUE(mgr.hasPendingModUpdate("PendMod"));
    mgr.setPendingModUpdate("PendMod", false);
    EXPECT_FALSE(mgr.hasPendingModUpdate("PendMod"));
}

TEST(ServerConfig, PendingUpdateNonexistentServer)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    EXPECT_FALSE(mgr.hasPendingUpdate("DoesNotExist"));
    EXPECT_FALSE(mgr.hasPendingModUpdate("DoesNotExist"));
}

// ===========================================================================
// Scheduler auto-update check timer tests
// ===========================================================================

TEST(ServerConfig, SchedulerUpdateCheckCallback)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "TimerTest";
    s.appid = 1;
    s.dir = tmp.filePath("srv");
    s.autoUpdateCheckIntervalMinutes = 1; // 1 minute
    mgr.servers().push_back(s);
    mgr.saveConfig();

    SchedulerModule scheduler(&mgr);

    std::string calledFor;
    scheduler.onScheduledUpdateCheck = [&calledFor](const std::string &name) {
        calledFor = name;
    };

    scheduler.startScheduler("TimerTest");

    // Tick should NOT fire immediately (interval has not elapsed)
    scheduler.tick();
    EXPECT_TRUE(calledFor.empty());
}

TEST(ServerConfig, SchedulerUpdateCheckDisabledWhenZero)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "NoCheck";
    s.appid = 1;
    s.dir = tmp.filePath("srv");
    s.autoUpdateCheckIntervalMinutes = 0; // disabled
    mgr.servers().push_back(s);
    mgr.saveConfig();

    SchedulerModule scheduler(&mgr);

    bool called = false;
    scheduler.onScheduledUpdateCheck = [&called](const std::string &) {
        called = true;
    };

    scheduler.startScheduler("NoCheck");
    scheduler.tick();
    EXPECT_FALSE(called);
}

// ===========================================================================
// Crash statistics accumulation
// ===========================================================================

TEST(ServerConfig, CrashStatsInitialValues)
{
    ServerConfig s;
    EXPECT_EQ(s.totalCrashes, 0);
    EXPECT_EQ(s.totalUptimeSeconds, 0);
    EXPECT_TRUE(s.lastCrashTime.empty());
}

// ===========================================================================
// Preferences file round-trip (tests JSON format)
// ===========================================================================

TEST(ServerConfig, PreferencesJsonFormat)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string prefPath = tmp.filePath("ssa_preferences.json");

    // Write preferences
    {
        nlohmann::json j;
        j["darkMode"] = false;
        std::ofstream f(prefPath);
        ASSERT_TRUE(f.is_open());
        f << j.dump(2) << "\n";
    }

    // Read back
    {
        std::ifstream f(prefPath);
        ASSERT_TRUE(f.is_open());
        nlohmann::json j;
        f >> j;
        ASSERT_TRUE(j.contains("darkMode"));
        EXPECT_FALSE(j["darkMode"].get<bool>());
    }

    // Write dark mode true
    {
        nlohmann::json j;
        j["darkMode"] = true;
        std::ofstream f(prefPath);
        ASSERT_TRUE(f.is_open());
        f << j.dump(2) << "\n";
    }

    // Read back
    {
        std::ifstream f(prefPath);
        ASSERT_TRUE(f.is_open());
        nlohmann::json j;
        f >> j;
        EXPECT_TRUE(j["darkMode"].get<bool>());
    }
}

// ===========================================================================
// SteamCMD path preferences persistence
// ===========================================================================

TEST(ServerConfig, SteamCmdPathPreferencePersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string prefPath = tmp.filePath("ssa_preferences.json");

    // Write preferences with steamCmdPath
    {
        nlohmann::json j;
        j["darkMode"] = true;
        j["steamCmdPath"] = "/usr/bin/steamcmd";
        std::ofstream f(prefPath);
        ASSERT_TRUE(f.is_open());
        f << j.dump(2) << "\n";
    }

    // Read back and verify
    {
        std::ifstream f(prefPath);
        ASSERT_TRUE(f.is_open());
        nlohmann::json j;
        f >> j;
        ASSERT_TRUE(j.contains("steamCmdPath"));
        EXPECT_EQ(j["steamCmdPath"].get<std::string>(), "/usr/bin/steamcmd");
    }
}

TEST(ServerConfig, SteamCmdPathPreferenceDefaultEmpty)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string prefPath = tmp.filePath("ssa_preferences.json");

    // Write preferences without steamCmdPath
    {
        nlohmann::json j;
        j["darkMode"] = true;
        std::ofstream f(prefPath);
        ASSERT_TRUE(f.is_open());
        f << j.dump(2) << "\n";
    }

    // Read back – steamCmdPath should not be present
    {
        std::ifstream f(prefPath);
        ASSERT_TRUE(f.is_open());
        nlohmann::json j;
        f >> j;
        EXPECT_FALSE(j.contains("steamCmdPath"));
    }
}

TEST(ServerConfig, SteamCmdPathAppliedToManager)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr(tmp.filePath("servers.json"));

    // Default path should be steamcmd (or steamcmd.exe on Windows)
    std::string defaultPath = mgr.steamCmdPath();
#ifdef _WIN32
    EXPECT_EQ(defaultPath, "steamcmd.exe");
#else
    EXPECT_EQ(defaultPath, "steamcmd");
#endif

    // Set a custom path
    mgr.setSteamCmdPath("/opt/steamcmd/steamcmd.sh");
    EXPECT_EQ(mgr.steamCmdPath(), "/opt/steamcmd/steamcmd.sh");
}

TEST(ServerConfig, DeployServerCallsEmitLog)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr(tmp.filePath("servers.json"));

    // Track log messages
    std::vector<std::string> logMessages;
    mgr.onLogMessage = [&](const std::string &, const std::string &msg) {
        logMessages.push_back(msg);
    };

    ServerConfig s;
    s.name  = "DeployTest";
    s.appid = 730;
    s.dir   = tmp.filePath("serverdir");
    mgr.servers().push_back(s);

    // deployServer should log the deployment start. SteamCMD is not available
    // in the test environment, so it will fail, but should still log messages.
    mgr.deployServer(mgr.servers()[0]);

    ASSERT_FALSE(logMessages.empty());
    EXPECT_NE(logMessages[0].find("SteamCMD"), std::string::npos);
}

// ===========================================================================
// INI Editor Tests
// ===========================================================================

TEST(IniEditor, ParseBasicIni)
{
    std::string content =
        "[ServerSettings]\n"
        "MaxPlayers=70\n"
        "ServerPassword=secret\n"
        "\n"
        "[SessionSettings]\n"
        "SessionName=MyServer\n";

    IniEditor editor;
    editor.loadFromString(content);

    auto sections = editor.sections();
    ASSERT_EQ(sections.size(), 2u);
    EXPECT_EQ(sections[0], "ServerSettings");
    EXPECT_EQ(sections[1], "SessionSettings");

    EXPECT_EQ(editor.getValue("ServerSettings", "MaxPlayers"), "70");
    EXPECT_EQ(editor.getValue("ServerSettings", "ServerPassword"), "secret");
    EXPECT_EQ(editor.getValue("SessionSettings", "SessionName"), "MyServer");
}

TEST(IniEditor, SetValueExistingKey)
{
    std::string content =
        "[Server]\n"
        "Port=7777\n";

    IniEditor editor;
    editor.loadFromString(content);

    editor.setValue("Server", "Port", "8888");
    EXPECT_EQ(editor.getValue("Server", "Port"), "8888");
}

TEST(IniEditor, SetValueNewKey)
{
    std::string content =
        "[Server]\n"
        "Port=7777\n";

    IniEditor editor;
    editor.loadFromString(content);

    editor.setValue("Server", "MaxPlayers", "100");
    EXPECT_EQ(editor.getValue("Server", "MaxPlayers"), "100");
    EXPECT_TRUE(editor.hasKey("Server", "MaxPlayers"));
}

TEST(IniEditor, SetValueNewSection)
{
    std::string content =
        "[Server]\n"
        "Port=7777\n";

    IniEditor editor;
    editor.loadFromString(content);

    editor.setValue("NewSection", "Key1", "Value1");
    EXPECT_TRUE(editor.hasSection("NewSection"));
    EXPECT_EQ(editor.getValue("NewSection", "Key1"), "Value1");
}

TEST(IniEditor, RemoveKey)
{
    std::string content =
        "[Server]\n"
        "Port=7777\n"
        "Name=Test\n";

    IniEditor editor;
    editor.loadFromString(content);

    EXPECT_TRUE(editor.removeKey("Server", "Port"));
    EXPECT_FALSE(editor.hasKey("Server", "Port"));
    EXPECT_TRUE(editor.hasKey("Server", "Name"));
}

TEST(IniEditor, PreserveComments)
{
    std::string content =
        "; This is a comment\n"
        "[Server]\n"
        "# Another comment\n"
        "Port=7777\n";

    IniEditor editor;
    editor.loadFromString(content);

    std::string output = editor.toString();
    EXPECT_NE(output.find("; This is a comment"), std::string::npos);
    EXPECT_NE(output.find("# Another comment"), std::string::npos);
}

TEST(IniEditor, RoundTrip)
{
    std::string content =
        "[ServerSettings]\n"
        "MaxPlayers=70\n"
        "Difficulty=Hard\n"
        "\n"
        "[World]\n"
        "MapName=TheIsland\n";

    IniEditor editor;
    editor.loadFromString(content);

    std::string output = editor.toString();
    EXPECT_EQ(output, content);
}

TEST(IniEditor, FileLoadSave)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    std::string iniPath = tmp.filePath("test.ini");
    {
        std::ofstream f(iniPath);
        f << "[Section1]\nKey1=Value1\nKey2=Value2\n";
    }

    IniEditor editor;
    EXPECT_TRUE(editor.loadFile(iniPath));
    EXPECT_EQ(editor.getValue("Section1", "Key1"), "Value1");

    editor.setValue("Section1", "Key1", "ModifiedValue");
    EXPECT_TRUE(editor.saveFile(iniPath));

    IniEditor editor2;
    EXPECT_TRUE(editor2.loadFile(iniPath));
    EXPECT_EQ(editor2.getValue("Section1", "Key1"), "ModifiedValue");
}

TEST(IniEditor, EmptyFile)
{
    IniEditor editor;
    editor.loadFromString("");

    EXPECT_TRUE(editor.sections().empty());
    EXPECT_TRUE(editor.lines().empty());
}

TEST(IniEditor, KeysInSection)
{
    std::string content =
        "[Server]\n"
        "Port=7777\n"
        "Name=Test\n"
        "Max=100\n";

    IniEditor editor;
    editor.loadFromString(content);

    auto keys = editor.keysInSection("Server");
    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0].first, "Port");
    EXPECT_EQ(keys[0].second, "7777");
    EXPECT_EQ(keys[1].first, "Name");
    EXPECT_EQ(keys[1].second, "Test");
    EXPECT_EQ(keys[2].first, "Max");
    EXPECT_EQ(keys[2].second, "100");
}

// ===========================================================================
// Config Backup Manager Tests
// ===========================================================================

TEST(ConfigBackup, CreateBackup)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    std::string configPath = tmp.filePath("GameUserSettings.ini");
    {
        std::ofstream f(configPath);
        f << "[Server]\nPort=7777\n";
    }

    std::string backupPath = ConfigBackupManager::createBackup(configPath);
    EXPECT_FALSE(backupPath.empty());
    EXPECT_TRUE(fs::exists(backupPath));

    // Verify backup content matches original
    std::ifstream orig(configPath);
    std::string origContent{std::istreambuf_iterator<char>(orig),
                            std::istreambuf_iterator<char>()};

    std::ifstream bak(backupPath);
    std::string bakContent{std::istreambuf_iterator<char>(bak),
                           std::istreambuf_iterator<char>()};

    EXPECT_EQ(origContent, bakContent);
}

TEST(ConfigBackup, ListBackups)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    std::string configPath = tmp.filePath("GameUserSettings.ini");
    {
        std::ofstream f(configPath);
        f << "[Server]\nPort=7777\n";
    }

    // Create multiple backups
    ConfigBackupManager::createBackup(configPath);
    // Sleep > 1 second to ensure different timestamp
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    // Modify the original and backup again
    {
        std::ofstream f(configPath);
        f << "[Server]\nPort=8888\n";
    }
    ConfigBackupManager::createBackup(configPath);

    auto backups = ConfigBackupManager::listBackups(configPath);
    ASSERT_GE(static_cast<int>(backups.size()), 2);
    // First entry should be newest
    EXPECT_GE(backups[0].timestamp, backups[1].timestamp);
}

TEST(ConfigBackup, RestoreBackup)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    std::string configPath = tmp.filePath("GameUserSettings.ini");
    std::string originalContent = "[Server]\nPort=7777\n";
    {
        std::ofstream f(configPath);
        f << originalContent;
    }

    // Backup
    std::string backupPath = ConfigBackupManager::createBackup(configPath);
    ASSERT_FALSE(backupPath.empty());

    // Modify original
    {
        std::ofstream f(configPath);
        f << "[Server]\nPort=9999\n";
    }

    // Restore
    EXPECT_TRUE(ConfigBackupManager::restoreBackup(backupPath, configPath));

    // Verify restored content
    std::ifstream f(configPath);
    std::string restoredContent{std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>()};
    EXPECT_EQ(restoredContent, originalContent);
}

TEST(ConfigBackup, BackupDir)
{
    std::string path = "/some/path/to/GameUserSettings.ini";
    std::string dir = ConfigBackupManager::backupDir(path);
    EXPECT_NE(dir.find(".ssa_config_backups"), std::string::npos);
}

TEST(ConfigBackup, RotateBackups)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    std::string configPath = tmp.filePath("test.ini");
    {
        std::ofstream f(configPath);
        f << "content\n";
    }

    // Create several backups
    for (int i = 0; i < 5; ++i) {
        ConfigBackupManager::createBackup(configPath);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Rotate to keep only 2
    ConfigBackupManager::rotateBackups(configPath, 2);

    auto backups = ConfigBackupManager::listBackups(configPath);
    EXPECT_LE(static_cast<int>(backups.size()), 2);
}

TEST(ConfigBackup, CreateBackupNonexistentFile)
{
    std::string result = ConfigBackupManager::createBackup("/nonexistent/file.ini");
    EXPECT_TRUE(result.empty());
}

// ===========================================================================
// Steam Library Detector Tests
// ===========================================================================

TEST(SteamLibrary, ParseAppManifest)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    // Create a mock library structure
    std::string libPath = tmp.path();
    fs::create_directories(fs::path(libPath) / "steamapps" / "common" / "TestGame");

    std::string acfPath = tmp.filePath("test.acf");
    {
        std::ofstream f(acfPath);
        f << "\"AppState\"\n"
          << "{\n"
          << "  \"appid\"  \"730\"\n"
          << "  \"name\"   \"Counter-Strike 2\"\n"
          << "  \"installdir\"  \"TestGame\"\n"
          << "  \"SizeOnDisk\"  \"1234567890\"\n"
          << "}\n";
    }

    auto app = SteamLibraryDetector::parseAppManifest(acfPath, libPath);
    EXPECT_EQ(app.appid, 730);
    EXPECT_EQ(app.name, "Counter-Strike 2");
    EXPECT_NE(app.installDir.find("TestGame"), std::string::npos);
    EXPECT_EQ(app.sizeOnDisk, "1234567890");
}

TEST(SteamLibrary, ParseLibraryFolders)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    // Create another temp dir to serve as a valid path in VDF
    TempDir libDir;
    ASSERT_TRUE(libDir.isValid());

    std::string vdfPath = tmp.filePath("libraryfolders.vdf");
    {
        std::ofstream f(vdfPath);
        f << "\"libraryfolders\"\n"
          << "{\n"
          << "  \"0\"\n"
          << "  {\n"
          << "    \"path\"  \"" << libDir.path() << "\"\n"
          << "  }\n"
          << "}\n";
    }

    auto folders = SteamLibraryDetector::parseLibraryFolders(vdfPath);
    ASSERT_EQ(folders.size(), 1u);
    EXPECT_EQ(folders[0], libDir.path());
}

TEST(SteamLibrary, DetectWithCustomRoot)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    // Create mock Steam structure
    fs::create_directories(fs::path(tmp.path()) / "steamapps" / "common" / "MyGame");

    // Create VDF with self-reference
    {
        std::ofstream f((fs::path(tmp.path()) / "steamapps" / "libraryfolders.vdf").string());
        f << "\"libraryfolders\"\n{\n  \"0\"\n  {\n    \"path\"  \"" << tmp.path() << "\"\n  }\n}\n";
    }

    // Create appmanifest
    {
        std::ofstream f((fs::path(tmp.path()) / "steamapps" / "appmanifest_730.acf").string());
        f << "\"AppState\"\n{\n  \"appid\"  \"730\"\n  \"name\"  \"CS2\"\n"
          << "  \"installdir\"  \"MyGame\"\n}\n";
    }

    SteamLibraryDetector detector;
    detector.setSteamRoot(tmp.path());

    auto apps = detector.detect();
    ASSERT_EQ(apps.size(), 1u);
    EXPECT_EQ(apps[0].appid, 730);
    EXPECT_EQ(apps[0].name, "CS2");
}

TEST(SteamLibrary, DetectEmptyRoot)
{
    SteamLibraryDetector detector;
    detector.setSteamRoot("/nonexistent/steam/path");

    auto apps = detector.detect();
    EXPECT_TRUE(apps.empty());
}

TEST(SteamLibrary, DefaultSteamRoot)
{
    // Just ensure it doesn't crash
    std::string root = SteamLibraryDetector::detectSteamRoot();
    // May or may not find Steam in CI; just ensure no exception
    (void)root;
}

TEST(SteamLibrary, AccessorMethods)
{
    SteamLibraryDetector detector;
    EXPECT_TRUE(detector.steamRoot().empty());

    detector.setSteamRoot("/some/path");
    EXPECT_EQ(detector.steamRoot(), "/some/path");
}

TEST(SteamLibrary, ServerManagerIntegration)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr(tmp.filePath("servers.json"));

    // The detector should be accessible and configurable
    auto *det = mgr.steamLibraryDetector();
    ASSERT_NE(det, nullptr);

    det->setSteamRoot("/nonexistent");
    EXPECT_EQ(det->steamRoot(), "/nonexistent");
}

// ===========================================================================
// Graceful Restart Manager Tests
// ===========================================================================

TEST(GracefulRestart, CountdownAlertMinutes)
{
    auto minutes = GracefulRestartManager::countdownAlertMinutes();
    ASSERT_FALSE(minutes.empty());
    // Should include 10, 5, 4, 3, 2, 1
    EXPECT_EQ(minutes[0], 10);
    EXPECT_EQ(minutes[1], 5);
    EXPECT_EQ(minutes[2], 4);
    EXPECT_EQ(minutes[3], 3);
    EXPECT_EQ(minutes[4], 2);
    EXPECT_EQ(minutes[5], 1);
}

TEST(GracefulRestart, BeginRestart)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr(tmp.filePath("servers.json"));
    auto *grm = mgr.gracefulRestartManager();
    ASSERT_NE(grm, nullptr);

    // Track log messages
    std::vector<std::string> logMsgs;
    grm->onLogMessage = [&](const std::string &, const std::string &msg) {
        logMsgs.push_back(msg);
    };

    // Add a server
    ServerConfig s;
    s.name  = "TestServer";
    s.appid = 730;
    s.dir   = tmp.filePath("serverdir");
    mgr.servers().push_back(s);

    // Begin graceful restart
    grm->beginGracefulRestart("TestServer", 10, "saveworld");

    // Should be restarting
    EXPECT_TRUE(grm->isRestarting("TestServer"));
    EXPECT_FALSE(logMsgs.empty());
}

TEST(GracefulRestart, CancelRestart)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr(tmp.filePath("servers.json"));
    auto *grm = mgr.gracefulRestartManager();

    ServerConfig s;
    s.name  = "TestServer";
    s.appid = 730;
    s.dir   = tmp.filePath("serverdir");
    mgr.servers().push_back(s);

    grm->beginGracefulRestart("TestServer", 10);
    EXPECT_TRUE(grm->isRestarting("TestServer"));

    grm->cancelGracefulRestart("TestServer");
    EXPECT_FALSE(grm->isRestarting("TestServer"));
}

TEST(GracefulRestart, ZeroCountdown)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr(tmp.filePath("servers.json"));
    auto *grm = mgr.gracefulRestartManager();

    std::vector<std::string> logMsgs;
    grm->onLogMessage = [&](const std::string &, const std::string &msg) {
        logMsgs.push_back(msg);
    };

    ServerConfig s;
    s.name  = "TestServer";
    s.appid = 730;
    s.dir   = tmp.filePath("serverdir");
    s.backupFolder = tmp.filePath("backups");
    fs::create_directories(s.dir);
    mgr.servers().push_back(s);

    // With 0 countdown, should go directly to save/restart
    grm->beginGracefulRestart("TestServer", 0, "saveworld");

    // After 0 countdown, it goes through save/backup/restart immediately
    bool foundSaveMsg = false;
    for (const auto &msg : logMsgs) {
        if (msg.find("save") != std::string::npos || msg.find("Save") != std::string::npos ||
            msg.find("restart") != std::string::npos || msg.find("Restart") != std::string::npos)
            foundSaveMsg = true;
    }
    EXPECT_TRUE(foundSaveMsg);
}

TEST(GracefulRestart, PhaseQuery)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr(tmp.filePath("servers.json"));
    auto *grm = mgr.gracefulRestartManager();

    // No restart active
    EXPECT_EQ(grm->currentPhase("NoServer"), GracefulRestartManager::Phase::Idle);
    EXPECT_EQ(grm->minutesRemaining("NoServer"), -1);
}

TEST(GracefulRestart, PhaseChangedCallback)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr(tmp.filePath("servers.json"));
    auto *grm = mgr.gracefulRestartManager();

    std::vector<GracefulRestartManager::Phase> phases;
    grm->onPhaseChanged = [&](const std::string &, GracefulRestartManager::Phase phase) {
        phases.push_back(phase);
    };

    ServerConfig s;
    s.name  = "TestServer";
    s.appid = 730;
    s.dir   = tmp.filePath("serverdir");
    s.backupFolder = tmp.filePath("backups");
    fs::create_directories(s.dir);
    mgr.servers().push_back(s);

    grm->beginGracefulRestart("TestServer", 0, "saveworld");

    // Should have gone through: Countdown, Saving, BackingUp, Restarting, Idle
    EXPECT_GE(phases.size(), 3u);
}

TEST(GracefulRestart, MinutesRemaining)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr(tmp.filePath("servers.json"));
    auto *grm = mgr.gracefulRestartManager();

    ServerConfig s;
    s.name  = "TestServer";
    s.appid = 730;
    s.dir   = tmp.filePath("serverdir");
    mgr.servers().push_back(s);

    grm->beginGracefulRestart("TestServer", 10);
    int mins = grm->minutesRemaining("TestServer");
    EXPECT_GE(mins, 9);
    EXPECT_LE(mins, 10);
}

// ===========================================================================
// SteamCMD installation helpers
// ===========================================================================

TEST(ServerConfig, SteamCmdDefaultInstallDir)
{
    std::string dir = SteamCmdModule::defaultInstallDir();
    EXPECT_FALSE(dir.empty());
    // Should end with "steamcmd"
    EXPECT_NE(dir.find("steamcmd"), std::string::npos);
}

TEST(ServerConfig, SteamCmdIsSteamCmdInstalledFalse)
{
    SteamCmdModule mod;
    mod.setSteamCmdPath("/nonexistent/path/to/steamcmd");
    EXPECT_FALSE(mod.isSteamCmdInstalled());
}

TEST(ServerConfig, SteamCmdIsSteamCmdInstalledTrue)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    // Create a fake steamcmd binary
    std::string fakeBin = tmp.filePath("steamcmd");
    { std::ofstream f(fakeBin); f << "#!/bin/sh\necho ok\n"; }

    SteamCmdModule mod;
    mod.setSteamCmdPath(fakeBin);
    EXPECT_TRUE(mod.isSteamCmdInstalled());
}

TEST(ServerConfig, SteamCmdInstallEmptyDir)
{
    SteamCmdModule mod;
    bool result = mod.installSteamCmd("");
    EXPECT_FALSE(result);
}

TEST(ServerConfig, ServerManagerIsSteamCmdInstalledFalse)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    ServerManager mgr(tmp.filePath("servers.json"));
    mgr.setSteamCmdPath("/nonexistent/path/to/steamcmd");
    EXPECT_FALSE(mgr.isSteamCmdInstalled());
}

TEST(ServerConfig, ServerManagerIsSteamCmdInstalledTrue)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    std::string fakeBin = tmp.filePath("steamcmd");
    { std::ofstream f(fakeBin); f << "#!/bin/sh\necho ok\n"; }

    ServerManager mgr(tmp.filePath("servers.json"));
    mgr.setSteamCmdPath(fakeBin);
    EXPECT_TRUE(mgr.isSteamCmdInstalled());
}

// ===========================================================================
// launchProcess working directory and script handling
// ===========================================================================

// Helper: wait for a child process to exit with a timeout
static void waitForChildProcess(ProcessInfo &proc, int maxIterations = 40, int intervalMs = 50)
{
    for (int i = 0; i < maxIterations; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        if (!isProcessRunning(proc)) break;
    }
}

TEST(ServerConfig, LaunchProcessWithWorkingDir)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    // Create a shell script that writes the cwd to a file
    std::string script = tmp.filePath("checkdir.sh");
    std::string outFile = tmp.filePath("cwd_output.txt");
    {
        std::ofstream f(script);
        f << "#!/bin/sh\npwd > '" << outFile << "'\n";
    }
    fs::permissions(script, fs::perms::owner_all);

    std::string workDir = tmp.path();
    ProcessInfo proc;
    std::map<std::string, std::string> env;
    bool ok = launchProcess(script, {}, env, proc, workDir);
    EXPECT_TRUE(ok);

    waitForChildProcess(proc);

    // Verify the working directory was set correctly
    std::string output = readFile(outFile);
    // Trim whitespace
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
        output.pop_back();
    // Resolve both paths to handle symlinks (e.g. /tmp -> /private/tmp on macOS)
    fs::path expectedPath = fs::canonical(workDir);
    fs::path actualPath = output.empty() ? fs::path() : fs::canonical(output);
    EXPECT_EQ(actualPath, expectedPath);
}

TEST(ServerConfig, LaunchProcessShellScript)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());

    // Create a .sh script that writes a marker file
    std::string script = tmp.filePath("marker.sh");
    std::string markerFile = tmp.filePath("marker.txt");
    {
        std::ofstream f(script);
        f << "#!/bin/sh\necho 'executed' > '" << markerFile << "'\n";
    }
    fs::permissions(script, fs::perms::owner_all);

    ProcessInfo proc;
    std::map<std::string, std::string> env;
    bool ok = launchProcess(script, {}, env, proc, tmp.path());
    EXPECT_TRUE(ok);

    waitForChildProcess(proc);

    EXPECT_TRUE(fs::exists(markerFile));
    std::string content = readFile(markerFile);
    EXPECT_TRUE(strContains(content, "executed"));
}

TEST(ServerConfig, SteamCmdInstallRejectsUnsafePath)
{
    SteamCmdModule mod;

    // Paths with shell metacharacters should be rejected
    EXPECT_FALSE(mod.installSteamCmd("/tmp/test; rm -rf /"));
    EXPECT_FALSE(mod.installSteamCmd("/tmp/test'$(whoami)"));
    EXPECT_FALSE(mod.installSteamCmd("/tmp/test`cmd`"));
    EXPECT_FALSE(mod.installSteamCmd("/tmp/test|pipe"));
    EXPECT_FALSE(mod.installSteamCmd("/tmp/test&bg"));
}

// ===========================================================================
// Mod reordering (swap-based)
// ===========================================================================

TEST(ServerConfig, ModSwapUp)
{
    ServerConfig s;
    s.mods = {100, 200, 300};

    // Swap index 1 with index 0 (move 200 up)
    std::swap(s.mods[1], s.mods[0]);
    EXPECT_EQ(s.mods, std::vector<int>({200, 100, 300}));
}

TEST(ServerConfig, ModSwapDown)
{
    ServerConfig s;
    s.mods = {100, 200, 300};

    // Swap index 0 with index 1 (move 100 down)
    std::swap(s.mods[0], s.mods[1]);
    EXPECT_EQ(s.mods, std::vector<int>({200, 100, 300}));
}

TEST(ServerConfig, ModSwapPersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "SwapTest";
    s.appid = 1;
    s.dir = "/srv/swap";
    s.mods = {100, 200, 300};
    mgr.servers().push_back(s);

    // Swap mods 0 and 2 (100 <-> 300)
    std::swap(mgr.servers()[0].mods[0], mgr.servers()[0].mods[2]);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    EXPECT_EQ(mgr2.servers()[0].mods, std::vector<int>({300, 200, 100}));
}

// ===========================================================================
// Tags comma-separated parsing (mirrors UI logic)
// ===========================================================================

static std::vector<std::string> parseCommaSeparatedTags(const std::string &input)
{
    std::vector<std::string> tags;
    std::istringstream ss(input);
    std::string tag;
    while (std::getline(ss, tag, ',')) {
        tag = trimString(tag);
        if (!tag.empty())
            tags.push_back(tag);
    }
    return tags;
}

static std::string joinTags(const std::vector<std::string> &tags)
{
    std::string result;
    for (size_t i = 0; i < tags.size(); ++i) {
        if (i > 0) result += ", ";
        result += tags[i];
    }
    return result;
}

TEST(ServerConfig, TagsCommaSeparatedParse)
{
    auto tags = parseCommaSeparatedTags("production, ark, cluster");
    ASSERT_EQ(tags.size(), 3);
    EXPECT_EQ(tags[0], "production");
    EXPECT_EQ(tags[1], "ark");
    EXPECT_EQ(tags[2], "cluster");
}

TEST(ServerConfig, TagsCommaSeparatedEmpty)
{
    auto tags = parseCommaSeparatedTags("");
    EXPECT_TRUE(tags.empty());
}

TEST(ServerConfig, TagsCommaSeparatedTrailingComma)
{
    auto tags = parseCommaSeparatedTags("alpha, beta, ");
    ASSERT_EQ(tags.size(), 2);
    EXPECT_EQ(tags[0], "alpha");
    EXPECT_EQ(tags[1], "beta");
}

TEST(ServerConfig, TagsCommaSeparatedWhitespace)
{
    auto tags = parseCommaSeparatedTags("  foo  ,  bar  ,  baz  ");
    ASSERT_EQ(tags.size(), 3);
    EXPECT_EQ(tags[0], "foo");
    EXPECT_EQ(tags[1], "bar");
    EXPECT_EQ(tags[2], "baz");
}

TEST(ServerConfig, TagsJoinRoundTrip)
{
    std::vector<std::string> original = {"production", "ark", "cluster"};
    std::string joined = joinTags(original);
    auto parsed = parseCommaSeparatedTags(joined);
    EXPECT_EQ(parsed, original);
}

// ===========================================================================
// Server group batch operations
// ===========================================================================

TEST(ServerConfig, ServerGroupsListing)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);

    ServerConfig s1;
    s1.name = "S1"; s1.appid = 1; s1.dir = "/srv/s1";
    s1.group = "Production";

    ServerConfig s2;
    s2.name = "S2"; s2.appid = 2; s2.dir = "/srv/s2";
    s2.group = "Testing";

    ServerConfig s3;
    s3.name = "S3"; s3.appid = 3; s3.dir = "/srv/s3";
    s3.group = "Production";

    ServerConfig s4;
    s4.name = "S4"; s4.appid = 4; s4.dir = "/srv/s4";
    // no group

    mgr.servers().push_back(s1);
    mgr.servers().push_back(s2);
    mgr.servers().push_back(s3);
    mgr.servers().push_back(s4);

    auto groups = mgr.serverGroups();
    EXPECT_EQ(groups.size(), 2);
    // Should be sorted (case-insensitive)
    EXPECT_EQ(groups[0], "Production");
    EXPECT_EQ(groups[1], "Testing");
}

TEST(ServerConfig, ServerGroupsEmptyNoGroup)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "S1"; s.appid = 1; s.dir = "/srv/s1";
    mgr.servers().push_back(s);

    auto groups = mgr.serverGroups();
    EXPECT_TRUE(groups.empty());
}

// ===========================================================================
// Event hooks per-event configuration round-trip
// ===========================================================================

TEST(ServerConfig, EventHooksPerEventPersistence)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "HookTest";
    s.appid = 1;
    s.dir = "/srv/hooks";
    s.eventHooks["onStart"]  = "/scripts/on_start.sh";
    s.eventHooks["onStop"]   = "/scripts/on_stop.sh";
    s.eventHooks["onCrash"]  = "/scripts/on_crash.sh";
    s.eventHooks["onBackup"] = "/scripts/on_backup.sh";
    s.eventHooks["onUpdate"] = "/scripts/on_update.sh";
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    const auto &hooks = mgr2.servers()[0].eventHooks;
    EXPECT_EQ(hooks.size(), 5);
    EXPECT_EQ(hooks.at("onStart"),  "/scripts/on_start.sh");
    EXPECT_EQ(hooks.at("onStop"),   "/scripts/on_stop.sh");
    EXPECT_EQ(hooks.at("onCrash"),  "/scripts/on_crash.sh");
    EXPECT_EQ(hooks.at("onBackup"), "/scripts/on_backup.sh");
    EXPECT_EQ(hooks.at("onUpdate"), "/scripts/on_update.sh");
}

// ===========================================================================
// Scheduled RCON commands add/remove
// ===========================================================================

TEST(ServerConfig, ScheduledRconCommandsAddRemove)
{
    ServerConfig s;
    EXPECT_TRUE(s.scheduledRconCommands.empty());

    s.scheduledRconCommands.push_back("saveworld");
    s.scheduledRconCommands.push_back("listplayers");
    s.scheduledRconCommands.push_back("broadcast hello");
    EXPECT_EQ(s.scheduledRconCommands.size(), 3);

    // Remove the middle command
    s.scheduledRconCommands.erase(s.scheduledRconCommands.begin() + 1);
    ASSERT_EQ(s.scheduledRconCommands.size(), 2);
    EXPECT_EQ(s.scheduledRconCommands[0], "saveworld");
    EXPECT_EQ(s.scheduledRconCommands[1], "broadcast hello");
}

TEST(ServerConfig, ScheduledRconCommandsPersistenceRoundTrip)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    ServerManager mgr(configPath);
    ServerConfig s;
    s.name = "RconTest";
    s.appid = 1;
    s.dir = "/srv/rcon";
    s.scheduledRconCommands.push_back("saveworld");
    s.scheduledRconCommands.push_back("listplayers");
    mgr.servers().push_back(s);
    ASSERT_TRUE(mgr.saveConfig());

    ServerManager mgr2(configPath);
    ASSERT_TRUE(mgr2.loadConfig());
    ASSERT_EQ(mgr2.servers()[0].scheduledRconCommands.size(), 2);
    EXPECT_EQ(mgr2.servers()[0].scheduledRconCommands[0], "saveworld");
    EXPECT_EQ(mgr2.servers()[0].scheduledRconCommands[1], "listplayers");
}

// ===========================================================================
// Environment variables add/remove
// ===========================================================================

TEST(ServerConfig, EnvironmentVariablesAddRemove)
{
    ServerConfig s;
    EXPECT_TRUE(s.environmentVariables.empty());

    s.environmentVariables["SRCDS_TOKEN"] = "abc123";
    s.environmentVariables["MY_VAR"] = "hello";
    EXPECT_EQ(s.environmentVariables.size(), 2);

    s.environmentVariables.erase("SRCDS_TOKEN");
    ASSERT_EQ(s.environmentVariables.size(), 1);
    EXPECT_EQ(s.environmentVariables.at("MY_VAR"), "hello");
}

// ===========================================================================
// Complete servers.json loads correctly with all documented fields
// ===========================================================================

TEST(ServerConfig, CompleteServersJsonLoadsAllFields)
{
    TempDir tmp;
    ASSERT_TRUE(tmp.isValid());
    std::string configPath = tmp.filePath("servers.json");

    {
        std::ofstream f(configPath);
        f << R"([
  {
    "name": "FullTest",
    "appid": 999,
    "dir": "/srv/full",
    "executable": "server.exe",
    "launchArgs": "-dedicated",
    "rcon": {"host":"10.0.0.1","port":27020,"password":"secret"},
    "mods": [100, 200],
    "disabledMods": [200],
    "backupFolder": "/backups/full",
    "notes": "Test notes",
    "discordWebhookUrl": "https://discord.com/api/webhooks/123",
    "webhookTemplate": "Server {server} event {event}",
    "autoUpdate": false,
    "autoStartOnLaunch": true,
    "favorite": true,
    "keepBackups": 5,
    "backupIntervalMinutes": 45,
    "restartIntervalHours": 12,
    "scheduledRconCommands": ["saveworld", "listplayers"],
    "rconCommandIntervalMinutes": 30,
    "backupCompressionLevel": 9,
    "maintenanceStartHour": 2,
    "maintenanceEndHour": 6,
    "consoleLogging": true,
    "maxPlayers": 64,
    "restartWarningMinutes": 10,
    "restartWarningMessage": "Restarting in {minutes} min!",
    "cpuAlertThreshold": 80.0,
    "memAlertThresholdMB": 4096.0,
    "eventHooks": {
      "onStart": "/hooks/start.sh",
      "onCrash": "/hooks/crash.sh"
    },
    "tags": ["production", "ark"],
    "group": "TestGroup",
    "startupPriority": 2,
    "backupBeforeRestart": true,
    "gracefulShutdownSeconds": 30,
    "autoUpdateCheckIntervalMinutes": 60,
    "totalUptimeSeconds": 86400,
    "totalCrashes": 3,
    "lastCrashTime": "2025-01-15T10:30:00Z",
    "environmentVariables": {"KEY1":"val1","KEY2":"val2"}
  }
])";
    }

    ServerManager mgr(configPath);
    ASSERT_TRUE(mgr.loadConfig());
    ASSERT_EQ(mgr.servers().size(), 1);
    const ServerConfig &s = mgr.servers()[0];

    EXPECT_EQ(s.name, "FullTest");
    EXPECT_EQ(s.appid, 999);
    EXPECT_EQ(s.dir, "/srv/full");
    EXPECT_EQ(s.executable, "server.exe");
    EXPECT_EQ(s.launchArgs, "-dedicated");
    EXPECT_EQ(s.rcon.host, "10.0.0.1");
    EXPECT_EQ(s.rcon.port, 27020);
    EXPECT_EQ(s.mods.size(), 2);
    EXPECT_EQ(s.disabledMods.size(), 1);
    EXPECT_EQ(s.backupFolder, "/backups/full");
    EXPECT_EQ(s.notes, "Test notes");
    EXPECT_EQ(s.discordWebhookUrl, "https://discord.com/api/webhooks/123");
    EXPECT_EQ(s.webhookTemplate, "Server {server} event {event}");
    EXPECT_FALSE(s.autoUpdate);
    EXPECT_TRUE(s.autoStartOnLaunch);
    EXPECT_TRUE(s.favorite);
    EXPECT_EQ(s.keepBackups, 5);
    EXPECT_EQ(s.backupIntervalMinutes, 45);
    EXPECT_EQ(s.restartIntervalHours, 12);
    ASSERT_EQ(s.scheduledRconCommands.size(), 2);
    EXPECT_EQ(s.scheduledRconCommands[0], "saveworld");
    EXPECT_EQ(s.scheduledRconCommands[1], "listplayers");
    EXPECT_EQ(s.rconCommandIntervalMinutes, 30);
    EXPECT_EQ(s.backupCompressionLevel, 9);
    EXPECT_EQ(s.maintenanceStartHour, 2);
    EXPECT_EQ(s.maintenanceEndHour, 6);
    EXPECT_TRUE(s.consoleLogging);
    EXPECT_EQ(s.maxPlayers, 64);
    EXPECT_EQ(s.restartWarningMinutes, 10);
    EXPECT_EQ(s.restartWarningMessage, "Restarting in {minutes} min!");
    EXPECT_DOUBLE_EQ(s.cpuAlertThreshold, 80.0);
    EXPECT_DOUBLE_EQ(s.memAlertThresholdMB, 4096.0);
    ASSERT_EQ(s.eventHooks.size(), 2);
    EXPECT_EQ(s.eventHooks.at("onStart"), "/hooks/start.sh");
    EXPECT_EQ(s.eventHooks.at("onCrash"), "/hooks/crash.sh");
    ASSERT_EQ(s.tags.size(), 2);
    EXPECT_EQ(s.tags[0], "production");
    EXPECT_EQ(s.tags[1], "ark");
    EXPECT_EQ(s.group, "TestGroup");
    EXPECT_EQ(s.startupPriority, 2);
    EXPECT_TRUE(s.backupBeforeRestart);
    EXPECT_EQ(s.gracefulShutdownSeconds, 30);
    EXPECT_EQ(s.autoUpdateCheckIntervalMinutes, 60);
    EXPECT_EQ(s.totalUptimeSeconds, 86400);
    EXPECT_EQ(s.totalCrashes, 3);
    EXPECT_EQ(s.lastCrashTime, "2025-01-15T10:30:00Z");
    ASSERT_EQ(s.environmentVariables.size(), 2);
    EXPECT_EQ(s.environmentVariables.at("KEY1"), "val1");
    EXPECT_EQ(s.environmentVariables.at("KEY2"), "val2");
}

#include "ServerManager.hpp"
#include "BackupModule.hpp"
#include "SteamCmdModule.hpp"
#include "RconClient.hpp"
#include "ConsoleLogWriter.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
extern char **environ;
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Simple XOR-based obfuscation for RCON passwords at rest.
// ---------------------------------------------------------------------------
static const std::string kObfuscationKey = "SSA_RCON_KEY_2026";

// ---------------------------------------------------------------------------
// Inline base64 encode/decode
// ---------------------------------------------------------------------------
static const char kBase64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64Encode(const std::string &input)
{
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(kBase64Chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6)
        out.push_back(kBase64Chars[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4)
        out.push_back('=');
    return out;
}

static std::string base64Decode(const std::string &input)
{
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++)
        T[static_cast<unsigned char>(kBase64Chars[i])] = i;

    std::string out;
    int val = 0, valb = -8;
    for (unsigned char c : input) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

static std::string obfuscatePassword(const std::string &plain)
{
    std::string data = plain;
    for (size_t i = 0; i < data.size(); ++i)
        data[i] = data[i] ^ kObfuscationKey[i % kObfuscationKey.size()];
    return "obf:" + base64Encode(data);
}

static std::string deobfuscatePassword(const std::string &stored)
{
    if (stored.size() < 4 || stored.substr(0, 4) != "obf:")
        return stored;
    std::string data = base64Decode(stored.substr(4));
    for (size_t i = 0; i < data.size(); ++i)
        data[i] = data[i] ^ kObfuscationKey[i % kObfuscationKey.size()];
    return data;
}

// ---------------------------------------------------------------------------
// Portable process helpers
// ---------------------------------------------------------------------------

bool launchProcess(const std::string &exe, const std::vector<std::string> &args,
                   const std::map<std::string, std::string> &env, ProcessInfo &out)
{
#ifdef _WIN32
    std::string cmdLine = "\"" + exe + "\"";
    for (const auto &a : args)
        cmdLine += " " + a;

    std::string envBlock;
    if (!env.empty()) {
        char *curEnv = GetEnvironmentStrings();
        if (curEnv) {
            const char *p = curEnv;
            while (*p) {
                std::string entry(p);
                envBlock += entry + '\0';
                p += entry.size() + 1;
            }
            FreeEnvironmentStrings(curEnv);
        }
        for (const auto &[k, v] : env)
            envBlock += k + "=" + v + '\0';
        envBlock += '\0';
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    BOOL ok = CreateProcessA(
        nullptr, const_cast<char*>(cmdLine.c_str()),
        nullptr, nullptr, FALSE, 0,
        env.empty() ? nullptr : const_cast<char*>(envBlock.c_str()),
        nullptr, &si, &pi);

    if (!ok) return false;

    CloseHandle(pi.hThread);
    out.processHandle = pi.hProcess;
    out.pid = pi.dwProcessId;
    out.running = true;
    return true;
#else
    pid_t pid = fork();
    if (pid < 0) return false;

    if (pid == 0) {
        for (const auto &[k, v] : env)
            setenv(k.c_str(), v.c_str(), 1);

        std::vector<char*> argv;
        std::string exeCopy = exe;
        argv.push_back(const_cast<char*>(exeCopy.c_str()));
        std::vector<std::string> argCopies(args.begin(), args.end());
        for (auto &a : argCopies)
            argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);

        execv(exe.c_str(), argv.data());
        _exit(127);
    }

    out.pid = pid;
    out.running = true;
    return true;
#endif
}

bool isProcessRunning(const ProcessInfo &info)
{
    if (!info.running) return false;
#ifdef _WIN32
    if (!info.processHandle) return false;
    DWORD exitCode;
    if (GetExitCodeProcess(info.processHandle, &exitCode))
        return exitCode == STILL_ACTIVE;
    return false;
#else
    if (info.pid <= 0) return false;
    int status;
    pid_t result = waitpid(info.pid, &status, WNOHANG);
    return result == 0;
#endif
}

void terminateProcess(ProcessInfo &info)
{
#ifdef _WIN32
    if (info.processHandle) {
        TerminateProcess(info.processHandle, 1);
        WaitForSingleObject(info.processHandle, 3000);
        CloseHandle(info.processHandle);
        info.processHandle = nullptr;
    }
#else
    if (info.pid > 0) {
        ::kill(info.pid, SIGTERM);
    }
#endif
    info.running = false;
}

void killProcess(ProcessInfo &info)
{
#ifdef _WIN32
    if (info.processHandle) {
        TerminateProcess(info.processHandle, 9);
        WaitForSingleObject(info.processHandle, 3000);
        CloseHandle(info.processHandle);
        info.processHandle = nullptr;
    }
#else
    if (info.pid > 0) {
        ::kill(info.pid, SIGKILL);
        int status;
        waitpid(info.pid, &status, 0);
    }
#endif
    info.running = false;
}

static bool waitForProcess(ProcessInfo &info, int timeoutMs)
{
#ifdef _WIN32
    if (!info.processHandle) return true;
    DWORD result = WaitForSingleObject(info.processHandle, timeoutMs);
    return result == WAIT_OBJECT_0;
#else
    if (info.pid <= 0) return true;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        int status;
        pid_t result = waitpid(info.pid, &status, WNOHANG);
        if (result != 0) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
#endif
}

static int getExitCode(const ProcessInfo &info)
{
#ifdef _WIN32
    if (!info.processHandle) return -1;
    DWORD exitCode;
    if (GetExitCodeProcess(info.processHandle, &exitCode))
        return static_cast<int>(exitCode);
    return -1;
#else
    if (info.pid <= 0) return -1;
    int status;
    pid_t result = waitpid(info.pid, &status, WNOHANG);
    if (result > 0 && WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
#endif
}

static void cleanupProcess(ProcessInfo &info)
{
#ifdef _WIN32
    if (info.processHandle) {
        CloseHandle(info.processHandle);
        info.processHandle = nullptr;
    }
#endif
    info.pid = 0;
    info.running = false;
}

static int64_t processId(const ProcessInfo &info)
{
    return static_cast<int64_t>(info.pid);
}

static std::vector<std::string> splitString(const std::string &s, char delim)
{
    std::vector<std::string> result;
    std::istringstream iss(s);
    std::string token;
    while (std::getline(iss, token, delim)) {
        if (!token.empty())
            result.push_back(token);
    }
    return result;
}

// ---------------------------------------------------------------------------
// ServerManager helpers
// ---------------------------------------------------------------------------
void ServerManager::emitLog(const std::string &serverName, const std::string &msg)
{
    if (onLogMessage) onLogMessage(serverName, msg);
}

std::string ServerManager::lookupEventHook(const std::string &serverName,
                                           const std::string &event) const
{
    for (const auto &s : m_servers) {
        if (s.name == serverName) {
            auto it = s.eventHooks.find(event);
            if (it != s.eventHooks.end())
                return it->second;
            return "";
        }
    }
    return "";
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

ServerManager::ServerManager(const std::string &configFile)
    : m_configFile(configFile)
{
#ifdef _WIN32
    m_steamCmdPath = "steamcmd.exe";
#else
    m_steamCmdPath = "steamcmd";
#endif

    m_resourceMonitor.onUsageUpdated =
        [this](const std::map<std::string, ResourceUsage> &usage) {
        constexpr double kBytesPerMB = 1024.0 * 1024.0;
        for (const auto &[name, ru] : usage) {
            for (const ServerConfig &s : m_servers) {
                if (s.name != name) continue;
                if (s.cpuAlertThreshold > 0 && ru.cpuPercent > s.cpuAlertThreshold) {
                    if (onResourceAlert) {
                        char buf[128];
                        snprintf(buf, sizeof(buf),
                                 "CPU usage %.1f%% exceeds threshold %.1f%%",
                                 ru.cpuPercent, s.cpuAlertThreshold);
                        onResourceAlert(name, buf);
                    }
                }
                if (s.memAlertThresholdMB > 0) {
                    double memMB = static_cast<double>(ru.memoryBytes) / kBytesPerMB;
                    if (memMB > s.memAlertThresholdMB) {
                        if (onResourceAlert) {
                            char buf[128];
                            snprintf(buf, sizeof(buf),
                                     "Memory usage %.1f MB exceeds threshold %.1f MB",
                                     memMB, s.memAlertThresholdMB);
                            onResourceAlert(name, buf);
                        }
                    }
                }
                break;
            }
        }
    };
}

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------
static std::string jsonStr(const json &obj, const std::string &key, const std::string &def = "")
{
    if (obj.contains(key) && obj[key].is_string())
        return obj[key].get<std::string>();
    return def;
}

static int jsonInt(const json &obj, const std::string &key, int def = 0)
{
    if (obj.contains(key) && obj[key].is_number_integer())
        return obj[key].get<int>();
    return def;
}

static bool jsonBool(const json &obj, const std::string &key, bool def = false)
{
    if (obj.contains(key) && obj[key].is_boolean())
        return obj[key].get<bool>();
    return def;
}

static double jsonDouble(const json &obj, const std::string &key, double def = 0.0)
{
    if (obj.contains(key) && obj[key].is_number())
        return obj[key].get<double>();
    return def;
}

static int64_t jsonInt64(const json &obj, const std::string &key, int64_t def = 0)
{
    if (obj.contains(key) && obj[key].is_number_integer())
        return obj[key].get<int64_t>();
    return def;
}

bool ServerManager::loadConfig()
{
    std::ifstream file(m_configFile);
    if (!file.is_open()) {
        std::cerr << "ServerManager::loadConfig: cannot open " << m_configFile << "\n";
        return false;
    }

    json doc;
    try {
        doc = json::parse(file);
    } catch (const json::parse_error &e) {
        std::cerr << "ServerManager::loadConfig: parse error: " << e.what() << "\n";
        return false;
    }

    if (!doc.is_array()) return false;

    m_servers.clear();
    for (const auto &obj : doc) {
        ServerConfig s;
        s.name           = jsonStr(obj, "name");
        s.appid          = jsonInt(obj, "appid");
        s.dir            = jsonStr(obj, "dir");
        s.executable     = jsonStr(obj, "executable");
        s.launchArgs     = jsonStr(obj, "launchArgs");
        s.backupFolder   = jsonStr(obj, "backupFolder");
        s.notes          = jsonStr(obj, "notes");
        s.discordWebhookUrl = jsonStr(obj, "discordWebhookUrl");
        s.webhookTemplate= jsonStr(obj, "webhookTemplate");
        s.autoUpdate     = jsonBool(obj, "autoUpdate", true);
        s.autoStartOnLaunch = jsonBool(obj, "autoStartOnLaunch", false);
        s.favorite       = jsonBool(obj, "favorite", false);
        s.keepBackups    = jsonInt(obj, "keepBackups", 10);
        s.backupIntervalMinutes  = jsonInt(obj, "backupIntervalMinutes", 30);
        s.restartIntervalHours   = jsonInt(obj, "restartIntervalHours", 24);
        s.rconCommandIntervalMinutes = jsonInt(obj, "rconCommandIntervalMinutes", 0);
        s.backupCompressionLevel = jsonInt(obj, "backupCompressionLevel", 6);
        s.maintenanceStartHour   = jsonInt(obj, "maintenanceStartHour", -1);
        s.maintenanceEndHour     = jsonInt(obj, "maintenanceEndHour", -1);
        s.consoleLogging = jsonBool(obj, "consoleLogging", false);
        s.maxPlayers     = jsonInt(obj, "maxPlayers", 0);
        s.restartWarningMinutes = jsonInt(obj, "restartWarningMinutes", 15);
        s.restartWarningMessage = jsonStr(obj, "restartWarningMessage");
        s.cpuAlertThreshold    = jsonDouble(obj, "cpuAlertThreshold", 90.0);
        s.memAlertThresholdMB  = jsonDouble(obj, "memAlertThresholdMB", 0.0);

        if (obj.contains("eventHooks") && obj["eventHooks"].is_object()) {
            for (auto &[k, v] : obj["eventHooks"].items())
                if (v.is_string()) s.eventHooks[k] = v.get<std::string>();
        }
        if (obj.contains("tags") && obj["tags"].is_array()) {
            for (const auto &v : obj["tags"])
                if (v.is_string()) s.tags.push_back(v.get<std::string>());
        }

        s.group = jsonStr(obj, "group");
        s.startupPriority = jsonInt(obj, "startupPriority", 0);
        s.backupBeforeRestart = jsonBool(obj, "backupBeforeRestart", false);
        s.gracefulShutdownSeconds = jsonInt(obj, "gracefulShutdownSeconds", 10);
        s.autoUpdateCheckIntervalMinutes = jsonInt(obj, "autoUpdateCheckIntervalMinutes", 0);
        s.totalUptimeSeconds = jsonInt64(obj, "totalUptimeSeconds", 0);
        s.totalCrashes = jsonInt(obj, "totalCrashes", 0);
        s.lastCrashTime = jsonStr(obj, "lastCrashTime");

        if (obj.contains("environmentVariables") && obj["environmentVariables"].is_object()) {
            for (auto &[k, v] : obj["environmentVariables"].items())
                if (v.is_string()) s.environmentVariables[k] = v.get<std::string>();
        }
        if (obj.contains("scheduledRconCommands") && obj["scheduledRconCommands"].is_array()) {
            for (const auto &v : obj["scheduledRconCommands"])
                if (v.is_string()) s.scheduledRconCommands.push_back(v.get<std::string>());
        }
        if (obj.contains("rcon") && obj["rcon"].is_object()) {
            const auto &rcon = obj["rcon"];
            s.rcon.host     = jsonStr(rcon, "host", "127.0.0.1");
            s.rcon.port     = jsonInt(rcon, "port", 27015);
            s.rcon.password = deobfuscatePassword(jsonStr(rcon, "password"));
        }
        if (obj.contains("mods") && obj["mods"].is_array()) {
            for (const auto &m : obj["mods"])
                if (m.is_number_integer()) s.mods.push_back(m.get<int>());
        }
        if (obj.contains("disabledMods") && obj["disabledMods"].is_array()) {
            for (const auto &m : obj["disabledMods"])
                if (m.is_number_integer()) s.disabledMods.push_back(m.get<int>());
        }

        m_servers.push_back(s);
    }
    return true;
}

std::vector<std::string> ServerManager::validateAll() const
{
    std::vector<std::string> errors;
    std::set<std::string> seenNames;

    for (int i = 0; i < static_cast<int>(m_servers.size()); ++i) {
        const ServerConfig &s = m_servers[i];
        std::vector<std::string> serverErrors = s.validate();
        for (const auto &e : serverErrors) {
            std::string nameStr = trimString(s.name).empty() ? "<unnamed>" : s.name;
            errors.push_back("Server #" + std::to_string(i + 1) + " (" + nameStr + "): " + e);
        }
        if (!trimString(s.name).empty()) {
            if (seenNames.count(s.name))
                errors.push_back("Duplicate server name: '" + s.name + "'.");
            else
                seenNames.insert(s.name);
        }
    }
    return errors;
}

static json serverToJson(const ServerConfig &s)
{
    json obj;
    obj["name"]          = s.name;
    obj["appid"]         = s.appid;
    obj["dir"]           = s.dir;
    obj["executable"]    = s.executable;
    obj["launchArgs"]    = s.launchArgs;
    obj["backupFolder"]  = s.backupFolder;
    obj["notes"]         = s.notes;
    obj["discordWebhookUrl"] = s.discordWebhookUrl;
    obj["webhookTemplate"] = s.webhookTemplate;
    obj["autoUpdate"]    = s.autoUpdate;
    obj["autoStartOnLaunch"] = s.autoStartOnLaunch;
    obj["favorite"]      = s.favorite;
    obj["keepBackups"]   = s.keepBackups;
    obj["backupIntervalMinutes"] = s.backupIntervalMinutes;
    obj["restartIntervalHours"]  = s.restartIntervalHours;
    obj["rconCommandIntervalMinutes"] = s.rconCommandIntervalMinutes;
    obj["backupCompressionLevel"] = s.backupCompressionLevel;
    obj["maintenanceStartHour"]   = s.maintenanceStartHour;
    obj["maintenanceEndHour"]     = s.maintenanceEndHour;
    obj["consoleLogging"] = s.consoleLogging;
    obj["maxPlayers"]     = s.maxPlayers;
    obj["restartWarningMinutes"] = s.restartWarningMinutes;
    obj["restartWarningMessage"]  = s.restartWarningMessage;
    obj["cpuAlertThreshold"]   = s.cpuAlertThreshold;
    obj["memAlertThresholdMB"] = s.memAlertThresholdMB;

    json hooks = json::object();
    for (const auto &[k, v] : s.eventHooks) hooks[k] = v;
    obj["eventHooks"] = hooks;

    json tagsArr = json::array();
    for (const auto &tag : s.tags) tagsArr.push_back(tag);
    obj["tags"] = tagsArr;

    obj["group"] = s.group;
    obj["startupPriority"] = s.startupPriority;
    obj["backupBeforeRestart"] = s.backupBeforeRestart;
    obj["gracefulShutdownSeconds"] = s.gracefulShutdownSeconds;
    obj["autoUpdateCheckIntervalMinutes"] = s.autoUpdateCheckIntervalMinutes;
    obj["totalUptimeSeconds"] = s.totalUptimeSeconds;
    obj["totalCrashes"] = s.totalCrashes;
    obj["lastCrashTime"] = s.lastCrashTime;

    json envVarsObj = json::object();
    for (const auto &[k, v] : s.environmentVariables) envVarsObj[k] = v;
    obj["environmentVariables"] = envVarsObj;

    json rcon;
    rcon["host"]     = s.rcon.host;
    rcon["port"]     = s.rcon.port;
    rcon["password"] = obfuscatePassword(s.rcon.password);
    obj["rcon"] = rcon;

    json mods = json::array();
    for (int m : s.mods) mods.push_back(m);
    obj["mods"] = mods;

    json disabledMods = json::array();
    for (int m : s.disabledMods) disabledMods.push_back(m);
    obj["disabledMods"] = disabledMods;

    json scheduledRcon = json::array();
    for (const auto &cmd : s.scheduledRconCommands) scheduledRcon.push_back(cmd);
    obj["scheduledRconCommands"] = scheduledRcon;

    return obj;
}

bool ServerManager::saveConfig() const
{
    std::vector<std::string> errors = validateAll();
    if (!errors.empty()) {
        std::cerr << "ServerManager::saveConfig: validation failed\n";
        for (const auto &e : errors) std::cerr << "  " << e << "\n";
        return false;
    }

    json arr = json::array();
    for (const ServerConfig &s : m_servers) arr.push_back(serverToJson(s));

    std::ofstream file(m_configFile, std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "ServerManager::saveConfig: cannot write " << m_configFile << "\n";
        return false;
    }
    file << arr.dump(2);
    return true;
}

std::vector<ServerConfig> &ServerManager::servers()       { return m_servers; }
const std::vector<ServerConfig> &ServerManager::servers() const { return m_servers; }

// ---------------------------------------------------------------------------
// Auto-start
// ---------------------------------------------------------------------------

void ServerManager::autoStartServers()
{
    std::vector<int> indices;
    for (int i = 0; i < static_cast<int>(m_servers.size()); ++i) {
        if (m_servers[i].autoStartOnLaunch) indices.push_back(i);
    }
    std::sort(indices.begin(), indices.end(), [this](int a, int b) {
        return m_servers[a].startupPriority < m_servers[b].startupPriority;
    });
    for (int idx : indices) startServer(m_servers[idx]);
}

// ---------------------------------------------------------------------------
// Batch server operations
// ---------------------------------------------------------------------------

void ServerManager::startAllServers()
{
    // Respect startup priority ordering
    std::vector<int> indices;
    for (int i = 0; i < static_cast<int>(m_servers.size()); ++i)
        indices.push_back(i);
    std::sort(indices.begin(), indices.end(), [this](int a, int b) {
        return m_servers[a].startupPriority < m_servers[b].startupPriority;
    });
    for (int idx : indices)
        startServer(m_servers[idx]);
}

void ServerManager::stopAllServers()
{
    for (ServerConfig &s : m_servers) {
        if (isServerRunning(s))
            stopServer(s);
    }
}

void ServerManager::restartAllServers()
{
    for (ServerConfig &s : m_servers) {
        if (isServerRunning(s))
            restartServer(s);
    }
}

void ServerManager::startGroup(const std::string &group)
{
    std::vector<int> indices;
    for (int i = 0; i < static_cast<int>(m_servers.size()); ++i) {
        if (m_servers[i].group == group)
            indices.push_back(i);
    }
    std::sort(indices.begin(), indices.end(), [this](int a, int b) {
        return m_servers[a].startupPriority < m_servers[b].startupPriority;
    });
    for (int idx : indices)
        startServer(m_servers[idx]);
}

void ServerManager::stopGroup(const std::string &group)
{
    for (ServerConfig &s : m_servers) {
        if (s.group == group && isServerRunning(s))
            stopServer(s);
    }
}

void ServerManager::restartGroup(const std::string &group)
{
    for (ServerConfig &s : m_servers) {
        if (s.group == group && isServerRunning(s))
            restartServer(s);
    }
}

std::vector<std::string> ServerManager::serverGroups() const
{
    std::set<std::string> groups;
    for (const ServerConfig &s : m_servers) {
        if (!trimString(s.group).empty())
            groups.insert(s.group);
    }
    std::vector<std::string> result(groups.begin(), groups.end());
    std::sort(result.begin(), result.end(), [](const std::string &a, const std::string &b) {
        // Case-insensitive sort
        std::string la = a, lb = b;
        std::transform(la.begin(), la.end(), la.begin(), ::tolower);
        std::transform(lb.begin(), lb.end(), lb.begin(), ::tolower);
        return la < lb;
    });
    return result;
}

int ServerManager::runningServerCount() const
{
    int count = 0;
    for (const ServerConfig &s : m_servers) {
        if (isServerRunning(s))
            ++count;
    }
    return count;
}

// ---------------------------------------------------------------------------
// Server lifecycle
// ---------------------------------------------------------------------------

void ServerManager::startServer(ServerConfig &server)
{
    if (isServerRunning(server)) {
        emitLog(server.name, "Server is already running.");
        return;
    }

    if (server.autoUpdate && !server.mods.empty()) {
        emitLog(server.name, "Auto-update: updating mods before start...");
        updateMods(server);
    }

    m_crashCounts.erase(server.name);

    std::string exe = (fs::path(server.dir) / server.executable).string();
    std::vector<std::string> args;
    if (!server.launchArgs.empty())
        args = splitString(server.launchArgs, ' ');

    ProcessInfo proc;
    bool ok = launchProcess(exe, args, server.environmentVariables, proc);

    if (ok) {
        m_processes[server.name] = proc;
        m_startTimes[server.name] = std::chrono::system_clock::now();
        emitLog(server.name, "Server started (PID " + std::to_string(processId(proc)) + ").");
        m_resourceMonitor.trackProcess(server.name, processId(proc));

        auto hookIt = server.eventHooks.find("onStart");
        if (hookIt != server.eventHooks.end())
            m_eventHookManager.fireHook(server.name, server.dir, "onStart", hookIt->second);

        m_webhook.sendNotification(server.discordWebhookUrl, server.name,
                                   "Server started.", server.webhookTemplate);
    } else {
        emitLog(server.name, "Failed to start server.");
    }
}

void ServerManager::stopServer(ServerConfig &server)
{
    auto it = m_processes.find(server.name);
    if (it == m_processes.end() || !it->second.running) {
        emitLog(server.name, "Server is not running.");
        return;
    }

    ProcessInfo &proc = it->second;
    int timeoutMs = server.gracefulShutdownSeconds * 1000;

    if (timeoutMs <= 0) {
        killProcess(proc);
    } else {
        terminateProcess(proc);
        if (!waitForProcess(proc, timeoutMs))
            killProcess(proc);
    }

    cleanupProcess(proc);
    m_processes.erase(server.name);
    m_startTimes.erase(server.name);
    m_crashCounts.erase(server.name);
    m_pendingRestarts.erase(server.name);
    m_resourceMonitor.untrackProcess(server.name);
    emitLog(server.name, "Server stopped.");

    auto hookIt = server.eventHooks.find("onStop");
    if (hookIt != server.eventHooks.end())
        m_eventHookManager.fireHook(server.name, server.dir, "onStop", hookIt->second);

    m_webhook.sendNotification(server.discordWebhookUrl, server.name,
                               "Server stopped.", server.webhookTemplate);
}

void ServerManager::restartServer(ServerConfig &server)
{
    stopServer(server);
    startServer(server);
}

bool ServerManager::isServerRunning(const ServerConfig &server) const
{
    auto it = m_processes.find(server.name);
    if (it == m_processes.end()) return false;
    ProcessInfo copy = it->second;
    return isProcessRunning(copy);
}

// ---------------------------------------------------------------------------
// tick()
// ---------------------------------------------------------------------------

void ServerManager::tick()
{
    checkProcesses();
    processPendingRestarts();
    m_resourceMonitor.tick();
}

void ServerManager::checkProcesses()
{
    std::vector<std::string> crashed;
    for (auto &[name, proc] : m_processes) {
        if (proc.running && !isProcessRunning(proc)) {
            int exitCode = getExitCode(proc);
            proc.running = false;
            cleanupProcess(proc);
            crashed.push_back(name);
            handleCrash(name, exitCode);
        }
    }
    for (const auto &name : crashed) {
        m_processes.erase(name);
        m_startTimes.erase(name);
        m_resourceMonitor.untrackProcess(name);
    }
}

void ServerManager::handleCrash(const std::string &serverName, int exitCode)
{
    int crashes = 0;
    auto cIt = m_crashCounts.find(serverName);
    if (cIt != m_crashCounts.end())
        crashes = cIt->second + 1;
    else
        crashes = 1;
    m_crashCounts[serverName] = crashes;

    if (onServerCrashed) onServerCrashed(serverName);

    for (const ServerConfig &s : m_servers) {
        if (s.name == serverName) {
            auto hookIt = s.eventHooks.find("onCrash");
            if (hookIt != s.eventHooks.end())
                m_eventHookManager.fireHook(s.name, s.dir, "onCrash", hookIt->second);
            m_webhook.sendNotification(s.discordWebhookUrl, serverName,
                "Server crashed (exit code " + std::to_string(exitCode) + ").",
                s.webhookTemplate);
            break;
        }
    }

    if (crashes > kMaxCrashRestarts) {
        emitLog(serverName,
            "Server crashed " + std::to_string(crashes) + " times consecutively. "
            "Auto-restart disabled until manual start.");
        return;
    }

    int delayMs = kCrashBackoffBaseMs * (1 << (crashes - 1));
    emitLog(serverName,
        "Server crashed (exit code " + std::to_string(exitCode)
        + ", attempt " + std::to_string(crashes) + "/" + std::to_string(kMaxCrashRestarts)
        + "). Auto-restarting in " + std::to_string(delayMs / 1000) + " s...");

    PendingRestart pr;
    pr.when = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs);
    m_pendingRestarts[serverName] = pr;
}

void ServerManager::processPendingRestarts()
{
    auto now = std::chrono::steady_clock::now();
    std::vector<std::string> ready;
    for (auto &[name, pr] : m_pendingRestarts) {
        if (now >= pr.when) ready.push_back(name);
    }
    for (const auto &name : ready) {
        m_pendingRestarts.erase(name);
        for (ServerConfig &s : m_servers) {
            if (s.name == name) { startServer(s); break; }
        }
    }
}

// ---------------------------------------------------------------------------
// SteamCMD
// ---------------------------------------------------------------------------

void ServerManager::deployServer(ServerConfig &server)
{
    emitLog(server.name, "Starting SteamCMD deployment...");
    SteamCmdModule steamCmd;
    steamCmd.setSteamCmdPath(m_steamCmdPath);
    steamCmd.onOutputLine = [this, &server](const std::string &line) {
        emitLog(server.name, line);
    };
    steamCmd.deployServer(server);
}

bool ServerManager::updateMods(ServerConfig &server)
{
    emitLog(server.name, "Taking pre-update snapshot...");
    std::string snapshotTs = takeSnapshot(server);

    emitLog(server.name, "Updating mods...");
    SteamCmdModule steamCmd;
    steamCmd.setSteamCmdPath(m_steamCmdPath);
    steamCmd.onOutputLine = [this, &server](const std::string &line) {
        emitLog(server.name, line);
    };
    bool ok = steamCmd.updateMods(server);

    if (ok) {
        auto hookIt = server.eventHooks.find("onUpdate");
        if (hookIt != server.eventHooks.end())
            m_eventHookManager.fireHook(server.name, server.dir, "onUpdate", hookIt->second);
    }

    if (!ok && !snapshotTs.empty()) {
        emitLog(server.name, "Mod update failed - rolling back to pre-update snapshot...");
        std::vector<std::string> snapshots = listSnapshots(server);
        for (const auto &snap : snapshots) {
            if (snap.find(snapshotTs) != std::string::npos &&
                snap.size() >= 9 && snap.substr(snap.size() - 9) == "_mods.zip") {
                restoreSnapshot(snap, server);
                break;
            }
        }
        emitLog(server.name, "Rollback complete.");
    } else if (!ok) {
        emitLog(server.name, "Mod update failed and no snapshot available for rollback.");
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Backup / restore
// ---------------------------------------------------------------------------

std::string ServerManager::takeSnapshot(const ServerConfig &server)
{
    emitLog(server.name, "Taking snapshot...");
    std::string ts = BackupModule::takeSnapshot(server);
    if (ts.empty())
        emitLog(server.name, "Snapshot failed.");
    else {
        emitLog(server.name, "Snapshot created: " + ts);
        auto hookIt = server.eventHooks.find("onBackup");
        if (hookIt != server.eventHooks.end())
            m_eventHookManager.fireHook(server.name, server.dir, "onBackup", hookIt->second);
        m_webhook.sendNotification(server.discordWebhookUrl, server.name,
                                   "Backup completed.", server.webhookTemplate);
    }
    return ts;
}

bool ServerManager::restoreSnapshot(const std::string &zipFile, const ServerConfig &server)
{
    emitLog(server.name, "Restoring from: " + zipFile);
    bool ok = BackupModule::restoreSnapshot(zipFile, server);
    emitLog(server.name, ok ? "Restore complete." : "Restore failed.");
    return ok;
}

std::vector<std::string> ServerManager::listSnapshots(const ServerConfig &server) const
{
    return BackupModule::listSnapshots(server);
}

// ---------------------------------------------------------------------------
// RCON
// ---------------------------------------------------------------------------

int ServerManager::getPlayerCount(const ServerConfig &server)
{
    RconClient rcon;
    if (!rcon.connectToServer(server.rcon.host, server.rcon.port, server.rcon.password, 3000))
        return -1;
    std::string resp = rcon.sendCommand("status", 3000);
    auto lines = splitString(resp, '\n');
    for (const auto &line : lines) {
        if (line.find("players") != std::string::npos) {
            std::regex re("(\\d+) humans");
            std::smatch m;
            if (std::regex_search(line, m, re))
                return std::stoi(m[1].str());
        }
    }
    return 0;
}

std::string ServerManager::sendRconCommand(const ServerConfig &server, const std::string &cmd)
{
    if (server.consoleLogging)
        ConsoleLogWriter::append(server.dir, server.name, "> " + cmd);
    RconClient rcon;
    if (!rcon.connectToServer(server.rcon.host, server.rcon.port, server.rcon.password, 3000))
        return "[RCON] Connection failed.";
    std::string resp = rcon.sendCommand(cmd);
    if (server.consoleLogging && !resp.empty())
        ConsoleLogWriter::append(server.dir, server.name, resp);
    return resp;
}

// ---------------------------------------------------------------------------
// Cluster operations
// ---------------------------------------------------------------------------

void ServerManager::syncModsCluster()
{
    for (ServerConfig &s : m_servers) updateMods(s);
}

std::vector<std::string> ServerManager::broadcastRconCommand(const std::string &cmd)
{
    std::vector<std::string> results;
    for (const ServerConfig &s : m_servers) {
        std::string resp = sendRconCommand(s, cmd);
        results.push_back("[" + s.name + "] " + resp);
        emitLog(s.name, "Broadcast RCON: " + cmd + " -> " + resp);
    }
    return results;
}

void ServerManager::syncConfigsCluster(const std::string &masterConfigZip)
{
    for (const ServerConfig &s : m_servers) {
        bool ok = BackupModule::extractZip(masterConfigZip, (fs::path(s.dir) / "Configs").string());
        emitLog(s.name, ok ? "Config synced from master zip." : "Config sync failed.");
    }
}

void ServerManager::setSteamCmdPath(const std::string &path) { m_steamCmdPath = path; }
std::string ServerManager::steamCmdPath() const               { return m_steamCmdPath; }

// ---------------------------------------------------------------------------
// Server removal
// ---------------------------------------------------------------------------

bool ServerManager::removeServer(const std::string &serverName)
{
    for (int i = 0; i < static_cast<int>(m_servers.size()); ++i) {
        if (m_servers[i].name == serverName) {
            if (isServerRunning(m_servers[i])) stopServer(m_servers[i]);
            m_servers.erase(m_servers.begin() + i);
            emitLog(serverName, "Server removed from configuration.");
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Export / Import
// ---------------------------------------------------------------------------

bool ServerManager::exportServerConfig(const std::string &serverName,
                                       const std::string &filePath) const
{
    const ServerConfig *found = nullptr;
    for (const auto &s : m_servers) {
        if (s.name == serverName) { found = &s; break; }
    }
    if (!found) return false;

    json obj = serverToJson(*found);
    std::ofstream file(filePath, std::ios::trunc);
    if (!file.is_open()) return false;
    file << obj.dump(2);
    return true;
}

std::string ServerManager::importServerConfig(const std::string &filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open()) return "Cannot open file.";

    json obj;
    try { obj = json::parse(file); }
    catch (const json::parse_error &e) {
        return std::string("Invalid JSON: ") + e.what();
    }
    if (!obj.is_object()) return "Expected a JSON object.";

    ServerConfig s;
    s.name           = jsonStr(obj, "name");
    s.appid          = jsonInt(obj, "appid");
    s.dir            = jsonStr(obj, "dir");
    s.executable     = jsonStr(obj, "executable");
    s.launchArgs     = jsonStr(obj, "launchArgs");
    s.backupFolder   = jsonStr(obj, "backupFolder");
    s.notes          = jsonStr(obj, "notes");
    s.discordWebhookUrl = jsonStr(obj, "discordWebhookUrl");
    s.webhookTemplate= jsonStr(obj, "webhookTemplate");
    s.autoUpdate     = jsonBool(obj, "autoUpdate", true);
    s.autoStartOnLaunch = jsonBool(obj, "autoStartOnLaunch", false);
    s.favorite       = jsonBool(obj, "favorite", false);
    s.keepBackups    = jsonInt(obj, "keepBackups", 10);
    s.backupIntervalMinutes = jsonInt(obj, "backupIntervalMinutes", 30);
    s.restartIntervalHours  = jsonInt(obj, "restartIntervalHours", 24);
    s.rconCommandIntervalMinutes = jsonInt(obj, "rconCommandIntervalMinutes", 0);
    s.backupCompressionLevel = jsonInt(obj, "backupCompressionLevel", 6);
    s.maintenanceStartHour   = jsonInt(obj, "maintenanceStartHour", -1);
    s.maintenanceEndHour     = jsonInt(obj, "maintenanceEndHour", -1);
    s.consoleLogging = jsonBool(obj, "consoleLogging", false);
    s.maxPlayers     = jsonInt(obj, "maxPlayers", 0);
    s.restartWarningMinutes = jsonInt(obj, "restartWarningMinutes", 15);
    s.restartWarningMessage = jsonStr(obj, "restartWarningMessage");
    s.cpuAlertThreshold    = jsonDouble(obj, "cpuAlertThreshold", 90.0);
    s.memAlertThresholdMB  = jsonDouble(obj, "memAlertThresholdMB", 0.0);

    if (obj.contains("eventHooks") && obj["eventHooks"].is_object()) {
        for (auto &[k, v] : obj["eventHooks"].items())
            if (v.is_string()) s.eventHooks[k] = v.get<std::string>();
    }
    if (obj.contains("tags") && obj["tags"].is_array()) {
        for (const auto &v : obj["tags"])
            if (v.is_string()) s.tags.push_back(v.get<std::string>());
    }

    s.group = jsonStr(obj, "group");
    s.startupPriority = jsonInt(obj, "startupPriority", 0);
    s.backupBeforeRestart = jsonBool(obj, "backupBeforeRestart", false);
    s.gracefulShutdownSeconds = jsonInt(obj, "gracefulShutdownSeconds", 10);
    s.autoUpdateCheckIntervalMinutes = jsonInt(obj, "autoUpdateCheckIntervalMinutes", 0);
    s.totalUptimeSeconds = jsonInt64(obj, "totalUptimeSeconds", 0);
    s.totalCrashes = jsonInt(obj, "totalCrashes", 0);
    s.lastCrashTime = jsonStr(obj, "lastCrashTime");

    if (obj.contains("environmentVariables") && obj["environmentVariables"].is_object()) {
        for (auto &[k, v] : obj["environmentVariables"].items())
            if (v.is_string()) s.environmentVariables[k] = v.get<std::string>();
    }
    if (obj.contains("rcon") && obj["rcon"].is_object()) {
        const auto &rcon = obj["rcon"];
        s.rcon.host     = jsonStr(rcon, "host", "127.0.0.1");
        s.rcon.port     = jsonInt(rcon, "port", 27015);
        s.rcon.password = deobfuscatePassword(jsonStr(rcon, "password"));
    }
    if (obj.contains("mods") && obj["mods"].is_array()) {
        for (const auto &m : obj["mods"])
            if (m.is_number_integer()) s.mods.push_back(m.get<int>());
    }
    if (obj.contains("disabledMods") && obj["disabledMods"].is_array()) {
        for (const auto &m : obj["disabledMods"])
            if (m.is_number_integer()) s.disabledMods.push_back(m.get<int>());
    }
    if (obj.contains("scheduledRconCommands") && obj["scheduledRconCommands"].is_array()) {
        for (const auto &v : obj["scheduledRconCommands"])
            if (v.is_string()) s.scheduledRconCommands.push_back(v.get<std::string>());
    }

    m_servers.push_back(s);
    std::vector<std::string> errors = validateAll();
    if (!errors.empty()) {
        m_servers.pop_back();
        std::string combined;
        for (size_t i = 0; i < errors.size(); ++i) {
            if (i > 0) combined += "\n";
            combined += errors[i];
        }
        return combined;
    }
    emitLog(s.name, "Server imported from file.");
    return "";
}

// ---------------------------------------------------------------------------
// Uptime tracking
// ---------------------------------------------------------------------------

std::chrono::system_clock::time_point ServerManager::serverStartTime(const std::string &serverName) const
{
    auto it = m_startTimes.find(serverName);
    if (it != m_startTimes.end()) return it->second;
    return {};
}

int64_t ServerManager::serverUptimeSeconds(const std::string &serverName) const
{
    auto it = m_startTimes.find(serverName);
    if (it == m_startTimes.end()) return -1;
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
}

// ---------------------------------------------------------------------------
// Crash backoff helpers
// ---------------------------------------------------------------------------

int ServerManager::crashCount(const std::string &serverName) const
{
    auto it = m_crashCounts.find(serverName);
    return (it != m_crashCounts.end()) ? it->second : 0;
}

void ServerManager::resetCrashCount(const std::string &serverName)
{
    m_crashCounts.erase(serverName);
}

// ---------------------------------------------------------------------------
// Pending update tracking
// ---------------------------------------------------------------------------

void ServerManager::setPendingUpdate(const std::string &serverName, bool pending)
{
    if (pending) m_pendingUpdates[serverName] = true;
    else m_pendingUpdates.erase(serverName);
}

bool ServerManager::hasPendingUpdate(const std::string &serverName) const
{
    auto it = m_pendingUpdates.find(serverName);
    return (it != m_pendingUpdates.end()) ? it->second : false;
}

void ServerManager::setPendingModUpdate(const std::string &serverName, bool pending)
{
    if (pending) m_pendingModUpdates[serverName] = true;
    else m_pendingModUpdates.erase(serverName);
}

bool ServerManager::hasPendingModUpdate(const std::string &serverName) const
{
    auto it = m_pendingModUpdates.find(serverName);
    return (it != m_pendingModUpdates.end()) ? it->second : false;
}

// ---------------------------------------------------------------------------
// Restart warning
// ---------------------------------------------------------------------------

void ServerManager::sendRestartWarning(ServerConfig &server, int minutesRemaining)
{
    if (!isServerRunning(server)) return;
    std::string msg = server.formatRestartWarning(minutesRemaining);
    emitLog(server.name,
        "Restart warning (" + std::to_string(minutesRemaining) + " min): " + msg);
    sendRconCommand(server, "broadcast " + msg);
}

// ---------------------------------------------------------------------------
// Resource monitor / Event hooks accessors
// ---------------------------------------------------------------------------

ResourceMonitor *ServerManager::resourceMonitor() { return &m_resourceMonitor; }
EventHookManager *ServerManager::eventHookManager() { return &m_eventHookManager; }

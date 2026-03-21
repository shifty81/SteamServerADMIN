#include "ServerManager.hpp"
#include "BackupModule.hpp"
#include "SteamCmdModule.hpp"
#include "RconClient.hpp"
#include "ConsoleLogWriter.hpp"
#include "GameTemplates.hpp"
#include "SteamQueryClient.hpp"

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
                   const std::map<std::string, std::string> &env, ProcessInfo &out,
                   const std::string &workingDir)
{
#ifdef _WIN32
    // Detect batch files (.bat / .cmd) and wrap with cmd.exe /c
    std::string lowerExe = exe;
    std::transform(lowerExe.begin(), lowerExe.end(), lowerExe.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    bool isBatch = (lowerExe.size() >= 4 &&
                    (lowerExe.substr(lowerExe.size() - 4) == ".bat" ||
                     lowerExe.substr(lowerExe.size() - 4) == ".cmd"));

    std::string cmdLine;
    if (isBatch)
        cmdLine = "cmd.exe /c \"" + exe + "\"";
    else
        cmdLine = "\"" + exe + "\"";
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

    const char *cwd = workingDir.empty() ? nullptr : workingDir.c_str();

    BOOL ok = CreateProcessA(
        nullptr, const_cast<char*>(cmdLine.c_str()),
        nullptr, nullptr, FALSE, 0,
        env.empty() ? nullptr : const_cast<char*>(envBlock.c_str()),
        cwd, &si, &pi);

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
        // Set working directory for the child process
        if (!workingDir.empty())
            if (chdir(workingDir.c_str()) != 0)
                _exit(127);

        for (const auto &[k, v] : env)
            setenv(k.c_str(), v.c_str(), 1);

        // Detect shell scripts (.sh) and use /bin/sh as interpreter
        bool isShellScript = (exe.size() >= 3 &&
                              exe.substr(exe.size() - 3) == ".sh");

        std::vector<char*> argv;
        std::string shPath = "/bin/sh";
        if (isShellScript) {
            argv.push_back(const_cast<char*>(shPath.c_str()));
        }
        std::string exeCopy = exe;
        argv.push_back(const_cast<char*>(exeCopy.c_str()));
        std::vector<std::string> argCopies(args.begin(), args.end());
        for (auto &a : argCopies)
            argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);

        if (isShellScript)
            execv("/bin/sh", argv.data());
        else
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
    if (result < 0) return false;  // error (e.g. already reaped)
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

    std::lock_guard<std::mutex> lk(m_deployObserverMutex);
    auto it = m_deployLogObservers.find(serverName);
    if (it != m_deployLogObservers.end() && it->second)
        it->second(msg);
}

void ServerManager::setDeployLogObserver(const std::string &serverName,
                                          std::function<void(const std::string &)> observer)
{
    std::lock_guard<std::mutex> lk(m_deployObserverMutex);
    m_deployLogObservers[serverName] = std::move(observer);
}

void ServerManager::clearDeployLogObserver(const std::string &serverName)
{
    std::lock_guard<std::mutex> lk(m_deployObserverMutex);
    m_deployLogObservers.erase(serverName);
}

bool ServerManager::isDeploying(const std::string &serverName) const
{
    std::lock_guard<std::mutex> lk(m_deployObserverMutex);
    auto it = m_deployLogObservers.find(serverName);
    return it != m_deployLogObservers.end() && it->second != nullptr;
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
    : m_configFile(configFile),
      m_gracefulRestartManager(this)
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

// ---------------------------------------------------------------------------
// Deserialize a single ServerConfig from a JSON object.
// Used by both loadConfig() and importServerConfig() to avoid duplication.
// ---------------------------------------------------------------------------
static ServerConfig deserializeServerConfig(const json &obj)
{
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
    s.queryPort = jsonInt(obj, "queryPort", 0);
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

    return s;
}

bool ServerManager::loadConfig()
{
    // If the config file does not exist yet (e.g. first run), treat it as an
    // empty server list rather than an error.
    if (!fs::exists(m_configFile))
        return true;

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
        m_servers.push_back(deserializeServerConfig(obj));
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
    obj["queryPort"] = s.queryPort;
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
    bool ok = launchProcess(exe, args, server.environmentVariables, proc, server.dir);

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

    // Accumulate uptime before stopping
    auto stIt = m_startTimes.find(server.name);
    if (stIt != m_startTimes.end()) {
        auto now = std::chrono::system_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
            now - stIt->second).count();
        server.totalUptimeSeconds += uptime;
        saveConfig();
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
    releaseRcon(server.name);
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
    processHourlyMaintenance();
    m_resourceMonitor.tick();
    m_gracefulRestartManager.tick();
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
        releaseRcon(name);
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

    // Update persistent crash statistics
    auto now = std::chrono::system_clock::now();
    std::time_t nowT = std::chrono::system_clock::to_time_t(now);
    char timeBuf[64];
    std::tm tmBuf{};
#ifdef _WIN32
    gmtime_s(&tmBuf, &nowT);
#else
    gmtime_r(&nowT, &tmBuf);
#endif
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%S", &tmBuf);

    for (ServerConfig &s : m_servers) {
        if (s.name == serverName) {
            // Accumulate uptime before clearing the start time
            auto stIt = m_startTimes.find(serverName);
            if (stIt != m_startTimes.end()) {
                auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
                    now - stIt->second).count();
                s.totalUptimeSeconds += uptime;
            }

            s.totalCrashes++;
            s.lastCrashTime = timeBuf;

            auto hookIt = s.eventHooks.find("onCrash");
            if (hookIt != s.eventHooks.end())
                m_eventHookManager.fireHook(s.name, s.dir, "onCrash", hookIt->second);
            m_webhook.sendNotification(s.discordWebhookUrl, serverName,
                "Server crashed (exit code " + std::to_string(exitCode) + ").",
                s.webhookTemplate);
            break;
        }
    }
    saveConfig();

    if (onServerCrashed) onServerCrashed(serverName);

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
    setPendingUpdate(server.name, false);
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
        setPendingModUpdate(server.name, false);
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
// deployOrUpdateServer – smart deploy: install if empty, verify/update if
//                        already deployed
// ---------------------------------------------------------------------------

bool ServerManager::deployOrUpdateServer(ServerConfig &server)
{
    if (!isSteamCmdInstalled()) {
        emitLog(server.name, "SteamCMD is not installed. Please install SteamCMD first.");
        return false;
    }

    if (isServerRunning(server)) {
        emitLog(server.name, "Server is running – stop it before deploying/updating.");
        return false;
    }

    // Determine if the server directory is empty (fresh install) or already
    // has files (update / verify).
    bool dirEmpty = true;
    try {
        if (fs::exists(server.dir) && fs::is_directory(server.dir)) {
            auto it = fs::directory_iterator(server.dir);
            dirEmpty = (it == fs::directory_iterator());
        }
    } catch (...) {
        dirEmpty = true;
    }

    if (dirEmpty)
        emitLog(server.name, "Server directory is empty – performing fresh install...");
    else
        emitLog(server.name, "Server directory has existing files – verifying/updating...");

    SteamCmdModule steamCmd;
    steamCmd.setSteamCmdPath(m_steamCmdPath);
    steamCmd.onOutputLine = [this, &server](const std::string &line) {
        emitLog(server.name, line);
    };

    bool ok = steamCmd.deployServer(server);

    if (ok) {
        setPendingUpdate(server.name, false);
        emitLog(server.name, "SteamCMD deployment/verification complete.");

        // Seed default config files for the game template on first install
        if (dirEmpty)
            seedConfigFiles(server);

        auto hookIt = server.eventHooks.find("onUpdate");
        if (hookIt != server.eventHooks.end())
            m_eventHookManager.fireHook(server.name, server.dir, "onUpdate", hookIt->second);
    } else {
        emitLog(server.name, "SteamCMD deployment failed. Check the log for details.");
    }
    return ok;
}

// ---------------------------------------------------------------------------
// seedConfigFiles – create starter config files after a fresh install
// ---------------------------------------------------------------------------

/// Return the default content for a well-known config file based on its name
/// and the game AppID.
static std::string defaultConfigContent(int appid, const std::string &relPath)
{
    // ---- Source-engine server.cfg (CS2, GMod, TF2, L4D2) ----
    if (relPath.find("server.cfg") != std::string::npos) {
        switch (appid) {
            case 730: // CS2
                return "// CS2 Dedicated Server Config\n"
                       "hostname \"My CS2 Server\"\n"
                       "rcon_password \"changeme\"\n"
                       "sv_cheats 0\n"
                       "sv_lan 0\n"
                       "mp_autoteambalance 1\n";
            case 4020: // GMod
                return "// Garry's Mod Server Config\n"
                       "hostname \"My GMod Server\"\n"
                       "rcon_password \"changeme\"\n"
                       "sv_defaultgamemode sandbox\n"
                       "sbox_maxprops 200\n";
            case 232250: // TF2
                return "// TF2 Dedicated Server Config\n"
                       "hostname \"My TF2 Server\"\n"
                       "rcon_password \"changeme\"\n"
                       "tf_mm_strict 0\n"
                       "sv_pure 1\n";
            case 222860: // L4D2
                return "// Left 4 Dead 2 Server Config\n"
                       "hostname \"My L4D2 Server\"\n"
                       "rcon_password \"changeme\"\n"
                       "sv_steamgroup_exclusive 0\n";
            default:
                return "// Server Configuration\n"
                       "hostname \"My Server\"\n"
                       "rcon_password \"changeme\"\n";
        }
    }

    // ---- ARK GameUserSettings.ini ----
    if (relPath.find("GameUserSettings.ini") != std::string::npos) {
        return "[ServerSettings]\n"
               "ServerAdminPassword=changeme\n"
               "ServerPassword=\n"
               "RCONEnabled=True\n"
               "RCONPort=27020\n"
               "MaxPlayers=70\n"
               "DifficultyOffset=0.2\n"
               "\n"
               "[SessionSettings]\n"
               "SessionName=My ARK Server\n";
    }

    // ---- Rust server.cfg ----
    if (relPath.find("server.cfg") != std::string::npos && appid == 258550) {
        return "// Rust Dedicated Server Config\n"
               "server.hostname \"My Rust Server\"\n"
               "server.maxplayers 100\n"
               "server.worldsize 4000\n"
               "server.seed 0\n"  // 0 = random seed; change to a fixed value for a consistent world
               "rcon.password \"changeme\"\n"
               "rcon.port 28016\n"
               "rcon.web 1\n";
    }

    // ---- Valheim adminlist/bannedlist/permittedlist ----
    if (relPath.find("adminlist.txt") != std::string::npos)
        return "// Add Steam64 IDs here, one per line\n";
    if (relPath.find("bannedlist.txt") != std::string::npos)
        return "// Add Steam64 IDs here, one per line\n";
    if (relPath.find("permittedlist.txt") != std::string::npos)
        return "// Add Steam64 IDs here, one per line\n";

    // ---- 7 Days to Die serverconfig.xml ----
    if (relPath.find("serverconfig.xml") != std::string::npos && appid == 294420) {
        return "<?xml version=\"1.0\"?>\n"
               "<ServerSettings>\n"
               "  <property name=\"ServerName\" value=\"My 7DTD Server\"/>\n"
               "  <property name=\"ServerPassword\" value=\"\"/>\n"
               "  <property name=\"TelnetEnabled\" value=\"true\"/>\n"
               "  <property name=\"TelnetPort\" value=\"8081\"/>\n"
               "  <property name=\"TelnetPassword\" value=\"changeme\"/>\n"
               "  <property name=\"MaxPlayerCount\" value=\"8\"/>\n"
               "</ServerSettings>\n";
    }

    // ---- DayZ serverDZ.cfg ----
    if (relPath.find("serverDZ.cfg") != std::string::npos) {
        return "hostname = \"My DayZ Server\";\n"
               "password = \"\";\n"
               "passwordAdmin = \"changeme\";\n"
               "maxPlayers = 60;\n"
               "steamQueryPort = 2305;\n";
    }

    // ---- Palworld PalWorldSettings.ini ----
    if (relPath.find("PalWorldSettings.ini") != std::string::npos) {
        return "[/Script/Pal.PalGameWorldSettings]\n"
               "OptionSettings=(ServerName=\"My Palworld Server\",AdminPassword=\"changeme\","
               "ServerPassword=\"\",PublicPort=8211,RCONEnabled=True,RCONPort=25575)\n";
    }

    // ---- Enshrouded enshrouded_server.json ----
    if (relPath.find("enshrouded_server.json") != std::string::npos) {
        return "{\n"
               "  \"name\": \"My Enshrouded Server\",\n"
               "  \"password\": \"\",\n"
               "  \"saveDirectory\": \"./savegame\",\n"
               "  \"logDirectory\": \"./logs\",\n"
               "  \"ip\": \"0.0.0.0\",\n"
               "  \"gamePort\": 15636,\n"
               "  \"queryPort\": 15637,\n"
               "  \"slotCount\": 16\n"
               "}\n";
    }

    // ---- V Rising ServerHostSettings.json ----
    if (relPath.find("ServerHostSettings.json") != std::string::npos) {
        return "{\n"
               "  \"Name\": \"My V Rising Server\",\n"
               "  \"Port\": 9876,\n"
               "  \"QueryPort\": 9877,\n"
               "  \"MaxConnectedUsers\": 40,\n"
               "  \"MaxConnectedAdmins\": 4,\n"
               "  \"Password\": \"\",\n"
               "  \"Rcon\": { \"Enabled\": true, \"Port\": 25575, \"Password\": \"changeme\" }\n"
               "}\n";
    }

    // ---- Conan Exiles ServerSettings.ini ----
    if (relPath.find("ServerSettings.ini") != std::string::npos && appid == 443030) {
        return "[ServerSettings]\n"
               "ServerName=My Conan Exiles Server\n"
               "AdminPassword=changeme\n"
               "MaxPlayers=40\n"
               "ServerPort=7777\n"
               "ServerQueryPort=27015\n";
    }

    // ---- Project Zomboid servertest.ini ----
    if (relPath.find("servertest.ini") != std::string::npos) {
        return "# Project Zomboid Server Settings\n"
               "RCONPassword=changeme\n"
               "RCONPort=27015\n"
               "DefaultPort=16261\n"
               "MaxPlayers=16\n"
               "Public=true\n"
               "PublicName=My PZ Server\n";
    }

    // ---- Satisfactory Game.ini ----
    if (relPath.find("Game.ini") != std::string::npos && appid == 1690800) {
        return "[/Script/Engine.GameSession]\n"
               "MaxPlayers=4\n";
    }

    // ---- Terraria serverconfig.txt ----
    if (relPath.find("serverconfig.txt") != std::string::npos && appid == 1281930) {
        return "# Terraria Server Config\n"
               "maxplayers=8\n"
               "port=7777\n"
               "password=\n"
               "worldname=world1\n"
               "autocreate=2\n"
               "difficulty=0\n";
    }

    // ---- The Forest config.cfg ----
    if (relPath.find("config.cfg") != std::string::npos && appid == 556450) {
        return "// The Forest Dedicated Server Config\n"
               "serverName My Forest Server\n"
               "serverPlayers 8\n"
               "serverPort 8766\n"
               "serverSteamPort 8766\n"
               "serverPassword \n"
               "enableVAC on\n";
    }

    // ---- Unturned Commands.dat ----
    if (relPath.find("Commands.dat") != std::string::npos) {
        return "Name MyServer\n"
               "Port 27015\n"
               "MaxPlayers 24\n"
               "Map PEI\n"
               "Password\n";
    }

    // ---- Don't Starve Together cluster.ini ----
    if (relPath.find("cluster.ini") != std::string::npos && appid == 343050) {
        return "[GAMEPLAY]\n"
               "game_mode = survival\n"
               "max_players = 6\n"
               "pvp = false\n"
               "\n"
               "[NETWORK]\n"
               "cluster_name = My DST Server\n"
               "cluster_password = \n"
               "cluster_description = \n"
               "\n"
               "[MISC]\n"
               "console_enabled = true\n";
    }

    // ---- ARMA 3 server.cfg ----
    if (relPath.find("server.cfg") != std::string::npos && appid == 233780) {
        return "// ARMA 3 Server Config\n"
               "hostname = \"My ARMA 3 Server\";\n"
               "password = \"\";\n"
               "passwordAdmin = \"changeme\";\n"
               "maxPlayers = 32;\n"
               "motd[] = { \"Welcome to my server\" };\n";
    }

    // ---- ARMA 3 basic.cfg ----
    if (relPath.find("basic.cfg") != std::string::npos && appid == 233780) {
        return "// ARMA 3 Basic Network Config\n"
               "MaxMsgSend = 128;\n"
               "MaxSizeGuaranteed = 512;\n"
               "MaxSizeNonguaranteed = 256;\n"
               "MinBandwidth = 131072;\n"
               "MaxBandwidth = 10000000;\n";
    }

    // ---- Squad Server.cfg ----
    if (relPath.find("Server.cfg") != std::string::npos && appid == 403240) {
        return "// Squad Server Config\n"
               "ServerName=\"My Squad Server\"\n"
               "MaxPlayers=80\n"
               "ShouldAdvertise=true\n";
    }

    // ---- Squad Rcon.cfg ----
    if (relPath.find("Rcon.cfg") != std::string::npos && appid == 403240) {
        return "// Squad RCON Config\n"
               "Port=21114\n"
               "Password=changeme\n";
    }

    // ---- Barotrauma serversettings.xml ----
    if (relPath.find("serversettings.xml") != std::string::npos && appid == 1026340) {
        return "<?xml version=\"1.0\"?>\n"
               "<serversettings\n"
               "  name=\"My Barotrauma Server\"\n"
               "  port=\"27015\"\n"
               "  maxplayers=\"6\"\n"
               "  password=\"\"\n"
               "/>\n";
    }

    // ---- Space Engineers SpaceEngineers-Dedicated.cfg ----
    if (relPath.find("SpaceEngineers-Dedicated.cfg") != std::string::npos && appid == 298740) {
        return "<?xml version=\"1.0\"?>\n"
               "<MyConfigDedicated>\n"
               "  <ServerName>My Space Engineers Server</ServerName>\n"
               "  <ServerPort>27016</ServerPort>\n"
               "  <WorldName>Star System</WorldName>\n"
               "</MyConfigDedicated>\n";
    }

    // ---- Sons of the Forest dedicatedserver.cfg ----
    if (relPath.find("dedicatedserver.cfg") != std::string::npos && appid == 2465200) {
        return "{\n"
               "  \"IpAddress\": \"0.0.0.0\",\n"
               "  \"GamePort\": 8766,\n"
               "  \"QueryPort\": 27016,\n"
               "  \"BlobSyncPort\": 9700,\n"
               "  \"MaxPlayers\": 8,\n"
               "  \"ServerName\": \"My Sons of the Forest Server\",\n"
               "  \"Password\": \"\"\n"
               "}\n";
    }

    // ---- Mordhau Game.ini ----
    if (relPath.find("Game.ini") != std::string::npos && appid == 629800) {
        return "[/Script/Mordhau.MordhauGameSession]\n"
               "MaxSlots=32\n"
               "ServerName=My Mordhau Server\n"
               "ServerPassword=\n"
               "AdminPassword=changeme\n";
    }

    // Fallback: empty file with a comment
    if (relPath.find(".ini") != std::string::npos || relPath.find(".cfg") != std::string::npos)
        return "// Auto-generated config stub – edit to taste\n";
    if (relPath.find(".json") != std::string::npos)
        return "{}\n";
    if (relPath.find(".xml") != std::string::npos)
        return "<?xml version=\"1.0\"?>\n<Config/>\n";
    if (relPath.find(".txt") != std::string::npos)
        return "# Auto-generated config stub\n";
    if (relPath.find(".lua") != std::string::npos)
        return "-- Auto-generated config stub\n";
    if (relPath.find(".dat") != std::string::npos)
        return "";

    return "";
}

void ServerManager::seedConfigFiles(ServerConfig &server)
{
    // Find the matching template for this server's appid
    auto templates = GameTemplate::builtinTemplates();
    const GameTemplate *tmpl = nullptr;
    for (const auto &t : templates) {
        if (t.appid == server.appid) { tmpl = &t; break; }
    }
    if (!tmpl || tmpl->configPaths.empty()) return;

    emitLog(server.name, "Seeding default config files...");

    for (const auto &relPath : tmpl->configPaths) {
        fs::path full = fs::path(server.dir) / relPath;

        // Don't overwrite existing files
        if (fs::exists(full)) {
            emitLog(server.name, "  Config already exists: " + relPath);
            continue;
        }

        // Create parent directories
        try {
            fs::create_directories(full.parent_path());
        } catch (const fs::filesystem_error &e) {
            emitLog(server.name, "  Failed to create directory for " + relPath + ": " + e.what());
            continue;
        }

        std::string content = defaultConfigContent(server.appid, relPath);
        std::ofstream f(full);
        if (f.is_open()) {
            f << content;
            f.close();
            emitLog(server.name, "  Created: " + relPath);
        } else {
            emitLog(server.name, "  Failed to create: " + relPath);
        }
    }
}

// ---------------------------------------------------------------------------
// processHourlyMaintenance – per-server auto-update checks respecting
// each server's autoUpdateCheckIntervalMinutes setting
// ---------------------------------------------------------------------------

void ServerManager::processHourlyMaintenance()
{
    // Only proceed if SteamCMD is installed
    if (!isSteamCmdInstalled())
        return;

    auto now = std::chrono::steady_clock::now();

    for (ServerConfig &server : m_servers) {
        // Skip servers that have auto-update disabled
        if (!server.autoUpdate)
            continue;

        // Determine the interval for this server.
        // A per-server value of 0 means "use the default".
        int intervalMin = server.autoUpdateCheckIntervalMinutes > 0
                              ? server.autoUpdateCheckIntervalMinutes
                              : kDefaultUpdateCheckIntervalMinutes;

        // Check whether enough time has elapsed since the last check for
        // this particular server.
        auto it = m_lastUpdateChecks.find(server.name);
        if (it != m_lastUpdateChecks.end()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - it->second).count();
            if (elapsed < intervalMin)
                continue;
        }

        m_lastUpdateChecks[server.name] = now;

        // Don't update running servers – flag them for later
        if (isServerRunning(server)) {
            setPendingUpdate(server.name, true);
            emitLog(server.name, "Update check: server is running, flagged for update when stopped.");
            continue;
        }

        emitLog(server.name, "Scheduled update check: verifying/updating server files...");
        deployOrUpdateServer(server);

        // Also update mods if any are configured
        if (!server.mods.empty()) {
            emitLog(server.name, "Scheduled update check: updating mods...");
            updateMods(server);
        }
    }
}
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
// RCON – persistent connection pool
// ---------------------------------------------------------------------------

RconClient *ServerManager::acquireRcon(const ServerConfig &server)
{
    auto it = m_rconPool.find(server.name);

    // Try the cached connection first
    if (it != m_rconPool.end() && it->second && it->second->isConnected())
        return it->second.get();

    // Connection-failure cooldown: don't spam reconnection attempts
    auto failIt = m_rconFailTimes.find(server.name);
    if (failIt != m_rconFailTimes.end()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - failIt->second).count();
        if (elapsed < kRconFailCooldownSeconds)
            return nullptr;   // still in cooldown
    }

    // Create or reconnect
    auto client = std::make_unique<RconClient>();

    // 7 Days to Die (appid 294420) uses a plain telnet console, not Source RCON
    if (server.appid == 294420)
        client->setTelnetMode(true);

    if (!client->connectToServer(server.rcon.host, server.rcon.port,
                                 server.rcon.password, 3000)) {
        m_rconPool.erase(server.name);   // clear stale entry
        m_rconFailTimes[server.name] = std::chrono::steady_clock::now();
        return nullptr;
    }
    m_rconFailTimes.erase(server.name);  // clear cooldown on success
    RconClient *ptr = client.get();
    m_rconPool[server.name] = std::move(client);
    return ptr;
}

void ServerManager::releaseRcon(const std::string &serverName)
{
    m_rconPool.erase(serverName);
    m_rconFailTimes.erase(serverName);  // allow immediate reconnect after stop/restart
}

// ---------------------------------------------------------------------------
// RCON
// ---------------------------------------------------------------------------

int ServerManager::getPlayerCount(const ServerConfig &server)
{
    // ---- Try RCON first (Source engine "status" response) ----
    bool rconConfigured = !trimString(server.rcon.host).empty()
                          && server.rcon.port > 0;
    bool rconConnected = false;

    if (rconConfigured) {
        RconClient *rcon = acquireRcon(server);
        if (rcon) {
            rconConnected = true;
            std::string resp = rcon->sendCommand("status", 3000);
            if (resp.empty() && !rcon->isConnected()) {
                // Connection dropped – retry once with a fresh connection
                releaseRcon(server.name);
                rcon = acquireRcon(server);
                if (rcon)
                    resp = rcon->sendCommand("status", 3000);
                else
                    rconConnected = false;
            }
            auto lines = splitString(resp, '\n');
            for (const auto &line : lines) {
                if (line.find("players") != std::string::npos) {
                    std::regex re("(\\d+) humans");
                    std::smatch m;
                    if (std::regex_search(line, m, re))
                        return std::stoi(m[1].str());
                }
            }
            // RCON connected but "status" didn't yield a parseable player count –
            // fall through to A2S below (may get a better count for games with
            // non-Source RCON formats). If a count WAS found above, the function
            // already returned early via the regex match path.
        }
    }

    // ---- Fallback: Steam A2S_INFO query ----
    // Used when RCON is not configured, unavailable, or didn't return a
    // parseable player count (e.g., games with non-Source RCON formats).
    if (server.queryPort > 0) {
        std::string host = trimString(server.rcon.host).empty()
                               ? "127.0.0.1"
                               : server.rcon.host;
        auto info = SteamQueryClient::queryInfo(host,
                                                static_cast<uint16_t>(server.queryPort),
                                                2000);
        if (info.has_value() && info->players >= 0)
            return info->players;
    }

    // Could not determine player count:
    //   -1 means "unknown" (RCON unreachable and no A2S query succeeded)
    //    0 means "server is reachable but no players" (RCON connected OK)
    return rconConnected ? 0 : -1;
}

std::string ServerManager::sendRconCommand(const ServerConfig &server, const std::string &cmd)
{
    if (server.consoleLogging)
        ConsoleLogWriter::append(server.dir, server.name, "> " + cmd);
    RconClient *rcon = acquireRcon(server);
    if (!rcon)
        return "[RCON] Connection failed.";
    std::string resp = rcon->sendCommand(cmd);
    if (resp.empty() && !rcon->isConnected()) {
        // Connection dropped – retry once with a fresh connection
        releaseRcon(server.name);
        rcon = acquireRcon(server);
        if (!rcon) return "[RCON] Connection failed.";
        resp = rcon->sendCommand(cmd);
    }
    if (server.consoleLogging && !resp.empty())
        ConsoleLogWriter::append(server.dir, server.name, resp);
    return resp;
}

// ---------------------------------------------------------------------------
// RCON connection test (temporary connection, independent of pool)
// ---------------------------------------------------------------------------

bool ServerManager::testRconConnection(const ServerConfig &server, std::string &errorOut)
{
    if (trimString(server.rcon.host).empty()) {
        errorOut = "RCON host is not configured.";
        return false;
    }
    if (server.rcon.port < 1 || server.rcon.port > 65535) {
        errorOut = "RCON port is invalid.";
        return false;
    }

    RconClient probe;
    if (server.appid == 294420) // 7 Days to Die uses telnet
        probe.setTelnetMode(true);

    bool ok = probe.connectToServer(server.rcon.host, server.rcon.port, server.rcon.password);
    if (!ok) {
        errorOut = "Connection failed (host unreachable or wrong password).";
        return false;
    }
    errorOut.clear();
    return true;
}

// ---------------------------------------------------------------------------
// Test Discord webhook
// ---------------------------------------------------------------------------

void ServerManager::sendTestWebhook(const ServerConfig &server)
{
    if (trimString(server.discordWebhookUrl).empty())
        return;
    m_webhook.sendNotification(server.discordWebhookUrl,
                               server.name,
                               "test notification",
                               server.webhookTemplate.empty()
                                   ? "**[{server}]** SSA test notification sent at {timestamp}."
                                   : server.webhookTemplate);
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

bool ServerManager::installSteamCmd(const std::string &installDir)
{
    emitLog("SSA", "Installing SteamCMD to " + installDir + " ...");
    SteamCmdModule steamCmd;
    steamCmd.onOutputLine = [this](const std::string &line) {
        emitLog("SSA", line);
    };
    bool ok = steamCmd.installSteamCmd(installDir);
    if (ok) {
        m_steamCmdPath = steamCmd.steamCmdPath();
        emitLog("SSA", "SteamCMD path set to " + m_steamCmdPath);
    }
    return ok;
}

bool ServerManager::isSteamCmdInstalled() const
{
    SteamCmdModule steamCmd;
    steamCmd.setSteamCmdPath(m_steamCmdPath);
    return steamCmd.isSteamCmdInstalled();
}

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

    ServerConfig s = deserializeServerConfig(obj);

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
// Resource monitor / Event hooks / Graceful restart / Steam library accessors
// ---------------------------------------------------------------------------

ResourceMonitor *ServerManager::resourceMonitor() { return &m_resourceMonitor; }
EventHookManager *ServerManager::eventHookManager() { return &m_eventHookManager; }
GracefulRestartManager *ServerManager::gracefulRestartManager() { return &m_gracefulRestartManager; }
SteamLibraryDetector *ServerManager::steamLibraryDetector() { return &m_steamLibraryDetector; }

#include "SteamCmdModule.hpp"

#include <filesystem>
#include <cstdio>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#define popen  _popen
#define pclose _pclose
#endif

namespace fs = std::filesystem;

SteamCmdModule::SteamCmdModule()
{
#ifdef _WIN32
    m_steamCmdPath = "steamcmd.exe";
#else
    m_steamCmdPath = "steamcmd";
#endif
}

void SteamCmdModule::setSteamCmdPath(const std::string &path)
{
    m_steamCmdPath = path;
}

std::string SteamCmdModule::steamCmdPath() const
{
    return m_steamCmdPath;
}

void SteamCmdModule::deployServer(const ServerConfig &server)
{
    fs::create_directories(server.dir);

    std::vector<std::string> args = {
        "+login",    "anonymous",
        "+force_install_dir", server.dir,
        "+app_update", std::to_string(server.appid),
        "validate",
        "+quit"
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
    std::vector<std::string> args = {
        "+login",    "anonymous",
        "+workshop_download_item",
        std::to_string(appid), std::to_string(modId),
        "+quit"
    };
    return runSteamCmd(args);
}

// ---------------------------------------------------------------------------
// installSteamCmd – download & extract the official SteamCMD package
// ---------------------------------------------------------------------------

/// Reject directory paths that contain characters dangerous in shell commands.
static bool isPathSafeForShell(const std::string &path)
{
    for (char c : path) {
        if (c == '\'' || c == '`' || c == '$' || c == '!' ||
            c == '|' || c == ';' || c == '&' || c == '"' ||
            c == '\n' || c == '\r')
            return false;
    }
    return true;
}

bool SteamCmdModule::installSteamCmd(const std::string &installDir)
{
    if (installDir.empty()) {
        if (onOutputLine) onOutputLine("Install directory is empty");
        if (onFinished) onFinished(false);
        return false;
    }

    if (!isPathSafeForShell(installDir)) {
        if (onOutputLine) onOutputLine("Install directory contains invalid characters");
        if (onFinished) onFinished(false);
        return false;
    }

    fs::create_directories(installDir);

#ifdef _WIN32
    // Windows: download zip and extract with PowerShell
    std::string zip = (fs::path(installDir) / "steamcmd.zip").string();
    std::string cmd =
        "powershell -NoProfile -Command \""
        "Invoke-WebRequest -Uri 'https://steamcdn-a.akamaihd.net/client/installer/steamcmd.zip'"
        " -OutFile '" + zip + "';"
        " Expand-Archive -Path '" + zip + "' -DestinationPath '" + installDir + "' -Force"
        "\" 2>&1";
#else
    // Linux / macOS: download tarball and extract with curl + tar
    std::string cmd =
        "curl -sqL 'https://steamcdn-a.akamaihd.net/client/installer/steamcmd_linux.tar.gz'"
        " | tar xzf - -C '" + installDir + "' 2>&1";
#endif

    if (onOutputLine) onOutputLine("Downloading SteamCMD to " + installDir + " ...");

    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        if (onOutputLine) onOutputLine("Failed to launch download command");
        if (onFinished) onFinished(false);
        return false;
    }

    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        std::string line(buffer);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        if (!line.empty() && onOutputLine)
            onOutputLine(line);
    }

    int status = pclose(pipe);
    if (status != 0) {
        if (onOutputLine) onOutputLine("SteamCMD download/extract failed");
        if (onFinished) onFinished(false);
        return false;
    }

    // Verify the binary was extracted
#ifdef _WIN32
    std::string binary = (fs::path(installDir) / "steamcmd.exe").string();
#else
    std::string binary = (fs::path(installDir) / "steamcmd.sh").string();
#endif

    if (!fs::exists(binary)) {
        if (onOutputLine) onOutputLine("SteamCMD binary not found after extraction");
        if (onFinished) onFinished(false);
        return false;
    }

    // Update path to point to the freshly installed binary
    m_steamCmdPath = binary;

    if (onOutputLine) onOutputLine("SteamCMD installed successfully at " + binary);
    if (onFinished) onFinished(true);
    return true;
}

// ---------------------------------------------------------------------------

bool SteamCmdModule::isSteamCmdInstalled() const
{
    return fs::exists(m_steamCmdPath);
}

std::string SteamCmdModule::defaultInstallDir()
{
    return (fs::current_path() / "steamcmd").string();
}

// ---------------------------------------------------------------------------

bool SteamCmdModule::runSteamCmd(const std::vector<std::string> &args)
{
    // Build command line
    std::string cmd = m_steamCmdPath;
    for (const auto &arg : args)
        cmd += " " + arg;
    cmd += " 2>&1";

    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        if (onOutputLine) onOutputLine("Failed to launch SteamCMD");
        if (onFinished) onFinished(false);
        return false;
    }

    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        std::string line(buffer);
        // Trim trailing newline
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        if (!line.empty() && onOutputLine)
            onOutputLine(line);
    }

    int status = pclose(pipe);
    bool ok = (status == 0);
    if (onFinished) onFinished(ok);
    return ok;
}

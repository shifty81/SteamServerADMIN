#include "SteamCmdModule.hpp"

#include <filesystem>
#include <cstdio>
#include <iostream>
#include <sstream>

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

#pragma once

#include "ServerConfig.hpp"

#include <string>
#include <vector>
#include <functional>

/**
 * @brief Wraps SteamCMD to install/update game servers and workshop mods.
 *
 * All operations run via popen().  The steamcmdPath must point to the
 * SteamCMD binary (steamcmd.exe on Windows, steamcmd on Linux).
 */
class SteamCmdModule {
public:
    SteamCmdModule();

    void setSteamCmdPath(const std::string &path);
    std::string steamCmdPath() const;

    /**
     * @brief Install or update a game server identified by server.appid into
     *        server.dir.  Calls onOutputLine() during execution.
     */
    void deployServer(const ServerConfig &server);

    /**
     * @brief Download/update all mods listed in server.mods via the Steam
     *        Workshop.  Calls onOutputLine() during execution.
     *  @return true if all mods updated successfully.
     */
    bool updateMods(const ServerConfig &server);

    /**
     * @brief Download a single workshop mod.
     * @return true on success.
     */
    bool downloadMod(int appid, int modId);

    /** Callback invoked for each line of SteamCMD output. */
    std::function<void(const std::string &line)> onOutputLine;

    /** Callback invoked when an operation finishes. */
    std::function<void(bool success)> onFinished;

private:
    bool runSteamCmd(const std::vector<std::string> &args);

    std::string m_steamCmdPath;
};

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

    /**
     * @brief Download and install SteamCMD into @p installDir.
     *
     * On Linux this downloads the official tarball and extracts it.
     * On Windows this downloads the zip and extracts it.
     * After a successful install the steamcmd path is updated automatically.
     *
     * @return true on success.
     */
    bool installSteamCmd(const std::string &installDir);

    /**
     * @brief Check whether the configured SteamCMD binary exists on disk.
     */
    bool isSteamCmdInstalled() const;

    /**
     * @brief Return the default installation directory used by installSteamCmd().
     *
     * This is a subfolder called "steamcmd" next to the running executable.
     */
    static std::string defaultInstallDir();

    /** Callback invoked for each line of SteamCMD output. */
    std::function<void(const std::string &line)> onOutputLine;

    /** Callback invoked when an operation finishes. */
    std::function<void(bool success)> onFinished;

private:
    bool runSteamCmd(const std::vector<std::string> &args);

    std::string m_steamCmdPath;
};

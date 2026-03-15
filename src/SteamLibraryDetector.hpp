#pragma once

#include <string>
#include <vector>

/**
 * @brief Detects locally installed Steam games by scanning Steam library
 *        folders and reading appmanifest ACF files.
 *
 * Supports automatic detection of the Steam installation path on
 * Windows (registry / default path) and Linux (~/.steam/steam).
 */
class SteamLibraryDetector {
public:
    /** Represents a single installed Steam application. */
    struct InstalledApp {
        int appid = 0;
        std::string name;
        std::string installDir;       // absolute path to the app install directory
        std::string sizeOnDisk;       // human-readable size (bytes string from ACF)
    };

    SteamLibraryDetector();

    /**
     * @brief Set a custom Steam installation root.
     *
     * If empty, detect() will try the platform default.
     */
    void setSteamRoot(const std::string &path);
    std::string steamRoot() const;

    /**
     * @brief Scan the Steam installation for installed apps.
     * @return List of installed applications found.
     */
    std::vector<InstalledApp> detect() const;

    /**
     * @brief Find the default Steam installation path for the current platform.
     * @return The detected path, or empty string if not found.
     */
    static std::string detectSteamRoot();

    /**
     * @brief Parse Steam's libraryfolders.vdf to extract library paths.
     * @param vdfPath Absolute path to libraryfolders.vdf.
     * @return List of Steam library folder paths.
     */
    static std::vector<std::string> parseLibraryFolders(const std::string &vdfPath);

    /**
     * @brief Parse a single appmanifest_*.acf file.
     * @param acfPath Absolute path to the ACF file.
     * @param libraryPath The library path containing this ACF.
     * @return The parsed InstalledApp, or an app with appid==0 on failure.
     */
    static InstalledApp parseAppManifest(const std::string &acfPath,
                                         const std::string &libraryPath);

private:
    std::string m_steamRoot;
};

#include "SteamLibraryDetector.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Simple key-value extractor for Valve VDF / ACF text formats.
// Lines look like:  "key"  "value"
// ---------------------------------------------------------------------------

static std::string extractVdfValue(const std::string &text, const std::string &key)
{
    // Match: "key" followed by whitespace then "value"
    std::string pattern = "\"" + key + "\"\\s+\"([^\"]*)\"";
    std::regex re(pattern, std::regex_constants::icase);
    std::smatch m;
    if (std::regex_search(text, m, re))
        return m[1].str();
    return {};
}

// ---------------------------------------------------------------------------

SteamLibraryDetector::SteamLibraryDetector() = default;

void SteamLibraryDetector::setSteamRoot(const std::string &path)
{
    m_steamRoot = path;
}

std::string SteamLibraryDetector::steamRoot() const
{
    return m_steamRoot;
}

// ---------------------------------------------------------------------------

std::string SteamLibraryDetector::detectSteamRoot()
{
#ifdef _WIN32
    // Try reading from the registry first
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SOFTWARE\\WOW6432Node\\Valve\\Steam", 0,
                      KEY_READ, &hKey) == ERROR_SUCCESS ||
        RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SOFTWARE\\Valve\\Steam", 0,
                      KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buf[512]{};
        DWORD size = sizeof(buf);
        if (RegQueryValueExA(hKey, "InstallPath", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            if (fs::exists(buf))
                return std::string(buf);
        }
        RegCloseKey(hKey);
    }
    // Fallback: common default path
    std::string defaultPath = "C:\\Program Files (x86)\\Steam";
    if (fs::exists(defaultPath)) return defaultPath;
#else
    // Linux / macOS
    const char *home = std::getenv("HOME");
    if (home) {
        std::string linuxPath = std::string(home) + "/.steam/steam";
        if (fs::exists(linuxPath)) return linuxPath;

        // Some distros use ~/.local/share/Steam
        std::string altPath = std::string(home) + "/.local/share/Steam";
        if (fs::exists(altPath)) return altPath;

        // Flatpak Steam
        std::string flatpakPath = std::string(home) +
            "/.var/app/com.valvesoftware.Steam/.steam/steam";
        if (fs::exists(flatpakPath)) return flatpakPath;
    }
#endif
    return {};
}

// ---------------------------------------------------------------------------

std::vector<std::string> SteamLibraryDetector::parseLibraryFolders(const std::string &vdfPath)
{
    std::vector<std::string> folders;

    std::ifstream f(vdfPath);
    if (!f.is_open())
        return folders;

    std::string content{std::istreambuf_iterator<char>(f),
                        std::istreambuf_iterator<char>()};

    // libraryfolders.vdf has numbered entries like "0" { "path" "..." }
    // We look for all "path" values.
    std::regex pathRe("\"path\"\\s+\"([^\"]*)\"");
    auto begin = std::sregex_iterator(content.begin(), content.end(), pathRe);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        std::string p = (*it)[1].str();
        // Normalise Windows double-backslash escaping
        std::string normalized;
        for (size_t i = 0; i < p.size(); ++i) {
            if (p[i] == '\\' && i + 1 < p.size() && p[i + 1] == '\\') {
                normalized += fs::path::preferred_separator;
                ++i;
            } else {
                normalized += p[i];
            }
        }
        if (fs::exists(normalized))
            folders.push_back(normalized);
    }

    return folders;
}

// ---------------------------------------------------------------------------

SteamLibraryDetector::InstalledApp
SteamLibraryDetector::parseAppManifest(const std::string &acfPath,
                                        const std::string &libraryPath)
{
    InstalledApp app;

    std::ifstream f(acfPath);
    if (!f.is_open())
        return app;

    std::string content{std::istreambuf_iterator<char>(f),
                        std::istreambuf_iterator<char>()};

    std::string appidStr = extractVdfValue(content, "appid");
    app.name       = extractVdfValue(content, "name");
    app.sizeOnDisk = extractVdfValue(content, "SizeOnDisk");

    if (!appidStr.empty()) {
        try { app.appid = std::stoi(appidStr); }
        catch (...) { app.appid = 0; }
    }

    std::string installdir = extractVdfValue(content, "installdir");
    if (!installdir.empty()) {
        fs::path fullPath = fs::path(libraryPath) / "steamapps" / "common" / installdir;
        app.installDir = fullPath.string();
    }

    return app;
}

// ---------------------------------------------------------------------------

std::vector<SteamLibraryDetector::InstalledApp> SteamLibraryDetector::detect() const
{
    std::vector<InstalledApp> results;

    std::string root = m_steamRoot.empty() ? detectSteamRoot() : m_steamRoot;
    if (root.empty())
        return results;

    // The main library is always under <steamRoot>/steamapps
    // Additional libraries come from libraryfolders.vdf
    std::vector<std::string> libraryPaths;

    std::string vdfPath = (fs::path(root) / "steamapps" / "libraryfolders.vdf").string();
    if (fs::exists(vdfPath)) {
        libraryPaths = parseLibraryFolders(vdfPath);
    }

    // Always include the main steam root as a library path
    bool hasRoot = false;
    for (const auto &p : libraryPaths) {
        if (fs::equivalent(fs::path(p), fs::path(root))) {
            hasRoot = true;
            break;
        }
    }
    if (!hasRoot)
        libraryPaths.insert(libraryPaths.begin(), root);

    // Scan each library for appmanifest_*.acf files
    for (const std::string &libPath : libraryPaths) {
        fs::path steamapps = fs::path(libPath) / "steamapps";
        if (!fs::exists(steamapps))
            continue;

        std::error_code ec;
        for (const auto &entry : fs::directory_iterator(steamapps, ec)) {
            if (!entry.is_regular_file())
                continue;
            std::string filename = entry.path().filename().string();
            if (filename.size() > 12 &&
                filename.substr(0, 12) == "appmanifest_" &&
                entry.path().extension() == ".acf") {
                InstalledApp app = parseAppManifest(entry.path().string(), libPath);
                if (app.appid > 0 && !app.name.empty())
                    results.push_back(app);
            }
        }
    }

    // Sort by name
    std::sort(results.begin(), results.end(), [](const InstalledApp &a, const InstalledApp &b) {
        return a.name < b.name;
    });

    return results;
}

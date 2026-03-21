#include "BackupModule.hpp"

#include <filesystem>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <algorithm>
#include <sstream>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Zip helpers – use platform-native tools via system() so we don't need an
// additional library dependency.
//   Windows  : PowerShell Compress-Archive / Expand-Archive
//   Linux/Mac: zip / unzip (paths passed as arguments)
// ---------------------------------------------------------------------------

#ifdef _WIN32
// Escape a path for use inside a PowerShell single-quoted string ('...').
// In PowerShell single-quoted strings only ' itself is special (escape as '').
// Using single quotes avoids the double-quote nesting problem that arises when
// cmd.exe parses the outer powershell -Command "..." invocation.
static std::string escapePwshPath(const std::string &path)
{
    return replaceAll(path, "'", "''");
}
#else
// Escape a path for use inside a POSIX shell single-quoted string ('...').
// The only character that cannot appear inside single quotes is ' itself,
// which must be ended, escaped as \', then re-opened: '\''
static std::string escapeShPath(const std::string &path)
{
    return replaceAll(path, "'", "'\\''");
}
#endif

static int runCommand(const std::string &cmd)
{
    return std::system(cmd.c_str());
}

bool BackupModule::createZip(const std::string &sourceDir, const std::string &destZip,
                             int compressionLevel)
{
    if (!fs::exists(sourceDir) || !fs::is_directory(sourceDir)) {
        std::cerr << "BackupModule::createZip: source dir does not exist: " << sourceDir << "\n";
        return false;
    }

    if (compressionLevel < 0) compressionLevel = 0;
    if (compressionLevel > 9) compressionLevel = 9;

    // Ensure destination parent directory exists
    fs::create_directories(fs::path(destZip).parent_path());

#ifdef _WIN32
    std::string levelArg = (compressionLevel == 0) ? "NoCompression" : "Optimal";
    std::string nativeSrc = fs::path(sourceDir).string();
    std::string nativeDst = fs::path(destZip).string();
    // Use single-quoted strings so that cmd.exe's double-quote stripping cannot
    // break paths that contain spaces (e.g. "C:\My Projects\...").
    std::string script =
        "$src = '" + escapePwshPath(nativeSrc) + "\\*'; "
        "$dst = '" + escapePwshPath(nativeDst) + "'; "
        "Compress-Archive -LiteralPath $src -DestinationPath $dst"
        " -CompressionLevel " + levelArg + " -Force";
    std::string cmd = "powershell -NoProfile -NonInteractive -Command \"" + script + "\"";
#else
    std::string cmd = "cd '" + escapeShPath(sourceDir) + "' && zip -r -"
                    + std::to_string(compressionLevel)
                    + " '" + escapeShPath(destZip) + "' . 2>&1";
#endif
    int ret = runCommand(cmd);
    if (ret != 0) {
        std::cerr << "BackupModule::createZip: process failed with exit code " << ret << "\n";
        return false;
    }
    return true;
}

bool BackupModule::extractZip(const std::string &zipFile, const std::string &destDir)
{
    fs::create_directories(destDir);

#ifdef _WIN32
    std::string nativeSrc = fs::path(zipFile).string();
    std::string nativeDst = fs::path(destDir).string();
    std::string script =
        "$src = '" + escapePwshPath(nativeSrc) + "'; "
        "$dst = '" + escapePwshPath(nativeDst) + "'; "
        "Expand-Archive -LiteralPath $src -DestinationPath $dst -Force";
    std::string cmd = "powershell -NoProfile -NonInteractive -Command \"" + script + "\"";
#else
    std::string cmd = "unzip -o '" + escapeShPath(zipFile) + "' -d '"
                    + escapeShPath(destDir) + "' 2>&1";
#endif
    int ret = runCommand(cmd);
    if (ret != 0) {
        std::cerr << "BackupModule::extractZip: process failed with exit code " << ret << "\n";
        return false;
    }
    return true;
}

std::string BackupModule::takeSnapshot(const ServerConfig &server)
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char tsBuf[32];
    std::strftime(tsBuf, sizeof(tsBuf), "%Y%m%d_%H%M%S", &tm);
    std::string timestamp(tsBuf);

    std::string base = (fs::path(server.backupFolder) / timestamp).string();
    fs::create_directories(server.backupFolder);

    bool ok = true;

    std::string configDir = (fs::path(server.dir) / "Configs").string();
    if (fs::exists(configDir))
        ok &= createZip(configDir, base + "_config.zip", server.backupCompressionLevel);

    std::string mapDir = (fs::path(server.dir) / "Maps").string();
    if (fs::exists(mapDir))
        ok &= createZip(mapDir, base + "_map.zip", server.backupCompressionLevel);

    std::string modsDir = (fs::path(server.dir) / "Mods").string();
    if (fs::exists(modsDir))
        ok &= createZip(modsDir, base + "_mods.zip", server.backupCompressionLevel);

    if (!ok) {
        std::cerr << "BackupModule::takeSnapshot: one or more parts failed for " << server.name << "\n";
        return {};
    }

    rotateBackups(server);
    return timestamp;
}

bool BackupModule::restoreSnapshot(const std::string &zipFile, const ServerConfig &server)
{
    fs::path p(zipFile);
    std::string base = p.stem().string();  // e.g. "20260301_120000_config"

    std::string destDir;
    if (base.size() >= 7 && base.substr(base.size() - 7) == "_config")
        destDir = (fs::path(server.dir) / "Configs").string();
    else if (base.size() >= 4 && base.substr(base.size() - 4) == "_map")
        destDir = (fs::path(server.dir) / "Maps").string();
    else if (base.size() >= 5 && base.substr(base.size() - 5) == "_mods")
        destDir = (fs::path(server.dir) / "Mods").string();
    else
        destDir = server.dir;

    return extractZip(zipFile, destDir);
}

std::vector<std::string> BackupModule::listSnapshots(const ServerConfig &server)
{
    if (!fs::exists(server.backupFolder))
        return {};

    std::vector<std::string> result;
    for (const auto &entry : fs::directory_iterator(server.backupFolder)) {
        if (entry.is_regular_file() && entry.path().extension() == ".zip")
            result.push_back(fs::absolute(entry.path()).string());
    }
    // Sort newest-first (reverse alphabetical)
    std::sort(result.begin(), result.end(), std::greater<std::string>());
    return result;
}

void BackupModule::rotateBackups(const ServerConfig &server)
{
    std::vector<std::string> all = listSnapshots(server);
    std::vector<std::string> types = { "_config.zip", "_map.zip", "_mods.zip" };

    for (const std::string &suffix : types) {
        std::vector<std::string> typed;
        for (const std::string &f : all) {
            if (f.size() >= suffix.size() &&
                f.substr(f.size() - suffix.size()) == suffix)
                typed.push_back(f);
        }
        // Already sorted newest-first
        for (int i = server.keepBackups; i < static_cast<int>(typed.size()); ++i)
            fs::remove(typed[i]);
    }
}

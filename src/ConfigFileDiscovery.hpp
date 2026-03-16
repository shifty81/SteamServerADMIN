#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>

/**
 * @brief Discovers editable configuration files within a server directory.
 *
 * Scans recursively for common config file extensions (.ini, .cfg, .conf,
 * .yaml, .yml, .json, .xml, .properties, .txt, .lua, .dat) while skipping
 * large binary directories (e.g. Engine/Binaries, steamapps).
 *
 * Results are returned as paths relative to the server root directory.
 */
class ConfigFileDiscovery {
public:
    /** Well-known config-file extensions (lowercase, with leading dot). */
    static std::vector<std::string> configExtensions()
    {
        return { ".ini", ".cfg", ".conf", ".yaml", ".yml",
                 ".json", ".xml", ".properties", ".txt", ".lua", ".dat" };
    }

    /**
     * @brief Scan @p serverDir for config files.
     * @param serverDir  Absolute path to the server installation directory.
     * @param maxDepth   Maximum directory depth to recurse (0 = root only).
     * @param maxResults Maximum number of results to return.
     * @return Relative paths (to serverDir) of discovered config files, sorted.
     */
    static std::vector<std::string> discover(const std::string &serverDir,
                                             int maxDepth = 5,
                                             int maxResults = 200)
    {
        namespace fs = std::filesystem;
        std::vector<std::string> results;

        if (serverDir.empty() || !fs::is_directory(serverDir))
            return results;

        const auto exts = configExtensions();
        const fs::path root(serverDir);

        try {
            for (auto it = fs::recursive_directory_iterator(
                     root, fs::directory_options::skip_permission_denied);
                 it != fs::recursive_directory_iterator(); ++it)
            {
                // Respect depth limit
                if (it.depth() > maxDepth) {
                    it.disable_recursion_pending();
                    continue;
                }

                // Skip symlinks to prevent symlink attacks
                if (it->is_symlink())
                    continue;

                // Skip well-known binary / cache directories
                if (it->is_directory()) {
                    std::string dirName = it->path().filename().string();
                    if (shouldSkipDir(dirName)) {
                        it.disable_recursion_pending();
                        continue;
                    }
                }

                if (!it->is_regular_file())
                    continue;

                // Check extension
                std::string ext = it->path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                               [](unsigned char c){ return std::tolower(c); });

                bool match = false;
                for (const auto &e : exts) {
                    if (ext == e) { match = true; break; }
                }
                if (!match)
                    continue;

                // Skip files larger than 2 MB (likely not hand-edited configs)
                if (it->file_size() > 2 * 1024 * 1024)
                    continue;

                auto rel = fs::relative(it->path(), root);
                std::string relStr = rel.generic_string();

                // Reject paths that escape the root via ../
                if (relStr.find("..") != std::string::npos)
                    continue;

                results.push_back(relStr);

                if (static_cast<int>(results.size()) >= maxResults)
                    break;
            }
        } catch (const fs::filesystem_error &) {
            // Best-effort scan; ignore permission / symlink errors
        }

        std::sort(results.begin(), results.end());
        return results;
    }

    /**
     * @brief Generate an appropriate server folder name from a template hint
     *        and user-given server name.
     *
     * @param folderHint  Short game-type hint (e.g. "ark_sa", "cs2").
     * @param serverName  User-given server name (e.g. "My Server").
     * @return A filesystem-safe folder name (e.g. "ark_sa_My_Server").
     */
    static std::string generateFolderName(const std::string &folderHint,
                                          const std::string &serverName)
    {
        std::string name = folderHint.empty() ? "server" : folderHint;

        if (!serverName.empty()) {
            name += "_";
            // Sanitize the server name for filesystem safety
            for (char c : serverName) {
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')
                    name += c;
                else if (c == ' ')
                    name += '_';
                // else skip unsafe chars
            }
        }

        // Remove trailing underscores
        while (!name.empty() && name.back() == '_')
            name.pop_back();

        return name.empty() ? "server" : name;
    }

    /**
     * @brief Return the default base directory for all servers.
     *
     * This is a "servers" folder next to the running executable.
     */
    static std::string defaultServersBaseDir()
    {
        namespace fs = std::filesystem;
        return (fs::current_path() / "servers").string();
    }

private:
    /// Directories to skip during recursive scan (common binary / cache dirs).
    static bool shouldSkipDir(const std::string &name)
    {
        // Skip hidden directories
        if (!name.empty() && name[0] == '.')
            return true;

        // Common binary / intermediate dirs in game servers
        static const char *skipDirs[] = {
            "Binaries", "Content", "Engine",
            "steamapps", "steamcmd", "__pycache__",
            "node_modules", ".git", "logs", "Logs",
            "CrashReports", "Crashes",
        };
        for (const char *d : skipDirs) {
            if (name == d) return true;
        }
        return false;
    }
};

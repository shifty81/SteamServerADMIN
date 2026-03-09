#pragma once

#include <string>
#include <vector>
#include <map>
#include <algorithm>

inline std::string trimString(const std::string &s) {
    auto start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

inline std::string replaceAll(std::string str, const std::string &from, const std::string &to) {
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
    return str;
}

struct RconInfo {
    std::string host;
    int port = 27015;
    std::string password;
};

struct ServerConfig {
    std::string name;
    int appid = 0;
    std::string dir;
    std::string executable;       // server EXE, relative to dir (e.g. "ShooterGameServer.exe")
    std::string launchArgs;       // extra command-line arguments passed to the server executable
    RconInfo rcon;
    std::vector<int> mods;
    std::vector<int> disabledMods;   // mods that are installed but not active on launch
    std::string backupFolder;
    std::string notes;             // free-form user notes / description for this server
    std::string discordWebhookUrl; // Discord webhook URL for event notifications
    std::string webhookTemplate;   // custom webhook message template; placeholders: {server}, {event}, {timestamp}
    bool autoUpdate = true;
    bool autoStartOnLaunch = false;  // start this server when the SSA application launches
    bool favorite = false;           // pinned / favorite server (appears first in sidebar)
    int backupIntervalMinutes = 30;
    int restartIntervalHours = 24;
    int keepBackups = 10;     // maximum number of versioned backups to retain
    std::vector<std::string> scheduledRconCommands; // RCON commands to run at a scheduled interval
    int rconCommandIntervalMinutes = 0; // 0 = disabled
    int backupCompressionLevel = 6;    // zip compression level (0=store, 1=fastest … 9=best; on Windows only 0 vs 1-9 are distinguished)
    int maintenanceStartHour = -1;     // hour (0-23) when maintenance window starts; -1 = disabled
    int maintenanceEndHour   = -1;     // hour (0-23) when maintenance window ends;   -1 = disabled
    bool consoleLogging = false;       // save RCON console sessions to disk
    int maxPlayers = 0;                // max player slots; 0 = unset / unlimited
    int restartWarningMinutes = 15;    // minutes before scheduled restart to start in-game warnings; 0 = disabled
    std::string restartWarningMessage;     // custom RCON warning template; placeholder: {minutes}. Empty = default message

    // ---- Resource monitoring alert thresholds ----
    double cpuAlertThreshold = 90.0;   // alert when CPU% exceeds this (0 = disabled)
    double memAlertThresholdMB = 0.0;  // alert when RSS exceeds this in MB (0 = disabled)

    // ---- Event hook scripts (per-event shell / script paths) ----
    std::map<std::string, std::string> eventHooks; // key = event name (e.g. "onStart"), value = script path

    // ---- User-defined tags for categorization / filtering ----
    std::vector<std::string> tags;

    // ---- Server group for organization ----
    std::string group;                         // named group (e.g. "Production", "Testing", "ARK Cluster")

    // ---- Startup priority for auto-start ordering ----
    int startupPriority = 0;               // lower value starts first; 0 = default

    // ---- Auto-backup before scheduled restart ----
    bool backupBeforeRestart = false;      // take a snapshot before every scheduled restart

    // ---- Graceful shutdown timeout ----
    int gracefulShutdownSeconds = 10;      // seconds to wait after terminate before force-kill; 0 = immediate kill

    // ---- Custom environment variables for server process ----
    std::map<std::string, std::string> environmentVariables; // key=value pairs passed to the server process on launch

    /**
     * @brief Format a restart warning message with the given minutes remaining.
     * @param minutes Minutes until the server restarts.
     * @return The formatted message string.
     */
    std::string formatRestartWarning(int minutes) const
    {
        std::string tpl = trimString(restartWarningMessage).empty()
            ? std::string("Server will restart in {minutes} minute(s). Please save your progress.")
            : restartWarningMessage;
        return replaceAll(tpl, "{minutes}", std::to_string(minutes));
    }

    /**
     * @brief Validate this server configuration.
     * @return A list of human-readable error strings. Empty list means valid.
     */
    std::vector<std::string> validate() const
    {
        std::vector<std::string> errors;

        if (trimString(name).empty())
            errors.push_back("Server name must not be empty.");

        if (appid <= 0)
            errors.push_back("Steam AppID must be a positive integer.");

        if (trimString(dir).empty())
            errors.push_back("Installation directory must not be empty.");

        if (rcon.port < 1 || rcon.port > 65535)
            errors.push_back("RCON port must be between 1 and 65535.");

        if (keepBackups < 0)
            errors.push_back("Keep-backups count must not be negative.");

        if (backupIntervalMinutes < 0)
            errors.push_back("Backup interval must not be negative.");

        if (restartIntervalHours < 0)
            errors.push_back("Restart interval must not be negative.");

        if (rconCommandIntervalMinutes < 0)
            errors.push_back("RCON command interval must not be negative.");

        if (backupCompressionLevel < 0 || backupCompressionLevel > 9)
            errors.push_back("Backup compression level must be between 0 and 9.");

        if (maintenanceStartHour != -1 && (maintenanceStartHour < 0 || maintenanceStartHour > 23))
            errors.push_back("Maintenance start hour must be between 0 and 23 (or -1 to disable).");

        if (maintenanceEndHour != -1 && (maintenanceEndHour < 0 || maintenanceEndHour > 23))
            errors.push_back("Maintenance end hour must be between 0 and 23 (or -1 to disable).");

        if (maxPlayers < 0)
            errors.push_back("Max players must not be negative.");

        if (restartWarningMinutes < 0)
            errors.push_back("Restart warning minutes must not be negative.");

        if (cpuAlertThreshold < 0.0)
            errors.push_back("CPU alert threshold must not be negative.");

        if (memAlertThresholdMB < 0.0)
            errors.push_back("Memory alert threshold must not be negative.");

        if (startupPriority < 0)
            errors.push_back("Startup priority must not be negative.");

        if (gracefulShutdownSeconds < 0)
            errors.push_back("Graceful shutdown timeout must not be negative.");

        return errors;
    }
};

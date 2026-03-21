#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <functional>

/**
 * @brief User roles for access control, ordered from least to most privileged.
 */
enum class UserRole {
    Player    = 0, ///< Standard player – no management access.
    Operator  = 1, ///< Can start/stop servers and view logs.
    Moderator = 2, ///< Can start/stop/restart servers, send RCON commands, manage whitelists, kick/ban players, and view logs.
    Admin     = 3  ///< Full management access.
};

/**
 * @brief Granular permission flags checked against a user's role.
 */
enum class Permission {
    StartServer,      ///< Launch a server process.
    StopServer,       ///< Stop a running server process.
    RestartServer,    ///< Restart a server (stop + start).
    UpdateServer,     ///< Run a SteamCMD update for a server.
    InstallServer,    ///< Install a new server via SteamCMD.
    ViewLogs,         ///< Read server console / log output.
    SendRconCommand,  ///< Send arbitrary RCON commands.
    ManageWhitelist,  ///< Add or remove whitelist entries.
    ManageAdmins,     ///< Add, remove, or change user roles.
    ManageBackups,    ///< Trigger or manage server backups.
    ManageScheduler,  ///< Modify scheduled tasks.
    KickPlayer,       ///< Kick a connected player.
    BanPlayer         ///< Ban a player from the server.
};

/**
 * @brief A user known to the role management system.
 */
struct User {
    std::string steamId; ///< Steam 64-bit ID (string form).
    std::string name;    ///< Human-readable display name.
    UserRole    role = UserRole::Player;
};

/**
 * @brief A single entry in the permission-change audit trail.
 */
struct AuditEntry {
    std::string timestamp; ///< ISO 8601 UTC timestamp.
    std::string actorId;   ///< SteamId (or "system") of who performed the action.
    std::string action;    ///< Human-readable description of the change.
    std::string targetId;  ///< SteamId of the affected user (may be empty).
};

/**
 * @brief Manages users, roles, whitelists, and permissions for all servers.
 *
 * Responsibilities:
 *  - Persist users and their roles to a JSON file.
 *  - Read/write per-server whitelist text files (one Steam ID per line).
 *  - Check whether a user has a specific permission based on their role.
 *  - Record every role/whitelist change in an in-memory audit log.
 */
class UserRoleManager {
public:
    UserRoleManager() = default;
    explicit UserRoleManager(const std::string &filePath);

    // ---- Persistence -------------------------------------------------------
    /** Load users and whitelist from a JSON file.  Returns false on error. */
    bool load(const std::string &filePath);

    /** Save users and whitelist to a JSON file.  Returns false on error. */
    bool save(const std::string &filePath) const;

    // ---- User management ---------------------------------------------------
    /** Add or update a user.  Logged to the audit trail. */
    void addUser(const User &user, const std::string &actorId = "system");

    /** Remove a user by Steam ID.  Returns false if not found. */
    bool removeUser(const std::string &steamId,
                    const std::string &actorId = "system");

    /** Change the role of an existing user.  Returns false if not found. */
    bool setRole(const std::string &steamId, UserRole role,
                 const std::string &actorId = "system");

    /** Look up a user by Steam ID.  Returns nullopt if not found. */
    std::optional<User> findUser(const std::string &steamId) const;

    /** Read-only view of all registered users. */
    const std::vector<User> &users() const { return m_users; }

    // ---- Whitelist ---------------------------------------------------------
    /** Returns true if the given Steam ID is on the whitelist. */
    bool isWhitelisted(const std::string &steamId) const;

    /** Add a Steam ID to the whitelist.  Logged to the audit trail. */
    void addToWhitelist(const std::string &steamId,
                        const std::string &actorId = "system");

    /** Remove a Steam ID from the whitelist.  Returns false if not present. */
    bool removeFromWhitelist(const std::string &steamId,
                             const std::string &actorId = "system");

    /**
     * @brief Load whitelist entries from a plain-text file (one ID per line).
     *        Lines beginning with '#' and blank lines are ignored.
     *        Existing whitelist entries are preserved; new ones are merged in.
     */
    bool loadWhitelistFile(const std::string &filePath);

    /**
     * @brief Save the current whitelist to a plain-text file (one ID per line).
     */
    bool saveWhitelistFile(const std::string &filePath) const;

    /** Read-only view of all whitelisted Steam IDs. */
    const std::set<std::string> &whitelist() const { return m_whitelist; }

    // ---- Permissions -------------------------------------------------------
    /** Returns true if the user with the given Steam ID holds @p perm. */
    bool hasPermission(const std::string &steamId, Permission perm) const;

    /** Returns true if @p role is sufficient to hold @p perm. */
    static bool roleHasPermission(UserRole role, Permission perm);

    /** Returns the display name for a role (e.g. "Admin", "Moderator"). */
    static std::string roleName(UserRole role);

    /** Returns the display name for a permission. */
    static std::string permissionName(Permission perm);

    // ---- Audit log ---------------------------------------------------------
    /** Read-only view of all recorded audit entries (most-recent last). */
    const std::vector<AuditEntry> &auditLog() const { return m_auditLog; }

    /** Clear the in-memory audit log. */
    void clearAuditLog() { m_auditLog.clear(); }

private:
    void logAudit(const std::string &actorId, const std::string &action,
                  const std::string &targetId = "");

    std::vector<User>        m_users;
    std::set<std::string>    m_whitelist;
    std::vector<AuditEntry>  m_auditLog;
};

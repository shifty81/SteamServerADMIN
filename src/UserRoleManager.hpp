#pragma once

#include <string>
#include <vector>
#include <optional>

/**
 * @brief A player's standing in a game server.
 *
 * These roles map directly to the access-control concepts used by most
 * Steam dedicated servers:
 *
 *  Whitelisted – player is permitted to connect (whitelist / permittedlist).
 *  Operator    – player has elevated in-game privileges (moderator / op).
 *  Admin       – player has full admin privileges in the game server.
 *  Banned      – player is blocked from connecting (blacklist / bannedlist).
 */
enum class ServerPlayerRole {
    Whitelisted = 0, ///< Allowed to connect; no extra in-game privileges.
    Operator    = 1, ///< Moderator / operator level in-game privileges.
    Admin       = 2, ///< Full admin level in-game privileges.
    Banned      = 3  ///< Banned from connecting to the server.
};

/**
 * @brief A player tracked in the server access-control lists.
 */
struct PlayerEntry {
    std::string      steamId; ///< Steam 64-bit ID (string form).
    std::string      name;    ///< Optional human-readable display name.
    ServerPlayerRole role = ServerPlayerRole::Whitelisted;
};

/**
 * @brief A single entry in the audit trail.
 */
struct AuditEntry {
    std::string timestamp; ///< ISO 8601 UTC timestamp.
    std::string actorId;   ///< SteamId (or "system") who performed the action.
    std::string action;    ///< Human-readable description of the change.
    std::string targetId;  ///< SteamId of the affected player (may be empty).
};

/**
 * @brief Manages the player access-control lists for a game server.
 *
 * Tracks players with their game-server roles (Whitelisted / Operator /
 * Admin / Banned) and provides helpers to read and write the native config
 * files that dedicated servers use for these lists:
 *
 *  - Plain-text files (one Steam ID per line) used by Valheim, ARK, etc.
 *    e.g. adminlist.txt, permittedlist.txt, bannedlist.txt
 *
 *  - XML admin files used by 7 Days to Die (serveradmin.xml) that contain
 *    <admins>, <moderators>, <whitelist>, and <blacklist> sections.
 *
 * Every explicit change (add / remove / role change) is recorded in an
 * in-memory audit log.
 */
class UserRoleManager {
public:
    UserRoleManager() = default;
    explicit UserRoleManager(const std::string &filePath);

    // ---- Persistence (SSA internal JSON cache) -----------------------------
    /** Load players from SSA's own JSON cache file.  Returns false on error. */
    bool load(const std::string &filePath);

    /** Save players to SSA's own JSON cache file.  Returns false on error. */
    bool save(const std::string &filePath) const;

    // ---- Player management -------------------------------------------------
    /** Add a new player or update an existing one (matched by steamId). */
    void addPlayer(const PlayerEntry &entry,
                   const std::string &actorId = "system");

    /** Remove a player by Steam ID.  Returns false if not found. */
    bool removePlayer(const std::string &steamId,
                      const std::string &actorId = "system");

    /** Change the role of an existing player.  Returns false if not found. */
    bool setPlayerRole(const std::string &steamId, ServerPlayerRole role,
                       const std::string &actorId = "system");

    /** Look up a player by Steam ID.  Returns nullopt if not found. */
    std::optional<PlayerEntry> findPlayer(const std::string &steamId) const;

    /** Read-only view of all tracked players. */
    const std::vector<PlayerEntry> &players() const { return m_players; }

    /** Return all players that have the specified role. */
    std::vector<PlayerEntry> getPlayersByRole(ServerPlayerRole role) const;

    // ---- Convenience queries -----------------------------------------------
    /** True if the player is tracked as Admin. */
    bool isAdmin(const std::string &steamId) const;

    /** True if the player is tracked as Banned. */
    bool isBanned(const std::string &steamId) const;

    /**
     * True if the player is explicitly Whitelisted, Operator, or Admin.
     * Returns false for Banned players and for unknown Steam IDs.
     */
    bool isPermitted(const std::string &steamId) const;

    // ---- Plain-text file I/O (one Steam ID per line) -----------------------
    /**
     * @brief Load entries from a plain-text game server list file.
     *
     * Each non-blank, non-comment line is treated as a Steam ID.  Lines
     * beginning with '//' or '#' are skipped.  Loaded entries are assigned
     * @p role and merged into the existing list (insert or update by steamId).
     * File-load operations are not written to the audit log.
     */
    bool loadPlainTextFile(const std::string &filePath, ServerPlayerRole role);

    /**
     * @brief Write all players with @p role to a plain-text file.
     *
     * Produces one Steam ID per line, preceded by a short comment header.
     */
    bool savePlainTextFile(const std::string &filePath,
                           ServerPlayerRole role) const;

    // ---- XML file I/O (7 Days to Die serveradmin.xml format) ---------------
    /**
     * @brief Load all role sections from a 7 Days to Die serveradmin.xml.
     *
     * Recognises <admins>, <moderators>, <whitelist>, and <blacklist>
     * sections and maps them to Admin, Operator, Whitelisted, and Banned
     * respectively.  Entries are merged into the existing list.
     * File-load operations are not written to the audit log.
     */
    bool loadXmlAdminFile(const std::string &filePath);

    /**
     * @brief Save all players to a 7 Days to Die-style serveradmin.xml.
     */
    bool saveXmlAdminFile(const std::string &filePath) const;

    // ---- Helpers -----------------------------------------------------------
    /** Return the display name for a role (e.g. "Admin", "Banned"). */
    static std::string roleName(ServerPlayerRole role);

    // ---- Audit log ---------------------------------------------------------
    /** Read-only view of all recorded audit entries (most-recent last). */
    const std::vector<AuditEntry> &auditLog() const { return m_auditLog; }

    /** Clear the in-memory audit log. */
    void clearAuditLog() { m_auditLog.clear(); }

private:
    void logAudit(const std::string &actorId, const std::string &action,
                  const std::string &targetId = "");

    // Internal upsert used by file-load methods (no audit logging).
    void upsertPlayer(const std::string &steamId, ServerPlayerRole role);

    std::vector<PlayerEntry>  m_players;
    std::vector<AuditEntry>   m_auditLog;
};

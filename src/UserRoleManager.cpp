#include "UserRoleManager.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static std::string utcNow()
{
    auto now   = std::chrono::system_clock::now();
    auto time  = std::chrono::system_clock::to_time_t(now);
    std::tm tm = {};
#ifdef _WIN32
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

UserRoleManager::UserRoleManager(const std::string &filePath)
{
    load(filePath);
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

bool UserRoleManager::load(const std::string &filePath)
{
    std::ifstream f(filePath);
    if (!f.is_open())
        return false;

    json root;
    try {
        f >> root;
    } catch (...) {
        return false;
    }

    m_users.clear();
    if (root.contains("users") && root["users"].is_array()) {
        for (const auto &obj : root["users"]) {
            User u;
            u.steamId = obj.value("steamId", "");
            u.name    = obj.value("name", "");
            u.role    = static_cast<UserRole>(obj.value("role", 0));
            if (!u.steamId.empty())
                m_users.push_back(std::move(u));
        }
    }

    m_whitelist.clear();
    if (root.contains("whitelist") && root["whitelist"].is_array()) {
        for (const auto &id : root["whitelist"]) {
            if (id.is_string() && !id.get<std::string>().empty())
                m_whitelist.insert(id.get<std::string>());
        }
    }

    return true;
}

bool UserRoleManager::save(const std::string &filePath) const
{
    json root;

    json usersArr = json::array();
    for (const auto &u : m_users) {
        json obj;
        obj["steamId"] = u.steamId;
        obj["name"]    = u.name;
        obj["role"]    = static_cast<int>(u.role);
        usersArr.push_back(obj);
    }
    root["users"] = usersArr;

    json wlArr = json::array();
    for (const auto &id : m_whitelist)
        wlArr.push_back(id);
    root["whitelist"] = wlArr;

    std::ofstream f(filePath);
    if (!f.is_open())
        return false;

    f << root.dump(2);
    return f.good();
}

// ---------------------------------------------------------------------------
// User management
// ---------------------------------------------------------------------------

void UserRoleManager::addUser(const User &user, const std::string &actorId)
{
    auto it = std::find_if(m_users.begin(), m_users.end(),
                           [&](const User &u) { return u.steamId == user.steamId; });
    if (it != m_users.end()) {
        *it = user;
        logAudit(actorId, "Updated user '" + user.name + "' role to " + roleName(user.role),
                 user.steamId);
    } else {
        m_users.push_back(user);
        logAudit(actorId, "Added user '" + user.name + "' with role " + roleName(user.role),
                 user.steamId);
    }
}

bool UserRoleManager::removeUser(const std::string &steamId,
                                 const std::string &actorId)
{
    auto it = std::find_if(m_users.begin(), m_users.end(),
                           [&](const User &u) { return u.steamId == steamId; });
    if (it == m_users.end())
        return false;

    logAudit(actorId, "Removed user '" + it->name + "'", steamId);
    m_users.erase(it);
    return true;
}

bool UserRoleManager::setRole(const std::string &steamId, UserRole role,
                              const std::string &actorId)
{
    auto it = std::find_if(m_users.begin(), m_users.end(),
                           [&](const User &u) { return u.steamId == steamId; });
    if (it == m_users.end())
        return false;

    it->role = role;
    logAudit(actorId, "Changed role of '" + it->name + "' to " + roleName(role), steamId);
    return true;
}

std::optional<User> UserRoleManager::findUser(const std::string &steamId) const
{
    auto it = std::find_if(m_users.begin(), m_users.end(),
                           [&](const User &u) { return u.steamId == steamId; });
    if (it == m_users.end())
        return std::nullopt;
    return *it;
}

// ---------------------------------------------------------------------------
// Whitelist
// ---------------------------------------------------------------------------

bool UserRoleManager::isWhitelisted(const std::string &steamId) const
{
    return m_whitelist.count(steamId) > 0;
}

void UserRoleManager::addToWhitelist(const std::string &steamId,
                                     const std::string &actorId)
{
    if (m_whitelist.insert(steamId).second)
        logAudit(actorId, "Added " + steamId + " to whitelist", steamId);
}

bool UserRoleManager::removeFromWhitelist(const std::string &steamId,
                                          const std::string &actorId)
{
    if (m_whitelist.erase(steamId) == 0)
        return false;
    logAudit(actorId, "Removed " + steamId + " from whitelist", steamId);
    return true;
}

bool UserRoleManager::loadWhitelistFile(const std::string &filePath)
{
    std::ifstream f(filePath);
    if (!f.is_open())
        return false;

    std::string line;
    while (std::getline(f, line)) {
        // Strip carriage return
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        // Skip blank lines and comments
        if (line.empty() || line[0] == '#')
            continue;
        m_whitelist.insert(line);
    }
    return true;
}

bool UserRoleManager::saveWhitelistFile(const std::string &filePath) const
{
    std::ofstream f(filePath);
    if (!f.is_open())
        return false;

    for (const auto &id : m_whitelist)
        f << id << "\n";

    return f.good();
}

// ---------------------------------------------------------------------------
// Permissions
// ---------------------------------------------------------------------------

bool UserRoleManager::roleHasPermission(UserRole role, Permission perm)
{
    // Admin has every permission.
    if (role == UserRole::Admin)
        return true;

    // Permission matrix:
    //
    // Permission          | Player | Operator | Moderator | Admin
    // --------------------|--------|----------|-----------|------
    // StartServer         |        |    ✓     |     ✓     |  ✓
    // StopServer          |        |    ✓     |     ✓     |  ✓
    // RestartServer       |        |    ✓     |     ✓     |  ✓
    // ViewLogs            |        |    ✓     |     ✓     |  ✓
    // SendRconCommand     |        |          |     ✓     |  ✓
    // ManageWhitelist     |        |          |     ✓     |  ✓
    // KickPlayer          |        |          |     ✓     |  ✓
    // BanPlayer           |        |          |     ✓     |  ✓
    // UpdateServer        |        |          |           |  ✓
    // InstallServer       |        |          |           |  ✓
    // ManageAdmins        |        |          |           |  ✓
    // ManageBackups       |        |          |           |  ✓
    // ManageScheduler     |        |          |           |  ✓

    switch (perm) {
        // Operator and above: start, stop, restart, view logs.
        case Permission::StartServer:
        case Permission::StopServer:
        case Permission::RestartServer:
        case Permission::ViewLogs:
            return role >= UserRole::Operator;

        // Moderator and above: kick/ban, send RCON, manage whitelist.
        case Permission::KickPlayer:
        case Permission::BanPlayer:
        case Permission::SendRconCommand:
        case Permission::ManageWhitelist:
            return role >= UserRole::Moderator;

        // Admin-only: update/install, manage admins, backups, scheduler.
        case Permission::UpdateServer:
        case Permission::InstallServer:
        case Permission::ManageAdmins:
        case Permission::ManageBackups:
        case Permission::ManageScheduler:
            return false;
    }
    return false;
}

bool UserRoleManager::hasPermission(const std::string &steamId, Permission perm) const
{
    auto u = findUser(steamId);
    if (!u.has_value())
        return false;
    return roleHasPermission(u->role, perm);
}

std::string UserRoleManager::roleName(UserRole role)
{
    switch (role) {
        case UserRole::Admin:     return "Admin";
        case UserRole::Moderator: return "Moderator";
        case UserRole::Operator:  return "Operator";
        case UserRole::Player:    return "Player";
    }
    return "Unknown";
}

std::string UserRoleManager::permissionName(Permission perm)
{
    switch (perm) {
        case Permission::StartServer:     return "StartServer";
        case Permission::StopServer:      return "StopServer";
        case Permission::RestartServer:   return "RestartServer";
        case Permission::UpdateServer:    return "UpdateServer";
        case Permission::InstallServer:   return "InstallServer";
        case Permission::ViewLogs:        return "ViewLogs";
        case Permission::SendRconCommand: return "SendRconCommand";
        case Permission::ManageWhitelist: return "ManageWhitelist";
        case Permission::ManageAdmins:    return "ManageAdmins";
        case Permission::ManageBackups:   return "ManageBackups";
        case Permission::ManageScheduler: return "ManageScheduler";
        case Permission::KickPlayer:      return "KickPlayer";
        case Permission::BanPlayer:       return "BanPlayer";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// Audit log
// ---------------------------------------------------------------------------

void UserRoleManager::logAudit(const std::string &actorId,
                                const std::string &action,
                                const std::string &targetId)
{
    AuditEntry entry;
    entry.timestamp = utcNow();
    entry.actorId   = actorId;
    entry.action    = action;
    entry.targetId  = targetId;
    m_auditLog.push_back(std::move(entry));
}

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

    m_players.clear();
    if (root.contains("players") && root["players"].is_array()) {
        for (const auto &obj : root["players"]) {
            PlayerEntry e;
            e.steamId = obj.value("steamId", "");
            e.name    = obj.value("name", "");
            e.role    = static_cast<ServerPlayerRole>(obj.value("role", 0));
            if (!e.steamId.empty())
                m_players.push_back(std::move(e));
        }
    }

    return true;
}

bool UserRoleManager::save(const std::string &filePath) const
{
    json root;

    json arr = json::array();
    for (const auto &e : m_players) {
        json obj;
        obj["steamId"] = e.steamId;
        obj["name"]    = e.name;
        obj["role"]    = static_cast<int>(e.role);
        arr.push_back(obj);
    }
    root["players"] = arr;

    std::ofstream f(filePath);
    if (!f.is_open())
        return false;

    f << root.dump(2);
    return f.good();
}

// ---------------------------------------------------------------------------
// Internal upsert (no audit logging – used by file-load methods)
// ---------------------------------------------------------------------------

void UserRoleManager::upsertPlayer(const std::string &steamId,
                                    ServerPlayerRole role)
{
    auto it = std::find_if(m_players.begin(), m_players.end(),
                           [&](const PlayerEntry &e){ return e.steamId == steamId; });
    if (it != m_players.end())
        it->role = role;
    else
        m_players.push_back({steamId, "", role});
}

// ---------------------------------------------------------------------------
// Player management
// ---------------------------------------------------------------------------

void UserRoleManager::addPlayer(const PlayerEntry &entry,
                                 const std::string &actorId)
{
    auto it = std::find_if(m_players.begin(), m_players.end(),
                           [&](const PlayerEntry &e){ return e.steamId == entry.steamId; });
    if (it != m_players.end()) {
        *it = entry;
        logAudit(actorId,
                 "Updated player '" + entry.name + "' role to " + roleName(entry.role),
                 entry.steamId);
    } else {
        m_players.push_back(entry);
        logAudit(actorId,
                 "Added player '" + entry.name + "' with role " + roleName(entry.role),
                 entry.steamId);
    }
}

bool UserRoleManager::removePlayer(const std::string &steamId,
                                    const std::string &actorId)
{
    auto it = std::find_if(m_players.begin(), m_players.end(),
                           [&](const PlayerEntry &e){ return e.steamId == steamId; });
    if (it == m_players.end())
        return false;

    logAudit(actorId, "Removed player '" + it->name + "'", steamId);
    m_players.erase(it);
    return true;
}

bool UserRoleManager::setPlayerRole(const std::string &steamId,
                                     ServerPlayerRole role,
                                     const std::string &actorId)
{
    auto it = std::find_if(m_players.begin(), m_players.end(),
                           [&](const PlayerEntry &e){ return e.steamId == steamId; });
    if (it == m_players.end())
        return false;

    it->role = role;
    logAudit(actorId,
             "Changed role of '" + it->name + "' to " + roleName(role),
             steamId);
    return true;
}

std::optional<PlayerEntry> UserRoleManager::findPlayer(const std::string &steamId) const
{
    auto it = std::find_if(m_players.begin(), m_players.end(),
                           [&](const PlayerEntry &e){ return e.steamId == steamId; });
    if (it == m_players.end())
        return std::nullopt;
    return *it;
}

std::vector<PlayerEntry> UserRoleManager::getPlayersByRole(ServerPlayerRole role) const
{
    std::vector<PlayerEntry> result;
    for (const auto &e : m_players) {
        if (e.role == role)
            result.push_back(e);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Convenience queries
// ---------------------------------------------------------------------------

bool UserRoleManager::isAdmin(const std::string &steamId) const
{
    auto p = findPlayer(steamId);
    return p.has_value() && p->role == ServerPlayerRole::Admin;
}

bool UserRoleManager::isBanned(const std::string &steamId) const
{
    auto p = findPlayer(steamId);
    return p.has_value() && p->role == ServerPlayerRole::Banned;
}

bool UserRoleManager::isPermitted(const std::string &steamId) const
{
    auto p = findPlayer(steamId);
    if (!p.has_value())
        return false;
    return p->role == ServerPlayerRole::Whitelisted
        || p->role == ServerPlayerRole::Operator
        || p->role == ServerPlayerRole::Admin;
}

// ---------------------------------------------------------------------------
// Plain-text file I/O
// ---------------------------------------------------------------------------

bool UserRoleManager::loadPlainTextFile(const std::string &filePath,
                                         ServerPlayerRole role)
{
    std::ifstream f(filePath);
    if (!f.is_open())
        return false;

    std::string line;
    while (std::getline(f, line)) {
        // Strip carriage return
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        // Skip blank lines and comments (// or #)
        if (line.empty() || line[0] == '#')
            continue;
        if (line.size() >= 2 && line[0] == '/' && line[1] == '/')
            continue;
        // Steam ID may be followed by optional whitespace and a display name;
        // take only the first token.
        size_t sep = line.find_first_of(" \t");
        std::string id = (sep == std::string::npos) ? line : line.substr(0, sep);
        if (!id.empty())
            upsertPlayer(id, role);
    }
    return true;
}

bool UserRoleManager::savePlainTextFile(const std::string &filePath,
                                         ServerPlayerRole role) const
{
    std::ofstream f(filePath);
    if (!f.is_open())
        return false;

    switch (role) {
        case ServerPlayerRole::Admin:
            f << "// Server admins (one Steam64 ID per line)\n";
            break;
        case ServerPlayerRole::Operator:
            f << "// Server operators / moderators (one Steam64 ID per line)\n";
            break;
        case ServerPlayerRole::Whitelisted:
            f << "// Whitelisted / permitted players (one Steam64 ID per line)\n";
            break;
        case ServerPlayerRole::Banned:
            f << "// Banned players (one Steam64 ID per line)\n";
            break;
    }

    for (const auto &e : m_players) {
        if (e.role == role)
            f << e.steamId << "\n";
    }

    return f.good();
}

// ---------------------------------------------------------------------------
// XML file I/O  (7 Days to Die serveradmin.xml format)
// ---------------------------------------------------------------------------

bool UserRoleManager::loadXmlAdminFile(const std::string &filePath)
{
    std::ifstream f(filePath);
    if (!f.is_open())
        return false;

    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    // Extract the text between <tag>...</tag>.
    auto extractSection = [&](const std::string &tag) -> std::string {
        const std::string open  = "<" + tag + ">";
        const std::string close = "</" + tag + ">";
        size_t start = content.find(open);
        if (start == std::string::npos) return "";
        start += open.size();
        size_t end = content.find(close, start);
        if (end == std::string::npos) return "";
        return content.substr(start, end - start);
    };

    // Pull every steamID="..." value from a section string.
    auto extractIds = [](const std::string &section) -> std::vector<std::string> {
        std::vector<std::string> ids;
        const std::string key = "steamID=\"";
        size_t pos = 0;
        while ((pos = section.find(key, pos)) != std::string::npos) {
            pos += key.size();
            size_t end = section.find('"', pos);
            if (end == std::string::npos) break;
            std::string id = section.substr(pos, end - pos);
            if (!id.empty()) ids.push_back(id);
            pos = end + 1;
        }
        return ids;
    };

    // Section-to-role mapping matches 7DTD's serveradmin.xml layout.
    struct { const char *tag; ServerPlayerRole role; } sections[] = {
        { "admins",     ServerPlayerRole::Admin       },
        { "moderators", ServerPlayerRole::Operator    },
        { "whitelist",  ServerPlayerRole::Whitelisted },
        { "blacklist",  ServerPlayerRole::Banned      },
    };

    for (const auto &s : sections) {
        for (const auto &id : extractIds(extractSection(s.tag)))
            upsertPlayer(id, s.role);
    }

    return true;
}

bool UserRoleManager::saveXmlAdminFile(const std::string &filePath) const
{
    std::ofstream f(filePath);
    if (!f.is_open())
        return false;

    f << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    f << "<adminTools>\n";

    f << "  <admins>\n";
    for (const auto &e : m_players) {
        if (e.role == ServerPlayerRole::Admin)
            f << "    <admin steamID=\"" << e.steamId
              << "\" permission_level=\"0\"/>\n";
    }
    f << "  </admins>\n";

    f << "  <moderators>\n";
    for (const auto &e : m_players) {
        if (e.role == ServerPlayerRole::Operator)
            f << "    <moderator steamID=\"" << e.steamId
              << "\" permission_level=\"1000\"/>\n";
    }
    f << "  </moderators>\n";

    f << "  <whitelist>\n";
    for (const auto &e : m_players) {
        if (e.role == ServerPlayerRole::Whitelisted)
            f << "    <whitelist steamID=\"" << e.steamId << "\"/>\n";
    }
    f << "  </whitelist>\n";

    f << "  <blacklist>\n";
    for (const auto &e : m_players) {
        if (e.role == ServerPlayerRole::Banned)
            f << "    <blacklist steamID=\"" << e.steamId
              << "\" unbandate=\"\"/>\n";
    }
    f << "  </blacklist>\n";

    f << "</adminTools>\n";
    return f.good();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string UserRoleManager::roleName(ServerPlayerRole role)
{
    switch (role) {
        case ServerPlayerRole::Admin:       return "Admin";
        case ServerPlayerRole::Operator:    return "Operator";
        case ServerPlayerRole::Whitelisted: return "Whitelisted";
        case ServerPlayerRole::Banned:      return "Banned";
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

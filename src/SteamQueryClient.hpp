#pragma once

/**
 * @file SteamQueryClient.hpp
 * @brief Valve A2S (Steam Server Query) protocol client.
 *
 * Provides a lightweight, dependency-free implementation of the Steam A2S_INFO
 * query protocol, which lets SSA retrieve player counts and basic server info
 * from any Valve-compatible dedicated server without requiring RCON.  Useful
 * for games that either have no RCON support or where RCON credentials are
 * not configured.
 *
 * Protocol reference:
 *   https://developer.valvesoftware.com/wiki/Server_queries
 */

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <fcntl.h>
#endif

#include <string>
#include <optional>
#include <cstdint>

/**
 * @brief Thin Valve A2S_INFO query client.
 *
 * All methods are static; the class acts as a namespace.  There is no
 * persistent state – each call opens a UDP socket, sends the query, waits
 * for the response, and closes the socket.
 */
class SteamQueryClient
{
public:
    /** Information returned by an A2S_INFO query. */
    struct ServerInfo {
        std::string serverName;  ///< Human-readable server name
        std::string mapName;     ///< Current map / world name
        std::string gameName;    ///< Game / folder name as reported by the server
        int         players    = -1; ///< Current human player count (-1 = query failed)
        int         maxPlayers =  0; ///< Maximum player slots (0 = unknown)
        int         bots       =  0; ///< Number of bot players
        bool        password   = false; ///< True if the server is password-protected
    };

    /**
     * @brief Query a server for basic info using the A2S_INFO protocol.
     *
     * Handles the optional server-side challenge exchange automatically.
     *
     * @param host       Hostname or dotted-decimal IP of the game server.
     * @param queryPort  UDP port used for Steam server queries (often the
     *                   game port, e.g. 27015).
     * @param timeoutMs  Maximum time to wait for a response (milliseconds).
     * @return Populated ServerInfo on success, or an empty optional when the
     *         server is unreachable, returns malformed data, or timeoutMs
     *         elapses.
     */
    static std::optional<ServerInfo> queryInfo(
        const std::string &host,
        uint16_t           queryPort,
        int                timeoutMs = 2000);

private:
    SteamQueryClient() = delete;

    // Low-level socket helpers
    static bool sendTo(int sock,
                       const struct sockaddr *addr, socklen_t addrLen,
                       const uint8_t *data, int len);

    static int recvFrom(int sock,
                        uint8_t *buf, int bufLen,
                        int timeoutMs);

    // Parse a null-terminated string from a byte buffer.
    // Advances `pos` past the null terminator.
    // Returns an empty string and does NOT advance pos if data is malformed.
    static std::string readCString(const uint8_t *buf, int bufLen, int &pos);
};

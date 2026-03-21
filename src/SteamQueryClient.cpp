#include "SteamQueryClient.hpp"

#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Platform-specific socket portability layer
// ---------------------------------------------------------------------------

#ifdef _WIN32
using SockLen = int;
using SocketFd = SOCKET;
static constexpr SocketFd kInvalidSocket = INVALID_SOCKET;
static void closeSocket(SocketFd s) { ::closesocket(s); }
#else
using SockLen = socklen_t;
using SocketFd = int;
static constexpr SocketFd kInvalidSocket = -1;
static void closeSocket(SocketFd s) { ::close(s); }
#endif

// ---------------------------------------------------------------------------
// A2S request constants
// ---------------------------------------------------------------------------

// A2S_INFO request payload (null-terminated "Source Engine Query" string
// prefixed with the 4-byte 0xFF header and the 0x54 request type byte).
static const uint8_t kA2SInfoRequest[] = {
    0xFF, 0xFF, 0xFF, 0xFF,
    0x54,
    'S','o','u','r','c','e',' ','E','n','g','i','n','e',' ','Q','u','e','r','y',
    0x00
};
static constexpr int kA2SInfoRequestLen = static_cast<int>(sizeof(kA2SInfoRequest));

// Packet header bytes
static constexpr uint8_t kHeaderA2SInfo      = 0x49; // A2S_INFO response
static constexpr uint8_t kHeaderChallenge     = 0x41; // S2C_CHALLENGE

// ---------------------------------------------------------------------------
// SteamQueryClient::readCString
// ---------------------------------------------------------------------------

std::string SteamQueryClient::readCString(const uint8_t *buf, int bufLen, int &pos)
{
    if (pos < 0 || pos >= bufLen)
        return {};

    const char *start = reinterpret_cast<const char *>(buf + pos);
    int remaining = bufLen - pos;

    // Scan for null terminator
    int len = 0;
    while (len < remaining && buf[pos + len] != 0x00)
        ++len;

    if (pos + len >= bufLen) {
        // No null terminator found within the buffer – malformed
        return {};
    }

    std::string result(start, len);
    pos += len + 1; // +1 to skip the null terminator
    return result;
}

// ---------------------------------------------------------------------------
// SteamQueryClient::sendTo
// ---------------------------------------------------------------------------

bool SteamQueryClient::sendTo(int sock,
                               const struct sockaddr *addr, socklen_t addrLen,
                               const uint8_t *data, int len)
{
    int sent = static_cast<int>(
        ::sendto(sock,
                 reinterpret_cast<const char *>(data),
                 len,
                 0,
                 addr, static_cast<SockLen>(addrLen)));
    return (sent == len);
}

// ---------------------------------------------------------------------------
// SteamQueryClient::recvFrom
// ---------------------------------------------------------------------------

int SteamQueryClient::recvFrom(int sock, uint8_t *buf, int bufLen, int timeoutMs)
{
#ifdef _WIN32
    // Windows: use setsockopt with SO_RCVTIMEO
    DWORD tv = static_cast<DWORD>(timeoutMs);
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char *>(&tv), sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec  = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    int received = static_cast<int>(
        ::recv(sock,
               reinterpret_cast<char *>(buf),
               bufLen, 0));
    return received; // -1 on timeout / error, > 0 on success
}

// ---------------------------------------------------------------------------
// SteamQueryClient::queryInfo
// ---------------------------------------------------------------------------

std::optional<SteamQueryClient::ServerInfo>
SteamQueryClient::queryInfo(const std::string &host,
                             uint16_t           queryPort,
                             int                timeoutMs)
{
    if (host.empty() || queryPort == 0)
        return {};

#ifdef _WIN32
    // One-time WSA initialisation (safe to call multiple times)
    static bool wsaInited = false;
    if (!wsaInited) {
        WSADATA wsa{};
        if (::WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            return {};
        wsaInited = true;
    }
#endif

    // ---- Resolve the server address ----
    struct addrinfo hints{};
    hints.ai_family   = AF_INET;      // IPv4 only
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    char portStr[16];
    std::snprintf(portStr, sizeof(portStr), "%u", static_cast<unsigned>(queryPort));

    struct addrinfo *res = nullptr;
    if (::getaddrinfo(host.c_str(), portStr, &hints, &res) != 0 || !res)
        return {};

    struct sockaddr_in serverAddr;
    std::memcpy(&serverAddr, res->ai_addr, res->ai_addrlen);
    ::freeaddrinfo(res);

    // ---- Create UDP socket ----
    SocketFd sock = static_cast<SocketFd>(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (sock == kInvalidSocket)
        return {};

    // ---- Send initial A2S_INFO request ----
    if (!sendTo(static_cast<int>(sock),
                reinterpret_cast<const struct sockaddr *>(&serverAddr),
                sizeof(serverAddr),
                kA2SInfoRequest, kA2SInfoRequestLen)) {
        closeSocket(sock);
        return {};
    }

    // ---- Receive response (may be a challenge or directly info) ----
    static constexpr int kBufSize = 1400;
    uint8_t buf[kBufSize];

    int received = recvFrom(static_cast<int>(sock), buf, kBufSize, timeoutMs);
    if (received < 5) { // minimum: 4-byte header + 1-byte type
        closeSocket(sock);
        return {};
    }

    // Validate the 4-byte all-0xFF header
    if (buf[0] != 0xFF || buf[1] != 0xFF || buf[2] != 0xFF || buf[3] != 0xFF) {
        closeSocket(sock);
        return {};
    }

    // ---- Handle challenge response ----
    if (buf[4] == kHeaderChallenge) {
        // Server sent a 4-byte challenge value; we must re-send the request
        // with the challenge appended.
        if (received < 9) { // 4-byte header + type + 4-byte challenge
            closeSocket(sock);
            return {};
        }

        uint8_t challengedRequest[kA2SInfoRequestLen + 4];
        std::memcpy(challengedRequest, kA2SInfoRequest, kA2SInfoRequestLen);
        std::memcpy(challengedRequest + kA2SInfoRequestLen, buf + 5, 4);

        if (!sendTo(static_cast<int>(sock),
                    reinterpret_cast<const struct sockaddr *>(&serverAddr),
                    sizeof(serverAddr),
                    challengedRequest,
                    kA2SInfoRequestLen + 4)) {
            closeSocket(sock);
            return {};
        }

        received = recvFrom(static_cast<int>(sock), buf, kBufSize, timeoutMs);
        if (received < 5 ||
            buf[0] != 0xFF || buf[1] != 0xFF || buf[2] != 0xFF || buf[3] != 0xFF) {
            closeSocket(sock);
            return {};
        }
    }

    closeSocket(sock);

    // ---- Parse A2S_INFO response ----
    if (buf[4] != kHeaderA2SInfo)
        return {};

    int pos = 5; // skip 4-byte header + 1-byte type

    // Protocol version (1 byte)
    if (pos >= received) return {};
    ++pos; // skip protocol

    ServerInfo info;

    // Server name (null-terminated string)
    info.serverName = readCString(buf, received, pos);
    if (pos >= received) return {};

    // Map name (null-terminated string)
    info.mapName = readCString(buf, received, pos);
    if (pos >= received) return {};

    // Game folder (null-terminated string)
    info.gameName = readCString(buf, received, pos);
    if (pos >= received) return {};

    // Game description (null-terminated string) – skip
    readCString(buf, received, pos);
    if (pos + 2 > received) return {};

    // AppID (2 bytes, little-endian) – skip
    pos += 2;
    if (pos + 4 > received) return {};

    // Players (1 byte)
    info.players = static_cast<int>(buf[pos++]);

    // Max players (1 byte)
    info.maxPlayers = static_cast<int>(buf[pos++]);

    // Bots (1 byte)
    info.bots = static_cast<int>(buf[pos++]);

    // Server type (1 byte) – skip
    ++pos;
    if (pos >= received) return {};

    // OS (1 byte) – skip
    ++pos;
    if (pos >= received) return {};

    // Password flag (1 byte)
    info.password = (buf[pos++] != 0);

    return info;
}

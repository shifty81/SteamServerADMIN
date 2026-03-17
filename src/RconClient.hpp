#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using SocketType = SOCKET;
constexpr SocketType kInvalidSocket = INVALID_SOCKET;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <poll.h>
using SocketType = int;
constexpr SocketType kInvalidSocket = -1;
#endif

/**
 * @brief Implements the Valve Source RCON protocol, with optional plain-telnet
 *        mode for games like 7 Days to Die that expose a telnet console instead
 *        of the binary Source RCON protocol.
 *
 * https://developer.valvesoftware.com/wiki/Source_RCON_Protocol
 */
class RconClient {
public:
    RconClient();
    ~RconClient();

    /** Enable plain-telnet mode (for 7DTD and similar games). Must be called
     *  before connectToServer(). */
    void setTelnetMode(bool enabled) { m_telnetMode = enabled; }
    bool isTelnetMode() const { return m_telnetMode; }

    bool connectToServer(const std::string &host, int port, const std::string &password,
                         int timeoutMs = 5000);
    void disconnect();
    bool isConnected() const;

    /**
     * @brief Send an RCON command and return the response.
     * @return Response string, or empty string on error.
     */
    std::string sendCommand(const std::string &command, int timeoutMs = 5000);

    /** Callback invoked when an error occurs. */
    std::function<void(const std::string &message)> onError;

private:
    // RCON packet types
    static constexpr int SERVERDATA_AUTH           = 3;
    static constexpr int SERVERDATA_AUTH_RESPONSE  = 2;
    static constexpr int SERVERDATA_EXECCOMMAND    = 2;
    static constexpr int SERVERDATA_RESPONSE_VALUE = 0;
    static constexpr int MAX_RCON_PACKET_SIZE      = 4110;

    struct Packet {
        int id;
        int type;
        std::vector<uint8_t> body;
    };

    bool sendPacket(const Packet &pkt);
    bool recvPacket(Packet &pkt, int timeoutMs);
    std::vector<uint8_t> buildRawPacket(const Packet &pkt) const;

    bool waitForData(int timeoutMs);
    int socketSend(const void *data, int len);
    int socketRecv(void *buf, int len);
    void emitError(const std::string &msg);

    // Telnet-mode helpers (7DTD)
    bool m_telnetMode = false;
    bool telnetAuth(const std::string &password, int timeoutMs);
    std::string telnetSendCommand(const std::string &command, int timeoutMs);
    std::string readAvailable(int timeoutMs);

    SocketType m_socket = kInvalidSocket;
    int m_nextId = 1;
    bool m_connected = false;

#ifdef _WIN32
    static bool s_wsaInitialized;
    static void ensureWSA();
#endif
};

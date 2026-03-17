#include "RconClient.hpp"

#include <cstring>
#include <iostream>

#ifdef _WIN32
bool RconClient::s_wsaInitialized = false;
void RconClient::ensureWSA()
{
    if (!s_wsaInitialized) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        s_wsaInitialized = true;
    }
}
#endif

// ---------------------------------------------------------------------------
// RCON packet layout (little-endian):
//   int32  size   (size of the rest: id + type + body + 2 null terminators)
//   int32  id
//   int32  type
//   char[] body   (null-terminated)
//   char   empty  (null terminator)
// ---------------------------------------------------------------------------

RconClient::RconClient()
{
#ifdef _WIN32
    ensureWSA();
#endif
}

RconClient::~RconClient()
{
    disconnect();
}

void RconClient::emitError(const std::string &msg)
{
    if (onError) onError(msg);
}

bool RconClient::waitForData(int timeoutMs)
{
#ifdef _WIN32
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(m_socket, &readSet);
    timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    return select(0, &readSet, nullptr, nullptr, &tv) > 0;
#else
    struct pollfd pfd;
    pfd.fd = m_socket;
    pfd.events = POLLIN;
    return poll(&pfd, 1, timeoutMs) > 0;
#endif
}

int RconClient::socketSend(const void *data, int len)
{
#ifdef _WIN32
    return ::send(m_socket, static_cast<const char*>(data), len, 0);
#else
    return static_cast<int>(::send(m_socket, data, len, 0));
#endif
}

int RconClient::socketRecv(void *buf, int len)
{
#ifdef _WIN32
    return ::recv(m_socket, static_cast<char*>(buf), len, 0);
#else
    return static_cast<int>(::recv(m_socket, buf, len, 0));
#endif
}

bool RconClient::connectToServer(const std::string &host, int port, const std::string &password,
                                 int timeoutMs)
{
    disconnect();

    m_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_socket == kInvalidSocket) {
        emitError("Failed to create socket");
        return false;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        // Try hostname resolution
        struct addrinfo hints{}, *result = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result) {
            emitError("Cannot resolve host: " + host);
            disconnect();
            return false;
        }
        addr.sin_addr = reinterpret_cast<struct sockaddr_in*>(result->ai_addr)->sin_addr;
        freeaddrinfo(result);
    }

    // Set receive/send timeout for the socket
#ifdef _WIN32
    DWORD tv = timeoutMs;
    setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

    if (::connect(m_socket, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        emitError("Connection failed to " + host + ":" + std::to_string(port));
        disconnect();
        return false;
    }

    m_connected = true;

    // Telnet mode: use plain-text password authentication (7DTD, etc.)
    if (m_telnetMode)
        return telnetAuth(password, timeoutMs);

    // Send auth packet
    Packet auth;
    auth.id   = m_nextId++;
    auth.type = SERVERDATA_AUTH;
    auth.body.assign(password.begin(), password.end());

    if (!sendPacket(auth))
        return false;

    // Read auth response
    Packet resp;
    if (!recvPacket(resp, timeoutMs)) {
        emitError("No auth response from server");
        return false;
    }

    // id == -1 means wrong password
    if (resp.id == -1) {
        emitError("RCON authentication failed: wrong password");
        disconnect();
        return false;
    }

    return true;
}

void RconClient::disconnect()
{
    if (m_socket != kInvalidSocket) {
#ifdef _WIN32
        closesocket(m_socket);
#else
        ::close(m_socket);
#endif
        m_socket = kInvalidSocket;
    }
    m_connected = false;
}

bool RconClient::isConnected() const
{
    return m_connected && m_socket != kInvalidSocket;
}

std::string RconClient::sendCommand(const std::string &command, int timeoutMs)
{
    if (!isConnected()) {
        emitError("Not connected to RCON");
        return {};
    }

    if (m_telnetMode)
        return telnetSendCommand(command, timeoutMs);

    Packet cmd;
    cmd.id   = m_nextId++;
    cmd.type = SERVERDATA_EXECCOMMAND;
    cmd.body.assign(command.begin(), command.end());

    if (!sendPacket(cmd))
        return {};

    // Read response (may be split across multiple packets)
    std::string result;
    Packet resp;
    while (recvPacket(resp, timeoutMs)) {
        result.append(resp.body.begin(), resp.body.end());
        // Check if more data is pending
        if (!waitForData(100))
            break;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

std::vector<uint8_t> RconClient::buildRawPacket(const Packet &pkt) const
{
    int bodyLen = static_cast<int>(pkt.body.size());
    int size = 4 + 4 + bodyLen + 2;  // id + type + body + 2 null terminators

    std::vector<uint8_t> raw(4 + size, 0);

    auto write32 = [&](int offset, int32_t val) {
        raw[offset]   = static_cast<uint8_t>(val & 0xFF);
        raw[offset+1] = static_cast<uint8_t>((val >>  8) & 0xFF);
        raw[offset+2] = static_cast<uint8_t>((val >> 16) & 0xFF);
        raw[offset+3] = static_cast<uint8_t>((val >> 24) & 0xFF);
    };

    write32(0, size);
    write32(4, pkt.id);
    write32(8, pkt.type);
    std::memcpy(raw.data() + 12, pkt.body.data(), bodyLen);
    raw[12 + bodyLen]     = 0;
    raw[12 + bodyLen + 1] = 0;
    return raw;
}

bool RconClient::sendPacket(const Packet &pkt)
{
    std::vector<uint8_t> raw = buildRawPacket(pkt);
    int sent = socketSend(raw.data(), static_cast<int>(raw.size()));
    if (sent != static_cast<int>(raw.size())) {
        emitError("Failed to send packet");
        return false;
    }
    return true;
}

bool RconClient::recvPacket(Packet &pkt, int timeoutMs)
{
    // Read 4-byte size field first
    if (!waitForData(timeoutMs))
        return false;

    uint8_t sizeBuf[4];
    int received = 0;
    while (received < 4) {
        int n = socketRecv(sizeBuf + received, 4 - received);
        if (n <= 0) return false;
        received += n;
    }

    auto read32 = [](const uint8_t *b) -> int32_t {
        return static_cast<int32_t>(
            b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
    };

    int packetSize = read32(sizeBuf);
    if (packetSize < 10 || packetSize > MAX_RCON_PACKET_SIZE) {
        emitError("Invalid RCON packet size: " + std::to_string(packetSize));
        return false;
    }

    std::vector<uint8_t> data(packetSize);
    received = 0;
    while (received < packetSize) {
        if (!waitForData(timeoutMs))
            return false;
        int n = socketRecv(data.data() + received, packetSize - received);
        if (n <= 0) return false;
        received += n;
    }

    pkt.id   = read32(data.data());
    pkt.type = read32(data.data() + 4);
    int bodyLen = packetSize - 4 - 4 - 2;
    if (bodyLen > 0) {
        pkt.body.assign(data.begin() + 8, data.begin() + 8 + bodyLen);
    } else {
        pkt.body.clear();
    }

    return true;
}

// ---------------------------------------------------------------------------
// Telnet-mode helpers (7 Days to Die, etc.)
// ---------------------------------------------------------------------------

std::string RconClient::readAvailable(int timeoutMs)
{
    std::string result;
    char buf[1024];
    while (waitForData(timeoutMs)) {
        int n = socketRecv(buf, sizeof(buf) - 1);
        if (n <= 0) break;
        result.append(buf, n);
        // After first chunk, use a short timeout for any remaining data
        timeoutMs = 200;
    }
    return result;
}

bool RconClient::telnetAuth(const std::string &password, int timeoutMs)
{
    // Read welcome / password prompt from the telnet server
    std::string welcome = readAvailable(timeoutMs);

    // If server didn't send a prompt the connection may still be valid
    // (some 7DTD configs have TelnetPassword empty → no prompt).
    if (password.empty())
        return true;

    // Send password followed by newline
    std::string passLine = password + "\n";
    if (socketSend(passLine.c_str(), static_cast<int>(passLine.size())) <= 0) {
        emitError("Failed to send telnet password");
        disconnect();
        return false;
    }

    // Read auth response
    std::string authResp = readAvailable(timeoutMs);

    // Accept common success indicators from 7DTD / generic telnet servers
    if (authResp.find("successful") != std::string::npos ||
        authResp.find("Logon") != std::string::npos ||
        authResp.find("authenticated") != std::string::npos ||
        authResp.find("Welcome") != std::string::npos) {
        return true;
    }

    // If the response contains an explicit failure keyword, reject
    if (authResp.find("incorrect") != std::string::npos ||
        authResp.find("denied") != std::string::npos) {
        emitError("Telnet authentication failed: wrong password");
        disconnect();
        return false;
    }

    // No clear indicator – assume success (the server may not echo anything)
    return true;
}

std::string RconClient::telnetSendCommand(const std::string &command, int timeoutMs)
{
    std::string cmdLine = command + "\n";
    if (socketSend(cmdLine.c_str(), static_cast<int>(cmdLine.size())) <= 0) {
        emitError("Failed to send telnet command");
        m_connected = false;
        return {};
    }

    return readAvailable(timeoutMs);
}

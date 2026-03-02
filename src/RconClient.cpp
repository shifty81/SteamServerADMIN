#include "RconClient.hpp"

#include <QDataStream>
#include <QDebug>

// ---------------------------------------------------------------------------
// RCON packet layout (little-endian):
//   int32  size   (size of the rest: id + type + body + 2 null terminators)
//   int32  id
//   int32  type
//   char[] body   (null-terminated)
//   char   empty  (null terminator)
// ---------------------------------------------------------------------------

RconClient::RconClient(QObject *parent) : QObject(parent)
{
    m_socket = new QTcpSocket(this);
}

RconClient::~RconClient()
{
    disconnect();
}

bool RconClient::connect(const QString &host, int port, const QString &password,
                         int timeoutMs)
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->disconnectFromHost();

    m_socket->connectToHost(host, static_cast<quint16>(port));
    if (!m_socket->waitForConnected(timeoutMs)) {
        emit errorOccurred(tr("Connection failed: %1").arg(m_socket->errorString()));
        return false;
    }

    // Send auth packet
    Packet auth;
    auth.id   = m_nextId++;
    auth.type = SERVERDATA_AUTH;
    auth.body = password.toUtf8();

    if (!sendPacket(auth))
        return false;

    // Read auth response
    Packet resp;
    if (!recvPacket(resp, timeoutMs)) {
        emit errorOccurred(tr("No auth response from server"));
        return false;
    }

    // id == -1 means wrong password
    if (resp.id == -1) {
        emit errorOccurred(tr("RCON authentication failed: wrong password"));
        m_socket->disconnectFromHost();
        return false;
    }

    return true;
}

void RconClient::disconnect()
{
    if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
        m_socket->waitForDisconnected(1000);
    }
}

bool RconClient::isConnected() const
{
    return m_socket &&
           m_socket->state() == QAbstractSocket::ConnectedState;
}

QString RconClient::sendCommand(const QString &command, int timeoutMs)
{
    if (!isConnected()) {
        emit errorOccurred(tr("Not connected to RCON"));
        return {};
    }

    Packet cmd;
    cmd.id   = m_nextId++;
    cmd.type = SERVERDATA_EXECCOMMAND;
    cmd.body = command.toUtf8();

    if (!sendPacket(cmd))
        return {};

    // Read response (may be split across multiple packets)
    QString result;
    Packet resp;
    while (recvPacket(resp, timeoutMs)) {
        result += QString::fromUtf8(resp.body);
        // A single-fragment response ends when there is no more data ready
        if (!m_socket->waitForReadyRead(100))
            break;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

QByteArray RconClient::buildRawPacket(const Packet &pkt) const
{
    QByteArray body = pkt.body;
    // body + null terminator + empty string null terminator
    int size = 4 + 4 + body.size() + 2;

    QByteArray raw;
    raw.resize(4 + size);

    auto write32 = [&](int offset, qint32 val) {
        raw[offset]   = static_cast<char>(val & 0xFF);
        raw[offset+1] = static_cast<char>((val >>  8) & 0xFF);
        raw[offset+2] = static_cast<char>((val >> 16) & 0xFF);
        raw[offset+3] = static_cast<char>((val >> 24) & 0xFF);
    };

    write32(0, size);
    write32(4, pkt.id);
    write32(8, pkt.type);
    raw.replace(12, body.size(), body);
    raw[12 + body.size()]     = '\0';
    raw[12 + body.size() + 1] = '\0';
    return raw;
}

bool RconClient::sendPacket(const Packet &pkt)
{
    QByteArray raw = buildRawPacket(pkt);
    qint64 written = m_socket->write(raw);
    if (written != raw.size()) {
        emit errorOccurred(tr("Failed to send packet"));
        return false;
    }
    return m_socket->waitForBytesWritten(3000);
}

bool RconClient::recvPacket(Packet &pkt, int timeoutMs)
{
    // Read 4-byte size field first
    if (!m_socket->waitForReadyRead(timeoutMs))
        return false;

    while (m_socket->bytesAvailable() < 4) {
        if (!m_socket->waitForReadyRead(timeoutMs))
            return false;
    }

    auto read32 = [](const QByteArray &b, int offset) -> qint32 {
        return static_cast<qint32>(
            (static_cast<unsigned char>(b[offset]))        |
            (static_cast<unsigned char>(b[offset+1]) <<  8) |
            (static_cast<unsigned char>(b[offset+2]) << 16) |
            (static_cast<unsigned char>(b[offset+3]) << 24));
    };

    QByteArray sizeBuf = m_socket->read(4);
    int packetSize = read32(sizeBuf, 0);

    if (packetSize < 10 || packetSize > MAX_RCON_PACKET_SIZE) {
        emit errorOccurred(tr("Invalid RCON packet size: %1").arg(packetSize));
        return false;
    }

    while (m_socket->bytesAvailable() < packetSize) {
        if (!m_socket->waitForReadyRead(timeoutMs))
            return false;
    }

    QByteArray data = m_socket->read(packetSize);
    pkt.id   = read32(data, 0);
    pkt.type = read32(data, 4);
    // body is null-terminated, starts at offset 8
    int bodyLen = packetSize - 4 - 4 - 2; // subtract id, type, two nulls
    if (bodyLen > 0)
        pkt.body = data.mid(8, bodyLen);
    else
        pkt.body.clear();

    return true;
}

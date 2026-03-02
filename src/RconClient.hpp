#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QByteArray>
#include <QString>

/**
 * @brief Implements the Valve Source RCON protocol.
 *
 * https://developer.valvesoftware.com/wiki/Source_RCON_Protocol
 */
class RconClient : public QObject {
    Q_OBJECT
public:
    explicit RconClient(QObject *parent = nullptr);
    ~RconClient() override;

    bool connect(const QString &host, int port, const QString &password,
                 int timeoutMs = 5000);
    void disconnect();
    bool isConnected() const;

    /**
     * @brief Send an RCON command and return the response.
     * @return Response string, or empty string on error.
     */
    QString sendCommand(const QString &command, int timeoutMs = 5000);

signals:
    void errorOccurred(const QString &message);

private:
    // RCON packet types
    static constexpr int SERVERDATA_AUTH           = 3;
    static constexpr int SERVERDATA_AUTH_RESPONSE  = 2;
    static constexpr int SERVERDATA_EXECCOMMAND    = 2;
    static constexpr int SERVERDATA_RESPONSE_VALUE = 0;
    // Source RCON spec: max body is 4096 bytes; total packet ≤ 4110 bytes.
    static constexpr int MAX_RCON_PACKET_SIZE      = 4110;

    struct Packet {
        int id;
        int type;
        QByteArray body;
    };

    bool sendPacket(const Packet &pkt);
    bool recvPacket(Packet &pkt, int timeoutMs);
    QByteArray buildRawPacket(const Packet &pkt) const;

    QTcpSocket *m_socket = nullptr;
    int m_nextId = 1;
};

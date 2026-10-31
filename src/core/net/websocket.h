#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QUrl>

namespace ex::net {

// Minimal WebSocket client (RFC 6455) over QTcpSocket.
// Needed for CDP (Chrome DevTools Protocol), later for Nexus.
// Supports: handshake, text frames, message fragmentation,
// ping/pong, masking of outgoing frames (mandatory for a client).
class WebSocket : public QObject {
    Q_OBJECT
public:
    enum class State {
        Disconnected,
        Connecting,   // TCP + HTTP Upgrade
        Open,
        Closing,
    };

    explicit WebSocket(QObject* parent = nullptr);
    ~WebSocket() override;

    void connectToUrl(const QUrl& url);   // ws://host:port/path
    void close();
    State state() const { return m_state; }
    bool isOpen() const { return m_state == State::Open; }

    // Send a text message (one frame, or several when the payload is large
    // size). Returns false when the socket is not open.
    bool sendTextMessage(const QString& message);

    // Parse ONE frame from the buffer (server frames are unmasked, client
    // ones masked). Public for unit tests; ok=false means not enough data;
    // a non-empty err is a protocol error.
    struct Frame {
        quint8 opcode = 0;
        bool fin = false;
        QByteArray payload;
    };
    static bool parseFrame(const QByteArray& buffer, Frame& out, int& consumed, QString& err);

signals:
    void connected();
    void textMessageReceived(const QString& message);
    void disconnected();
    void errorOccurred(const QString& error);

private slots:
    void onSocketConnected();
    void onSocketReadyRead();
    void onSocketDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);

private:
    void sendFrame(quint8 opcode, const QByteArray& payload);
    void handleControlFrame(const Frame& f);

    QTcpSocket m_socket;
    State m_state = State::Disconnected;
    QByteArray m_handshakeResponse;
    QByteArray m_rxBuffer;

    // Assembling a fragmented message
    bool m_fragmenting = false;
    quint8 m_fragmentOpcode = 0;
    QByteArray m_fragmentBuffer;

    QString m_path;
    QString m_host;
    quint16 m_port = 0;
};

} // namespace ex::net

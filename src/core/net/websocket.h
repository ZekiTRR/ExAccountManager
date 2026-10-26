#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QUrl>

namespace ex::net {

// Минимальный WebSocket-клиент (RFC 6455) поверх QTcpSocket.
// Нужен для CDP (Chrome DevTools Protocol) и в будущем для Nexus.
// Поддержка: рукопожатие, текстовые фреймы, фрагментация сообщений,
// ping/pong, маскирование исходящих фреймов (обязательно для клиента).
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

    // Отправка текстового сообщения (один или несколько фреймов при большом
    // размере). Возвращает false, если сокет не открыт.
    bool sendTextMessage(const QString& message);

    // Разбор ОДНОГО кадра из буфера (серверные кадры без маски, клиентские
    // с маской). Публично — для юнит-тестов. ok=false — данных мало;
    // err непустой — протоколная ошибка.
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

    // Сборка фрагментированного сообщения
    bool m_fragmenting = false;
    quint8 m_fragmentOpcode = 0;
    QByteArray m_fragmentBuffer;

    QString m_path;
    QString m_host;
    quint16 m_port = 0;
};

} // namespace ex::net

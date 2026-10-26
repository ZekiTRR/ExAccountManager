#include "core/net/websocket.h"

#include "core/crypto/sodium.h"
#include "core/util/log.h"

#include <QCryptographicHash>

namespace ex::net {

namespace {
constexpr quint8 kOpCont = 0x0;
constexpr quint8 kOpText = 0x1;
constexpr quint8 kOpBinary = 0x2;
constexpr quint8 kOpClose = 0x8;
constexpr quint8 kOpPing = 0x9;
constexpr quint8 kOpPong = 0xA;
} // namespace

WebSocket::WebSocket(QObject* parent)
    : QObject(parent)
{
    connect(&m_socket, &QTcpSocket::connected, this, &WebSocket::onSocketConnected);
    connect(&m_socket, &QTcpSocket::readyRead, this, &WebSocket::onSocketReadyRead);
    connect(&m_socket, &QTcpSocket::disconnected, this, &WebSocket::onSocketDisconnected);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(&m_socket, &QTcpSocket::errorOccurred, this, &WebSocket::onSocketError);
#else
    connect(&m_socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
            this, &WebSocket::onSocketError);
#endif
}

WebSocket::~WebSocket()
{
    close();
}

void WebSocket::connectToUrl(const QUrl& url)
{
    if (m_state != State::Disconnected)
        close();

    m_host = url.host();
    m_port = static_cast<quint16>(url.port(80));
    m_path = url.path().isEmpty() ? QStringLiteral("/") : url.path();
    if (url.hasQuery())
        m_path += QLatin1Char('?') + url.query();

    m_state = State::Connecting;
    m_handshakeResponse.clear();
    m_rxBuffer.clear();
    m_fragmenting = false;
    m_fragmentBuffer.clear();

    m_socket.connectToHost(m_host, m_port);
}

void WebSocket::close()
{
    if (m_state == State::Disconnected)
        return;
    if (m_state == State::Open) {
        // Вежливое закрытие: opcode close
        sendFrame(kOpClose, QByteArray());
        m_state = State::Closing;
        m_socket.flush();
    }
    m_socket.abort(); // CDP-сервер не требует graceful; надёжнее разорвать
    m_state = State::Disconnected;
    emit disconnected();
}

void WebSocket::onSocketConnected()
{
    // HTTP Upgrade рукопожатие
    unsigned char raw[16];
    ex::crypto::randomBytes(raw, sizeof raw);
    const QString key = QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(raw), sizeof raw).toBase64());

    const QString req = QStringLiteral(
        "GET %1 HTTP/1.1\r\n"
        "Host: %2:%3\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %4\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n")
        .arg(m_path, m_host).arg(m_port).arg(key);
    // ВАЖНО: не предлагаем permessage-deflate — сервер может согласовать
    // сжатие, а наш парсер не умеет разжимать payload.
    m_socket.write(req.toUtf8());
}

void WebSocket::onSocketReadyRead()
{
    m_rxBuffer += m_socket.readAll();

    if (m_state == State::Connecting) {
        // Ждём полный HTTP-ответ (до \r\n\r\n)
        const int headerEnd = m_rxBuffer.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        const QByteArray header = m_rxBuffer.left(headerEnd);
        m_rxBuffer.remove(0, headerEnd + 4);

        if (!header.startsWith("HTTP/1.1 101")) {
            m_state = State::Disconnected;
            m_socket.abort();
            emit errorOccurred(QStringLiteral("handshake-failed: %1")
                                   .arg(QString::fromLatin1(header.left(60))));
            emit disconnected();
            return;
        }
        m_state = State::Open;
        emit connected();
        // Остаток буфера — уже кадры, падаем дальше
    }

    // Разбор кадров
    while (true) {
        Frame f;
        int consumed = 0;
        QString err;
        if (!parseFrame(m_rxBuffer, f, consumed, err)) {
            if (!err.isEmpty()) {
                EXLOG_WARN("ws") << "frame parse error:" << err;
                close();
            }
            return; // ждём ещё данных
        }
        m_rxBuffer.remove(0, consumed);

        switch (f.opcode) {
        case kOpText:
        case kOpBinary: {
            if (f.fin && !m_fragmenting) {
                // Обычное одиночное сообщение
                if (f.opcode == kOpText)
                    emit textMessageReceived(QString::fromUtf8(f.payload));
                break;
            }
            // Начало фрагментированного сообщения
            m_fragmenting = true;
            m_fragmentOpcode = f.opcode;
            m_fragmentBuffer += f.payload;
            break;
        }
        case kOpCont: {
            if (!m_fragmenting) {
                EXLOG_WARN("ws") << "continuation frame without start";
                close();
                return;
            }
            m_fragmentBuffer += f.payload;
            if (f.fin) {
                const QString msg = QString::fromUtf8(m_fragmentBuffer);
                m_fragmenting = false;
                m_fragmentBuffer.clear();
                if (m_fragmentOpcode == kOpText)
                    emit textMessageReceived(msg);
                else
                    emit textMessageReceived(QString::fromUtf8(m_fragmentBuffer)); // binary как текст для CDP не приходит
            }
            break;
        }
        default:
            handleControlFrame(f);
            if (f.opcode == kOpClose) {
                close();
                return;
            }
            break;
        }
    }
}

bool WebSocket::parseFrame(const QByteArray& buffer, Frame& out, int& consumed, QString& err)
{
    if (buffer.size() < 2)
        return false;
    const quint8 b0 = static_cast<quint8>(buffer[0]);
    const quint8 b1 = static_cast<quint8>(buffer[1]);
    out.fin = (b0 & 0x80) != 0;
    out.opcode = b0 & 0x0F;
    const bool masked = (b1 & 0x80) != 0;
    quint64 len = b1 & 0x7F;

    int offset = 2;
    if (len == 126) {
        if (buffer.size() < offset + 2)
            return false;
        len = (static_cast<quint64>(static_cast<quint8>(buffer[offset])) << 8)
              | static_cast<quint8>(buffer[offset + 1]);
        offset += 2;
    } else if (len == 127) {
        if (buffer.size() < offset + 8)
            return false;
        len = 0;
        for (int i = 0; i < 8; ++i)
            len = (len << 8) | static_cast<quint8>(buffer[offset + i]);
        offset += 8;
    }
    // Защита от мусора: CDP-сообщения не больше ~256 МБ
    if (len > 256ull * 1024 * 1024) {
        err = QStringLiteral("frame too large");
        return false;
    }

    QByteArray mask;
    if (masked) {
        if (buffer.size() < offset + 4)
            return false;
        mask = buffer.mid(offset, 4);
        offset += 4;
    }
    if (static_cast<quint64>(buffer.size()) < offset + len)
        return false; // ждём остаток кадра

    QByteArray payload = buffer.mid(offset, static_cast<int>(len));
    if (masked) {
        for (qsizetype i = 0; i < payload.size(); ++i)
            payload[i] = static_cast<char>(payload[i] ^ mask[static_cast<int>(i % 4)]);
    }
    out.payload = payload;
    consumed = offset + static_cast<int>(len);
    return true;
}

void WebSocket::handleControlFrame(const Frame& f)
{
    switch (f.opcode) {
    case kOpPing:
        sendFrame(kOpPong, f.payload);
        break;
    case kOpPong:
        break; // не ждём pong — CDP не пингует
    case kOpClose:
        break;  // обработается вызывающим кодом
    default:
        break;
    }
}

bool WebSocket::sendTextMessage(const QString& message)
{
    if (m_state != State::Open)
        return false;
    sendFrame(kOpText, message.toUtf8());
    return true;
}

void WebSocket::sendFrame(quint8 opcode, const QByteArray& payload)
{
    // Исходящие клиентские кадры ВСЕГДА маскируются (RFC 6455 §5.3)
    QByteArray frame;
    frame.reserve(payload.size() + 14);

    frame.append(static_cast<char>(0x80 | opcode)); // FIN + opcode

    unsigned char maskRaw[4];
    ex::crypto::randomBytes(maskRaw, sizeof maskRaw);
    const QByteArray mask(reinterpret_cast<const char*>(maskRaw), 4);

    if (payload.size() <= 125) {
        frame.append(static_cast<char>(0x80 | payload.size()));
    } else if (payload.size() <= 0xFFFF) {
        frame.append(static_cast<char>(0x80 | 126));
        frame.append(static_cast<char>((payload.size() >> 8) & 0xFF));
        frame.append(static_cast<char>(payload.size() & 0xFF));
    } else {
        frame.append(static_cast<char>(0x80 | 127));
        const quint64 n = static_cast<quint64>(payload.size());
        for (int i = 7; i >= 0; --i)
            frame.append(static_cast<char>((n >> (8 * i)) & 0xFF));
    }
    frame.append(mask);
    for (qsizetype i = 0; i < payload.size(); ++i)
        frame.append(static_cast<char>(payload[i] ^ mask[static_cast<int>(i % 4)]));

    m_socket.write(frame);
}

void WebSocket::onSocketDisconnected()
{
    const bool wasOpen = (m_state == State::Open || m_state == State::Closing);
    m_state = State::Disconnected;
    if (wasOpen)
        emit disconnected();
}

void WebSocket::onSocketError(QAbstractSocket::SocketError error)
{
    emit errorOccurred(QStringLiteral("socket-error-%1").arg(static_cast<int>(error)));
    if (m_state != State::Disconnected) {
        m_state = State::Disconnected;
        m_socket.abort();
        emit disconnected();
    }
}

} // namespace ex::net

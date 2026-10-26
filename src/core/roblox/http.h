#pragma once

#include <QByteArray>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QPair>
#include <QString>
#include <functional>

namespace ex::roblox {

// Результат HTTP-запроса. Заголовки хранятся парами: Set-Cookie может
// приходить несколько раз, и QMap-свёртка потеряла бы ротацию cookie.
struct HttpResponse {
    bool networkOk = false;     // транспортный уровень (не HTTP-статус)
    int status = 0;
    QByteArray body;
    QList<QPair<QByteArray, QByteArray>> allHeaders;
    QString errorString;

    QByteArray header(const QByteArray& name) const
    {
        const QList<QByteArray> values = headers(name);
        return values.isEmpty() ? QByteArray() : values.first();
    }

    QList<QByteArray> headers(const QByteArray& name) const
    {
        QList<QByteArray> out;
        for (const auto& pair : allHeaders) {
            if (pair.first.compare(name, Qt::CaseInsensitive) == 0)
                out.append(pair.second);
        }
        return out;
    }
};

// Асинхронный HTTP-хелпер поверх QNetworkAccessManager.
// Все колбэки вызываются в потоке объекта Http.
class Http : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(HttpResponse)>;

    explicit Http(QObject* parent = nullptr);

    void get(const QNetworkRequest& req, Callback cb, int timeoutMs = 15000);
    void post(const QNetworkRequest& req, const QByteArray& body, Callback cb, int timeoutMs = 15000);
    void postJson(const QNetworkRequest& req, const QByteArray& json, Callback cb, int timeoutMs = 15000);

    static QNetworkRequest jsonRequest(const QUrl& url);
    static QByteArray userAgent();

private:
    void exec(QNetworkReply* reply, Callback cb, int timeoutMs);

    QNetworkAccessManager m_nam;
};

} // namespace ex::roblox

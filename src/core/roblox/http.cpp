#include "core/roblox/http.h"

#include "core/util/log.h"

#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QTimer>

namespace ex::roblox {

namespace {
// Cookie задаётся вручную для каждого аккаунта. Встроенный jar QNAM хранил
// Set-Cookie предыдущего аккаунта и подмешивал его в следующий запрос —
// вторая валидация возвращала первый аккаунт. Jar должен быть пустым.
class EmptyCookieJar final : public QNetworkCookieJar {
public:
    using QNetworkCookieJar::QNetworkCookieJar;

    QList<QNetworkCookie> cookiesForUrl(const QUrl&) const override
    {
        return {};
    }

    bool setCookiesFromUrl(const QList<QNetworkCookie>&, const QUrl&) override
    {
        return false;
    }
};
} // namespace

Http::Http(QObject* parent)
    : QObject(parent)
{
    m_nam.setCookieJar(new EmptyCookieJar);
    m_nam.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

QByteArray Http::userAgent()
{
    // Браузерный UA: часть эндпоинтов (auth-ticket) фильтрует не-браузерные.
    return QByteArrayLiteral(
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36");
}

QNetworkRequest Http::jsonRequest(const QUrl& url)
{
    QNetworkRequest r(url);
    r.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    r.setRawHeader(QByteArrayLiteral("User-Agent"), userAgent());
    r.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json"));
    return r;
}

void Http::exec(QNetworkReply* reply, Callback cb, int timeoutMs)
{
    if (!reply) {
        cb({});
        return;
    }
    auto* timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, [reply]() {
        reply->abort();
    });
    timer->start(timeoutMs);

    connect(reply, &QNetworkReply::finished, this, [this, reply, cb]() {
        HttpResponse res;
        res.networkOk = (reply->error() == QNetworkReply::NoError)
                        || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid();
        const QVariant statusV = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        if (statusV.isValid())
            res.status = statusV.toInt();
        if (reply->error() != QNetworkReply::NoError && res.status == 0)
            res.errorString = reply->errorString();
        res.body = reply->readAll();
        res.allHeaders = reply->rawHeaderPairs();
        if (!res.networkOk && res.status != 0)
            res.networkOk = true; // HTTP-ошибка тоже «состоялся» — статус валиден
        const QNetworkReply::NetworkError e = reply->error();
        const QString es = reply->errorString();
        reply->deleteLater();
        if (e != QNetworkReply::NoError && res.status == 0)
            EXLOG_DEBUG("http") << "network error:" << es;
        cb(res);
    });
}

void Http::get(const QNetworkRequest& req, Callback cb, int timeoutMs)
{
    QNetworkRequest r = req;
    if (!r.hasRawHeader(QByteArrayLiteral("User-Agent")))
        r.setRawHeader(QByteArrayLiteral("User-Agent"), userAgent());
    // HTTP/1.1, как RestSharp в C# RAM: Qt-HTTP/2 с браузерным UA
    // фильтруется Roblox как бот-фингерпринт (cookie-rejected).
    r.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    exec(m_nam.get(r), std::move(cb), timeoutMs);
}

void Http::post(const QNetworkRequest& req, const QByteArray& body, Callback cb, int timeoutMs)
{
    QNetworkRequest r = req;
    if (!r.hasRawHeader(QByteArrayLiteral("User-Agent")))
        r.setRawHeader(QByteArrayLiteral("User-Agent"), userAgent());
    r.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    exec(m_nam.post(r, body), std::move(cb), timeoutMs);
}

void Http::postJson(const QNetworkRequest& req, const QByteArray& json, Callback cb, int timeoutMs)
{
    post(req, json, std::move(cb), timeoutMs);
}

} // namespace ex::roblox

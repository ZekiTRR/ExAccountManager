#include "core/roblox/api.h"

#include "core/util/log.h"
#include "core/util/redact.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>

#include <memory>

namespace ex::roblox {

namespace {

QByteArray cookieHash(const QString& cookie)
{
    return QCryptographicHash::hash(cookie.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
}

// Общие заголовки авторизованного запроса. Referer/Origin и пустой
// x-bound-auth-token повторяют браузер (приём RM): без них часть эндпоинтов
// периодически отвечает 401 даже на живой cookie.
QNetworkRequest sessionRequest(const QUrl& url, const QString& cookie,
                               const QByteArray& csrf = {})
{
    QNetworkRequest r = Http::jsonRequest(url);
    r.setRawHeader(QByteArrayLiteral("Cookie"),
                   QStringLiteral(".ROBLOSECURITY=%1").arg(cookie).toUtf8());
    r.setRawHeader(QByteArrayLiteral("Referer"), QByteArrayLiteral("https://www.roblox.com/"));
    r.setRawHeader(QByteArrayLiteral("Origin"), QByteArrayLiteral("https://www.roblox.com"));
    r.setRawHeader(QByteArrayLiteral("x-bound-auth-token"), QByteArray());
    if (!csrf.isEmpty())
        r.setRawHeader(QByteArrayLiteral("x-csrf-token"), csrf);
    return r;
}

qint64 jsonInt(const QJsonObject& o, const char* camel, const char* pascal = "")
{
    const qint64 direct = o.value(QLatin1String(camel)).toInteger();
    if (direct > 0 || pascal[0] == '\0')
        return direct;
    return o.value(QLatin1String(pascal)).toInteger();
}

QString jsonString(const QJsonObject& o, const char* camel, const char* pascal = "")
{
    const QString direct = o.value(QLatin1String(camel)).toString();
    if (!direct.isEmpty() || pascal[0] == '\0')
        return direct;
    return o.value(QLatin1String(pascal)).toString();
}

// Новый .ROBLOSECURITY из ответа (Set-Cookie), если сервер его ротировал.
QString rotatedCookieFrom(const HttpResponse& res, const QString& currentCookie)
{
    QString rotated;
    const QList<QByteArray> setCookies = res.headers(QByteArrayLiteral("Set-Cookie"));
    for (const QByteArray& raw : setCookies) {
        const QString header = QString::fromLatin1(raw);
        if (!header.contains(QLatin1String(".ROBLOSECURITY="), Qt::CaseInsensitive))
            continue;
        const QString value = util::redact::normalizeCookieValue(header);
        if (!value.isEmpty() && value != currentCookie)
            rotated = value;
    }
    return rotated;
}

QString curlExecutable()
{
    // Системный curl (Windows 10 1803+): Schannel-стек, который проходит
    // auth-ticket там, где Qt-клиент получает 401 на CSRF-retry.
    const QString system = QStringLiteral("C:/Windows/System32/curl.exe");
    if (QFile::exists(system))
        return system;
    return QStandardPaths::findExecutable(QStringLiteral("curl"));
}

QString escapeCurlConfig(QString value)
{
    // Не даём значению (cookie/токен) добавить строки в curl-конфиг.
    value.remove(QLatin1Char('\r'));
    value.remove(QLatin1Char('\n'));
    value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return value;
}

struct CurlHeaders {
    int status = 0;
    QList<QPair<QString, QString>> fields;

    QString first(const QString& name) const
    {
        for (const auto& f : fields) {
            if (f.first.compare(name, Qt::CaseInsensitive) == 0)
                return f.second;
        }
        return {};
    }
};

CurlHeaders parseCurlHeaders(const QByteArray& data)
{
    CurlHeaders out;
    QString text = QString::fromLatin1(data);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        if (line.startsWith(QLatin1String("HTTP/"))) {
            const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (parts.size() >= 2)
                out.status = parts.at(1).toInt();
            out.fields.clear();
            continue;
        }
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon > 0)
            out.fields.append({line.left(colon).trimmed(), line.mid(colon + 1).trimmed()});
    }
    return out;
}

QString rotatedSecurityCookie(const CurlHeaders& headers, const QString& current)
{
    QString rotated;
    for (const auto& f : headers.fields) {
        if (f.first.compare(QStringLiteral("set-cookie"), Qt::CaseInsensitive) != 0)
            continue;
        if (!f.second.contains(QStringLiteral(".ROBLOSECURITY"), Qt::CaseInsensitive))
            continue;
        const QString value = util::redact::normalizeCookieValue(f.second);
        if (!value.isEmpty() && value != current)
            rotated = value;
    }
    return rotated;
}

QString buildCurlGetConfig(const QString& cookie, const QUrl& url,
                           const QString& outputPath)
{
    QString config;
    config += QStringLiteral("url = \"%1\"\n")
                  .arg(escapeCurlConfig(url.toString()));
    config += QStringLiteral("header = \"Cookie: .ROBLOSECURITY=%1\"\n")
                  .arg(escapeCurlConfig(cookie));
    config += QStringLiteral("header = \"Accept: application/json\"\n");
    config += QStringLiteral("header = \"User-Agent: %1\"\n")
                  .arg(escapeCurlConfig(QString::fromLatin1(Http::userAgent())));
    config += QStringLiteral("header = \"Referer: https://www.roblox.com/\"\n");
    config += QStringLiteral("header = \"Origin: https://www.roblox.com\"\n");
    config += QStringLiteral("http1.1\n");
    config += QStringLiteral("silent\n");
    config += QStringLiteral("show-error\n");
    config += QStringLiteral("max-time = 20\n");
    config += QStringLiteral("dump-header = \"-\"\n");
    config += QStringLiteral("output = \"%1\"\n")
                  .arg(escapeCurlConfig(QDir::fromNativeSeparators(outputPath)));
    return config;
}

QString buildCurlConfig(const QString& cookie, const QByteArray& csrf)
{
    QString config;
    config += QStringLiteral("url = \"https://auth.roblox.com/v1/authentication-ticket\"\n");
    config += QStringLiteral("request = \"POST\"\n");
    config += QStringLiteral("header = \"Cookie: .ROBLOSECURITY=%1\"\n")
                  .arg(escapeCurlConfig(cookie));
    config += QStringLiteral("header = \"Content-Type: application/json\"\n");
    config += QStringLiteral("header = \"Accept: application/json\"\n");
    config += QStringLiteral("header = \"User-Agent: %1\"\n")
                  .arg(escapeCurlConfig(QString::fromLatin1(Http::userAgent())));
    config += QStringLiteral("header = \"Referer: https://www.roblox.com/\"\n");
    config += QStringLiteral("header = \"Origin: https://www.roblox.com\"\n");
    if (!csrf.isEmpty()) {
        config += QStringLiteral("header = \"x-csrf-token: %1\"\n")
                      .arg(escapeCurlConfig(QString::fromLatin1(csrf)));
    }
    config += QStringLiteral("data = \"{}\"\n");
    config += QStringLiteral("http1.1\n");
    config += QStringLiteral("silent\n");
    config += QStringLiteral("show-error\n");
    config += QStringLiteral("dump-header = \"-\"\n");
    config += QStringLiteral("output = \"NUL\"\n");
    return config;
}

} // namespace

RobloxApi::RobloxApi(QObject* parent)
    : QObject(parent)
{
}

void RobloxApi::authedPost(const QString& cookie, const QNetworkRequest& reqIn,
                           const QByteArray& body,
                           std::function<void(HttpResponse, QString)> cb, int attempts)
{
    QNetworkRequest req = reqIn;
    const QByteArray token = m_csrf.value(cookieHash(cookie));
    if (!token.isEmpty())
        req.setRawHeader(QByteArrayLiteral("x-csrf-token"), token);

    m_http.post(req, body,
                [this, cookie, req, body, cb = std::move(cb), attempts](HttpResponse res) mutable {
        EXLOG_DEBUG("api") << "HTTP" << res.status << req.url().toString();
        const QString rotated = rotatedCookieFrom(res, cookie);
        const QString effective = rotated.isEmpty() ? cookie : rotated;
        observeSetCookie(cookie, res);

        if (!rotated.isEmpty()) {
            // CSRF-токен принадлежит сессии, а не конкретному значению cookie:
            // переносим его на новый ключ, иначе retry не найдёт токен.
            const QByteArray cached = m_csrf.value(cookieHash(cookie));
            if (!cached.isEmpty())
                m_csrf.insert(cookieHash(effective), cached);
        }

        auto withEffectiveCookie = [&req, effective, rotated]() {
            QNetworkRequest retry = req;
            if (!rotated.isEmpty()) {
                retry.setRawHeader(
                    QByteArrayLiteral("Cookie"),
                    QStringLiteral(".ROBLOSECURITY=%1").arg(effective).toUtf8());
            }
            return retry;
        };

        if (res.status == 403) {
            const QByteArray fresh = res.header(QByteArrayLiteral("x-csrf-token"));
            if (!fresh.isEmpty()) {
                EXLOG_DEBUG("api") << "csrf challenge, retrying with token";
                m_csrf.insert(cookieHash(effective), fresh);
                if (attempts > 1) {
                    authedPost(effective, withEffectiveCookie(), body, std::move(cb),
                               attempts - 1);
                    return;
                }
            }
        } else if (res.status == 429 && attempts > 1) {
            const QByteArray retryAfter = res.header(QByteArrayLiteral("Retry-After"));
            bool okNum = false;
            const int seconds = QString::fromLatin1(retryAfter).toInt(&okNum);
            const int waitMs = (okNum && seconds > 0 ? seconds * 1000 : 2000)
                               + QRandomGenerator::global()->bounded(500);
            QNetworkRequest retry = withEffectiveCookie();
            QTimer::singleShot(
                waitMs, this,
                [this, effective, retry, body, cb = std::move(cb), attempts]() mutable {
                    authedPost(effective, retry, body, std::move(cb), attempts - 1);
                });
            return;
        }
        cb(res, effective);
    });
}

void RobloxApi::observeSetCookie(const QString& oldCookie, const HttpResponse& res)
{
    const QString rotated = rotatedCookieFrom(res, oldCookie);
    if (rotated.isEmpty())
        return;
    EXLOG_INFO("api") << "server rotated .ROBLOSECURITY";
    emit cookieRotated(oldCookie, rotated);
}

void RobloxApi::validateCookie(const QString& cookie,
                               std::function<void(const CookieValidation&)> cb)
{
    CookieValidation out;
    out.effectiveCookie = cookie;
    if (cookie.trimmed().isEmpty()) {
        out.check = SessionCheck::InvalidSession;
        out.error = QStringLiteral("empty-cookie");
        cb(out);
        return;
    }

    auto deliver = std::make_shared<
        std::function<void(const CookieValidation&)>>(std::move(cb));

    const QNetworkRequest req = sessionRequest(
        QUrl(QStringLiteral("https://users.roblox.com/v1/users/authenticated")), cookie);
    m_http.get(req, [this, cookie, deliver](HttpResponse res) mutable {
        CookieValidation result;
        const QString rotated = rotatedCookieFrom(res, cookie);
        result.effectiveCookie = rotated.isEmpty() ? cookie : rotated;
        if (!rotated.isEmpty()) {
            EXLOG_INFO("api") << "server rotated .ROBLOSECURITY during validation";
            emit cookieRotated(cookie, rotated);
        }

        // Qt-стек иногда получает ложный 401 (иная TLS/HTTP-подпись), поэтому
        // любой не-OK исход перепроверяем системным curl — как auth-ticket.
        auto fallback = [this, cookie, deliver](CookieValidation qt) {
            const QString curlCookie =
                qt.effectiveCookie.isEmpty() ? cookie : qt.effectiveCookie;
            validateCookieViaCurl(
                curlCookie, std::move(qt),
                [deliver](const CookieValidation& v) mutable { (*deliver)(v); });
        };

        if (res.status == 401) {
            result.check = SessionCheck::InvalidSession;
            result.error = QStringLiteral("cookie-rejected");
            EXLOG_WARN("api") << "Qt validation rejected cookie, verifying via curl";
            fallback(std::move(result));
            return;
        }
        if (res.status == 403) {
            // JSON-ошибка Roblox = сессия недействительна; HTML/Cloudflare =
            // блокировка запроса, сессию не трогаем.
            const bool robloxJson = res.body.contains("\"errors\"")
                || res.body.contains("not authenticated");
            result.check = robloxJson ? SessionCheck::InvalidSession
                                      : SessionCheck::ServerError;
            result.error = robloxJson ? QStringLiteral("cookie-rejected")
                                      : QStringLiteral("blocked");
            EXLOG_WARN("api") << "validate cookie status 403, robloxJson" << robloxJson;
            fallback(std::move(result));
            return;
        }
        if (res.status != 200) {
            if (res.status == 0 || !res.networkOk) {
                result.check = SessionCheck::NetworkError;
                result.error = QStringLiteral("network-error");
            } else {
                result.check = SessionCheck::ServerError;
                result.error = res.status == 429
                    ? QStringLiteral("rate-limited")
                    : QStringLiteral("http-%1").arg(res.status);
            }
            fallback(std::move(result));
            return;
        }

        const QJsonObject o = QJsonDocument::fromJson(res.body).object();
        result.user.userId = jsonInt(o, "id", "UserId");
        result.user.username = jsonString(o, "name", "Name");
        if (result.user.username.isEmpty())
            result.user.username = o.value(QLatin1String("Username")).toString();
        result.user.displayName = jsonString(o, "displayName", "DisplayName");
        if (result.user.displayName.isEmpty())
            result.user.displayName = result.user.username;

        // Диагностика формата: имена Set-Cookie без значений (ротация/
        // удаление .ROBLOSECURITY — частая причина 401 после валидации).
        for (const QByteArray& raw : res.headers(QByteArrayLiteral("Set-Cookie"))) {
            const QString header = QString::fromLatin1(raw);
            const QString first = header.section(QLatin1Char(';'), 0, 0);
            const QString name = first.section(QLatin1Char('='), 0, 0).trimmed();
            const int eq = first.indexOf(QLatin1Char('='));
            EXLOG_INFO("api") << "validation set-cookie:" << name
                              << "valueLen"
                              << (eq >= 0 ? first.mid(eq + 1).trimmed().size() : 0);
        }

        if (result.user.userId <= 0 || result.user.username.isEmpty()) {
            result.check = SessionCheck::Unexpected;
            result.error = QStringLiteral("bad-response");
            fallback(std::move(result));
            return;
        }
        result.check = SessionCheck::Ok;
        (*deliver)(result);
    });
}

void RobloxApi::validateCookieViaCurl(
    const QString& cookie, CookieValidation fallback,
    std::function<void(const CookieValidation&)> cb)
{
    const QString exe = curlExecutable();
    if (exe.isEmpty()) {
        cb(fallback);
        return;
    }

    const QString outputPath = QDir::tempPath()
        + QStringLiteral("/exm_validate_%1.json")
              .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    auto* proc = new QProcess(this);
    auto* timeout = new QTimer(proc);
    timeout->setSingleShot(true);

    auto done = std::make_shared<bool>(false);
    auto finish = [this, proc, timeout, outputPath, cookie, fallback,
                   cb = std::move(cb), done](
                      bool executed, int status, const QByteArray& body,
                      const CurlHeaders& headers) mutable {
        if (*done)
            return;
        *done = true;
        timeout->stop();
        proc->disconnect();
        proc->deleteLater();
        QFile::remove(outputPath);

        if (!executed) {
            EXLOG_WARN("api") << "curl validation unavailable, keeping Qt result:"
                              << fallback.error;
            cb(fallback);
            return;
        }

        CookieValidation result;
        result.effectiveCookie = cookie;
        const QString rotated = rotatedSecurityCookie(headers, cookie);
        if (!rotated.isEmpty()) {
            result.effectiveCookie = rotated;
            EXLOG_INFO("api") << "server rotated .ROBLOSECURITY during curl validation";
            emit cookieRotated(cookie, rotated);
        }

        if (status == 401) {
            result.check = SessionCheck::InvalidSession;
            result.error = QStringLiteral("cookie-rejected");
            EXLOG_WARN("api") << "curl validation rejected cookie";
            cb(result);
            return;
        }
        if (status == 403) {
            const bool robloxJson = body.contains("\"errors\"")
                || body.contains("not authenticated");
            result.check = robloxJson ? SessionCheck::InvalidSession
                                      : SessionCheck::ServerError;
            result.error = robloxJson ? QStringLiteral("cookie-rejected")
                                      : QStringLiteral("blocked");
            cb(result);
            return;
        }
        if (status != 200) {
            if (status == 0) {
                result.check = SessionCheck::NetworkError;
                result.error = QStringLiteral("network-error");
            } else {
                result.check = SessionCheck::ServerError;
                result.error = status == 429
                    ? QStringLiteral("rate-limited")
                    : QStringLiteral("http-%1").arg(status);
            }
            cb(result);
            return;
        }

        const QJsonObject o = QJsonDocument::fromJson(body).object();
        result.user.userId = jsonInt(o, "id", "UserId");
        result.user.username = jsonString(o, "name", "Name");
        if (result.user.username.isEmpty())
            result.user.username = o.value(QLatin1String("Username")).toString();
        result.user.displayName = jsonString(o, "displayName", "DisplayName");
        if (result.user.displayName.isEmpty())
            result.user.displayName = result.user.username;
        if (result.user.userId <= 0 || result.user.username.isEmpty()) {
            result.check = SessionCheck::Unexpected;
            result.error = QStringLiteral("bad-response");
            cb(result);
            return;
        }
        result.check = SessionCheck::Ok;
        cb(result);
    };

    connect(timeout, &QTimer::timeout, this, [proc, finish]() mutable {
        proc->kill();
        finish(false, 0, {}, {});
    });
    connect(proc, &QProcess::errorOccurred, this,
            [finish](QProcess::ProcessError) mutable {
                finish(false, 0, {}, {});
            });
    connect(proc, &QProcess::finished, this,
            [proc, finish, outputPath](int exitCode, QProcess::ExitStatus status) mutable {
                const QByteArray out = proc->readAllStandardOutput();
                const QByteArray errOut = proc->readAllStandardError();
                if (status != QProcess::NormalExit || exitCode != 0) {
                    EXLOG_WARN("api") << "curl validation exited" << exitCode
                                      << QString::fromLatin1(errOut.left(120));
                    finish(false, 0, {}, {});
                    return;
                }
                QByteArray body;
                QFile file(outputPath);
                if (file.open(QIODevice::ReadOnly))
                    body = file.readAll();
                const CurlHeaders headers = parseCurlHeaders(out);
                finish(true, headers.status, body, headers);
            });

    timeout->start(22000);
    proc->setProgram(exe);
    proc->setArguments({QStringLiteral("--config"), QStringLiteral("-")});
    proc->start();
    if (!proc->waitForStarted(2000)) {
        finish(false, 0, {}, {});
        return;
    }
    proc->write(buildCurlGetConfig(
                    cookie,
                    QUrl(QStringLiteral(
                        "https://users.roblox.com/v1/users/authenticated")),
                    outputPath)
                    .toUtf8());
    proc->closeWriteChannel();
}

void RobloxApi::generateAuthTicket(const QString& cookie, TicketCallback cb)
{
    if (curlExecutable().isEmpty()) {
        EXLOG_WARN("api") << "curl.exe not found; using Qt HTTP for auth ticket";
        generateAuthTicketViaQt(cookie, std::move(cb));
        return;
    }
    generateAuthTicketViaCurl(cookie, std::move(cb));
}

void RobloxApi::generateAuthTicketViaQt(const QString& cookie, TicketCallback cb)
{
    // Fallback без curl: Astro-форма (тело "{}" + Content-Type JSON).
    // Qt-клиент на этой сессии получает 401 на CSRF-retry, поэтому основной
    // путь — curl (см. generateAuthTicketViaCurl).
    QNetworkRequest req = sessionRequest(
        QUrl(QStringLiteral("https://auth.roblox.com/v1/authentication-ticket")), cookie);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    authedPost(cookie, req, QByteArrayLiteral("{}"),
               [cb = std::move(cb)](HttpResponse res,
                                    const QString& effectiveFromPost) mutable {
        const QString rotated = rotatedCookieFrom(res, effectiveFromPost);
        const QString effective = rotated.isEmpty() ? effectiveFromPost : rotated;
        if (res.status != 200) {
            QString err;
            if (res.status == 401)
                err = QStringLiteral("cookie-rejected");
            else if (res.status == 403)
                err = QStringLiteral("csrf-failed");
            else if (res.status == 429)
                err = QStringLiteral("rate-limited");
            else if (res.status == 0 || !res.networkOk)
                err = QStringLiteral("network-error");
            else
                err = QStringLiteral("http-%1").arg(res.status);
            cb(false, QString(), err, effective);
            return;
        }

        QByteArray ticket = res.header(QByteArrayLiteral("rbx-authentication-ticket"));
        if (ticket.isEmpty()) {
            const QJsonObject o = QJsonDocument::fromJson(res.body).object();
            ticket = o.value(QStringLiteral("ticket")).toString().toLatin1();
        }
        if (ticket.isEmpty()) {
            cb(false, QString(), QStringLiteral("no-ticket-header"), effective);
            return;
        }
        cb(true, QString::fromLatin1(ticket), QString(), effective);
    });
}

void RobloxApi::generateAuthTicketViaCurl(const QString& cookie, TicketCallback cb)
{
    struct State {
        QString cookie;
        int attempt = 1;
        TicketCallback cb;
        QProcess* proc = nullptr;
        QTimer* timeout = nullptr;
        bool done = false;
        std::function<void(const QByteArray&)> run;
    };
    auto state = std::make_shared<State>();
    state->cookie = cookie;
    state->cb = std::move(cb);

    auto finish = [this, state](bool ok, const QString& ticket, const QString& err,
                                const QString& effective) {
        if (state->done)
            return;
        state->done = true;
        if (state->timeout)
            state->timeout->stop();
        QProcess* proc = state->proc;
        state->proc = nullptr;
        if (proc) {
            proc->disconnect();
            proc->deleteLater();
        }
        auto cb = std::move(state->cb);
        state->cb = nullptr;
        state->run = nullptr;
        if (ok)
            EXLOG_INFO("api") << "auth ticket issued (curl), len" << ticket.size();
        else
            EXLOG_WARN("api") << "auth ticket failed (curl):" << err;
        if (cb)
            cb(ok, ticket, err, effective);
    };

    auto run = [this, state, finish](const QByteArray& csrf) {
        if (state->done)
            return;
        QProcess* proc = new QProcess(this);
        state->proc = proc;
        auto* timeout = new QTimer(proc);
        state->timeout = timeout;
        timeout->setSingleShot(true);
        connect(timeout, &QTimer::timeout, this, [proc, finish]() {
            proc->kill();
            finish(false, QString(), QStringLiteral("network-error"), QString());
        });
        connect(proc, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError) {
            finish(false, QString(), QStringLiteral("curl-failed"), QString());
        });
        connect(proc, &QProcess::finished, this,
                [this, state, finish, csrf](int exitCode, QProcess::ExitStatus status) {
            QProcess* p = state->proc;
            if (!p || state->done)
                return;
            const QByteArray out = p->readAllStandardOutput();
            const QByteArray errOut = p->readAllStandardError();
            p->disconnect();
            p->deleteLater();
            state->proc = nullptr;
            if (state->timeout)
                state->timeout->stop();

            if (status != QProcess::NormalExit || exitCode != 0) {
                EXLOG_WARN("api") << "curl exited" << exitCode
                                  << QString::fromLatin1(errOut.left(120));
                finish(false, QString(), QStringLiteral("curl-failed"), state->cookie);
                return;
            }

            const CurlHeaders headers = parseCurlHeaders(out);
            EXLOG_INFO("api") << "curl ticket" << (csrf.isEmpty() ? "first" : "retry")
                              << "status" << headers.status;
            const QString rotated = rotatedSecurityCookie(headers, state->cookie);
            if (!rotated.isEmpty()) {
                emit cookieRotated(state->cookie, rotated);
                state->cookie = rotated;
            }

            const QString ticket =
                headers.first(QStringLiteral("rbx-authentication-ticket"));
            if (headers.status == 200 && !ticket.isEmpty()) {
                finish(true, ticket, QString(), state->cookie);
                return;
            }
            if (headers.status == 403 && state->attempt == 1) {
                const QString token = headers.first(QStringLiteral("x-csrf-token"));
                if (!token.isEmpty()) {
                    state->attempt = 2;
                    if (state->run)
                        state->run(token.toLatin1());
                    return;
                }
            }
            QString err;
            if (headers.status == 401)
                err = QStringLiteral("cookie-rejected");
            else if (headers.status == 403)
                err = QStringLiteral("csrf-failed");
            else if (headers.status == 429)
                err = QStringLiteral("rate-limited");
            else if (headers.status == 0)
                err = QStringLiteral("network-error");
            else
                err = QStringLiteral("http-%1").arg(headers.status);
            finish(false, QString(), err, state->cookie);
        });

        timeout->start(20000);
        proc->setProgram(curlExecutable());
        proc->setArguments({QStringLiteral("--config"), QStringLiteral("-")});
        proc->start();
        if (!proc->waitForStarted(2000)) {
            finish(false, QString(), QStringLiteral("curl-start-failed"), state->cookie);
            return;
        }
        proc->write(buildCurlConfig(state->cookie, csrf).toUtf8());
        proc->closeWriteChannel();
    };
    state->run = run;
    run(QByteArray());
}

void RobloxApi::presence(const QString& cookie, const QList<qint64>& userIds,
                         std::function<void(bool, QList<model::Presence>)> cb)
{
    if (userIds.isEmpty()) {
        cb(true, {});
        return;
    }
    QJsonArray arr;
    for (qint64 id : userIds)
        arr.append(static_cast<double>(id));
    QJsonObject body;
    body[QStringLiteral("userIds")] = arr;

    QNetworkRequest req = sessionRequest(
        QUrl(QStringLiteral("https://presence.roblox.com/v1/presence/users")), cookie);
    authedPost(cookie, req, QJsonDocument(body).toJson(QJsonDocument::Compact),
               [cb = std::move(cb)](HttpResponse res, const QString&) mutable {
        QList<model::Presence> out;
        if (res.status != 200) {
            cb(false, out);
            return;
        }
        const QJsonArray presences = QJsonDocument::fromJson(res.body)
            .object()[QStringLiteral("userPresences")].toArray();
        for (const auto& v : presences) {
            const QJsonObject p = v.toObject();
            model::Presence pr;
            pr.userId = p.value(QStringLiteral("userId")).toInteger();
            const int numeric = p.value(QStringLiteral("userPresenceType")).toInt(-1);
            const QString type = p.value(QStringLiteral("type")).toString();
            if (numeric == 0 || type == QLatin1String("Offline"))
                pr.type = model::PresenceType::Offline;
            else if (numeric == 1 || type == QLatin1String("Online"))
                pr.type = model::PresenceType::Online;
            else if (numeric == 2 || type == QLatin1String("InGame"))
                pr.type = model::PresenceType::InGame;
            else if (numeric == 3 || type == QLatin1String("InStudio"))
                pr.type = model::PresenceType::InStudio;
            pr.placeId = p.value(QStringLiteral("placeId")).toInteger();
            pr.universeId = p.value(QStringLiteral("universeId")).toInteger();
            pr.lastLocation = p.value(QStringLiteral("lastLocation")).toString();
            out.append(pr);
        }
        cb(true, out);
    });
}

void RobloxApi::avatarHeadshots(const QList<qint64>& userIds,
                                std::function<void(bool, QMap<qint64, QString>)> cb)
{
    if (userIds.isEmpty()) {
        cb(true, {});
        return;
    }
    QStringList ids;
    for (qint64 id : userIds)
        ids << QString::number(id);
    // Сопоставление строго по targetId — Roblox возвращает не по порядку и
    // может пропускать id (наблюдение RM).
    QUrl url(QStringLiteral("https://thumbnails.roblox.com/v1/users/avatar-headshot"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("userIds"), ids.join(QLatin1Char(',')));
    q.addQueryItem(QStringLiteral("size"), QStringLiteral("150x150"));
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("Png"));
    q.addQueryItem(QStringLiteral("isCircular"), QStringLiteral("false"));
    url.setQuery(q);

    m_http.get(Http::jsonRequest(url), [cb = std::move(cb)](HttpResponse res) mutable {
        QMap<qint64, QString> out;
        if (res.status != 200) {
            cb(false, out);
            return;
        }
        const QJsonArray data = QJsonDocument::fromJson(res.body)
            .object()[QStringLiteral("data")].toArray();
        for (const auto& v : data) {
            const QJsonObject d = v.toObject();
            const qint64 id = d[QStringLiteral("targetId")].toInteger();
            const QString state = d[QStringLiteral("state")].toString();
            if (id > 0 && state == QLatin1String("Completed"))
                out.insert(id, d[QStringLiteral("imageUrl")].toString());
        }
        cb(true, out);
    });
}

void RobloxApi::publicServers(const QString& cookie, qint64 placeId,
                              std::function<void(bool, QList<ServerEntry>, QString)> cb)
{
    QUrl url(QStringLiteral("https://games.roblox.com/v1/games/%1/servers/0").arg(placeId));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("100"));
    url.setQuery(q);

    m_http.get(sessionRequest(url, cookie),
               [this, cookie, cb = std::move(cb)](HttpResponse res) mutable {
        observeSetCookie(cookie, res);
        QList<ServerEntry> out;
        if (res.status != 200) {
            cb(false, out, QString());
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(res.body).object();
        const QString cursor = o[QStringLiteral("nextPageCursor")].toString();
        const QJsonArray data = o[QStringLiteral("data")].toArray();
        for (const auto& v : data) {
            const QJsonObject s = v.toObject();
            ServerEntry e;
            e.jobId = s[QStringLiteral("id")].toString();
            e.playerCount = s[QStringLiteral("playing")].toInt();
            e.maxPlayers = s[QStringLiteral("maxPlayers")].toInt();
            e.ping = s[QStringLiteral("ping")].toDouble();
            e.fps = s[QStringLiteral("fps")].toDouble();
            out.append(e);
        }
        cb(true, out, cursor);
    });
}

void RobloxApi::gameDetails(const QString& cookie, qint64 placeId,
                            std::function<void(bool, GameDetails)> cb)
{
    QNetworkRequest req = sessionRequest(
        QUrl(QStringLiteral("https://games.roblox.com/v1/games/multiget-place-details")), cookie);
    QJsonArray arr;
    arr.append(static_cast<double>(placeId));
    QJsonObject body;
    body[QStringLiteral("placeIds")] = arr;
    authedPost(cookie, req, QJsonDocument(body).toJson(QJsonDocument::Compact),
               [placeId, cb = std::move(cb)](HttpResponse res, const QString&) mutable {
        GameDetails g;
        if (res.status != 200) {
            cb(false, g);
            return;
        }
        const QJsonArray data = QJsonDocument::fromJson(res.body).array();
        for (const auto& v : data) {
            const QJsonObject o = v.toObject();
            if (o[QStringLiteral("placeId")].toInteger() == placeId) {
                g.placeId = placeId;
                g.universeId = o[QStringLiteral("universeId")].toInteger();
                g.name = o[QStringLiteral("name")].toString();
                g.maxPlayers = o[QStringLiteral("maxPlayers")].toInt();
                cb(true, g);
                return;
            }
        }
        cb(false, g);
    });
}

void RobloxApi::resolveShareLink(const QString& cookie, const QString& code,
                                 std::function<void(bool, ShareLinkInfo, QString)> cb)
{
    // Порт Astro: share-ссылка не содержит placeId, Roblox разворачивает её
    // в privateServerInviteData (placeId + linkCode) от имени аккаунта.
    QNetworkRequest req = sessionRequest(
        QUrl(QStringLiteral("https://apis.roblox.com/sharelinks/v1/resolve-link")), cookie);
    QJsonObject body;
    body[QStringLiteral("linkId")] = code;
    body[QStringLiteral("linkType")] = QStringLiteral("Server");
    authedPost(cookie, req, QJsonDocument(body).toJson(QJsonDocument::Compact),
               [cb = std::move(cb)](HttpResponse res, const QString&) mutable {
        ShareLinkInfo info;
        if (res.status == 401) {
            cb(false, info, QStringLiteral("cookie-rejected"));
            return;
        }
        if (res.status == 403) {
            cb(false, info, QStringLiteral("csrf-failed"));
            return;
        }
        if (res.status != 200) {
            cb(false, info, res.status == 429
                               ? QStringLiteral("rate-limited")
                               : QStringLiteral("http-%1").arg(res.status));
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(res.body).object();
        const QJsonObject invite = o.value(QStringLiteral("privateServerInviteData")).toObject();
        info.placeId = invite.value(QStringLiteral("placeId")).toInteger();
        if (info.placeId <= 0)
            info.placeId = invite.value(QStringLiteral("placeId")).toString().toLongLong();
        info.status = invite.value(QStringLiteral("status")).toString();
        info.privateServerCode = invite.value(QStringLiteral("linkCode")).toString();
        if (info.privateServerCode.isEmpty())
            info.privateServerCode = invite.value(QStringLiteral("inviteCode")).toString();
        if (info.privateServerCode.isEmpty())
            info.privateServerCode = invite.value(QStringLiteral("code")).toString();
        if (info.placeId <= 0 || info.privateServerCode.isEmpty()) {
            cb(false, info, QStringLiteral("not-a-private-server-link"));
            return;
        }
        // Astro: статус может быть "Valid"/"Invalid" — не притворяемся, что
        // недействительное приглашение сработает.
        if (!info.status.isEmpty()
            && info.status.compare(QStringLiteral("valid"), Qt::CaseInsensitive) != 0) {
            cb(false, info, QStringLiteral("share-link-%1").arg(info.status.toLower()));
            return;
        }
        cb(true, info, QString());
    });
}

void RobloxApi::usernameToUserId(const QString& cookie, const QString& username,
                                 std::function<void(bool, qint64)> cb)
{
    QNetworkRequest req = sessionRequest(
        QUrl(QStringLiteral("https://users.roblox.com/v1/usernames/users")), cookie);
    QJsonArray arr;
    arr.append(username);
    QJsonObject body;
    body[QStringLiteral("usernames")] = arr;
    body[QStringLiteral("excludeBannedUsers")] = false; // для забаненных (приём RM)
    authedPost(cookie, req, QJsonDocument(body).toJson(QJsonDocument::Compact),
               [cb = std::move(cb)](HttpResponse res, const QString&) mutable {
        if (res.status != 200) {
            cb(false, 0);
            return;
        }
        const QJsonArray data = QJsonDocument::fromJson(res.body)
            .object()[QStringLiteral("data")].toArray();
        if (data.isEmpty()) {
            cb(false, 0);
            return;
        }
        cb(true, data[0].toObject()[QStringLiteral("id")].toInteger());
    });
}

void RobloxApi::moderationStatus(const QString& cookie, qint64 userId,
                                 std::function<void(bool, model::ModerationInfo)> cb)
{
    // Публичный флаг isBanned + cookie-only причина.
    QNetworkRequest pub = Http::jsonRequest(
        QUrl(QStringLiteral("https://users.roblox.com/v1/users/%1").arg(userId)));
    m_http.get(pub, [this, cookie, userId, cb = std::move(cb)](HttpResponse res1) mutable {
        model::ModerationInfo m;
        if (res1.status != 200) {
            cb(false, m);
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(res1.body).object();
        if (!o[QStringLiteral("isBanned")].toBool()) {
            cb(true, m); // не модерируется
            return;
        }
        m.moderated = true;
        // Причина и срок — от авторизованного пользователя (только сам себя).
        QNetworkRequest req = sessionRequest(
            QUrl(QStringLiteral("https://usermoderation.roblox.com/v1/not-approved")), cookie);
        m_http.get(req, [this, cookie, cb = std::move(cb), m](HttpResponse res2) mutable {
            observeSetCookie(cookie, res2);
            if (res2.status == 200) {
                const QJsonObject mo = QJsonDocument::fromJson(res2.body).object();
                m.reason = mo[QStringLiteral("moderationMessage")] // v1 поле может отличаться
                               .toString(mo[QStringLiteral("reason")].toString());
            }
            cb(true, m);
        });
    });
}

void RobloxApi::clearCsrf(const QString& cookie)
{
    m_csrf.remove(cookieHash(cookie));
}

} // namespace ex::roblox

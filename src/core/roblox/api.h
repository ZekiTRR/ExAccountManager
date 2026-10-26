#pragma once

#include "core/models/account.h"
#include "core/roblox/http.h"

#include <QList>
#include <QObject>
#include <QString>
#include <functional>

namespace ex::roblox {

// Высокоуровневый клиент Roblox API.
// Особенности (выжимка реалий API из трёх менеджеров):
//  - валидация сессии — users.roblox.com/v1/users/authenticated (RM/Astro);
//  - CSRF: POST c cookie сначала получает 403 + x-csrf-token, повторяем с ним.
//    Токен кэшируется per-cookie (у каждого аккаунта свой);
//  - Roblox может ротировать .ROBLOSECURITY через Set-Cookie (с 2026 г.);
//    каждую ротацию нужно сохранять, иначе следующие запросы получат 401;
//  - 429: Retry-After + jitter, до 3 попыток.
struct AuthedUser {
    qint64 userId = 0;
    QString username;
    QString displayName;
    bool hasPremium = false;
};

// Чем закончилась проверка cookie. Сетевую ошибку НЕЛЬЗЯ трактовать как
// истёкшую сессию — иначе один offline-запрос помечает все аккаунты мёртвыми.
enum class SessionCheck {
    Ok,
    InvalidSession, // 401/403 — cookie действительно отклонён
    NetworkError,   // таймаут/DNS/offline
    ServerError,    // 5xx/429 и прочие ответы сервера
    Unexpected,     // 200, но тело не разобрано
};

struct CookieValidation {
    SessionCheck check = SessionCheck::Unexpected;
    AuthedUser user;
    QString error;
    QString effectiveCookie; // cookie с учётом Set-Cookie-ротации
};

struct ServerEntry {
    QString jobId;
    int playerCount = 0;
    int maxPlayers = 0;
    QString machineAddress; // заполняется через join-game probe
    double fps = 0;         // avgPing? нет — performanceStats если есть
    double ping = 0;
};

struct GameDetails {
    qint64 placeId = 0;
    qint64 universeId = 0;
    QString name;
    int playing = 0;
    int maxPlayers = 0;
    QString iconUrl;
};

// Резолв share-ссылки (roblox.com/share?code=...) — порт Astro
// read_private_server_invite: Roblox отдаёт privateServerInviteData.
struct ShareLinkInfo {
    qint64 placeId = 0;
    QString privateServerCode;
    QString status;
};

class RobloxApi : public QObject {
    Q_OBJECT
public:
    using TicketCallback =
        std::function<void(bool ok, QString ticket, QString error, QString effectiveCookie)>;

    explicit RobloxApi(QObject* parent = nullptr);

    // --- валидация cookie / информация о себе ---
    void validateCookie(const QString& cookie,
                        std::function<void(const CookieValidation&)> cb);

    // --- auth ticket (для запуска клиента) ---
    // POST auth.roblox.com/v1/authentication-ticket; ticket в заголовке
    // rbx-authentication-ticket (или в JSON-поле ticket). CSRF обрабатывается.
    // effectiveCookie — cookie с учётом Set-Cookie-ротации в этом же запросе;
    // его нужно сохранить, иначе следующий запрос может получить 401.
    void generateAuthTicket(const QString& cookie, TicketCallback cb);

    // --- presence (батч) ---
    void presence(const QString& anyCookie, const QList<qint64>& userIds,
                  std::function<void(bool ok, QList<model::Presence>)> cb);

    // --- аватары (headshot, без авторизации) ---
    void avatarHeadshots(const QList<qint64>& userIds,
                         std::function<void(bool ok, QMap<qint64, QString>)> cb);

    // --- публичные серверы игры ---
    void publicServers(const QString& anyCookie, qint64 placeId,
                       std::function<void(bool ok, QList<ServerEntry>, QString cursor)> cb);

    // --- детали игры ---
    void gameDetails(const QString& anyCookie, qint64 placeId,
                     std::function<void(bool ok, GameDetails)> cb);

    // --- резолв share-ссылки (roblox.com/share?code=...) ---
    void resolveShareLink(const QString& cookie, const QString& code,
                          std::function<void(bool ok, ShareLinkInfo, QString error)> cb);

    // --- поиск пользователя по имени (для follow / добавления) ---
    void usernameToUserId(const QString& anyCookie, const QString& username,
                          std::function<void(bool ok, qint64 userId)> cb);

    // --- модерация аккаунта ---
    void moderationStatus(const QString& cookie, qint64 userId,
                          std::function<void(bool ok, model::ModerationInfo)> cb);

    // Очистить кэш CSRF (например, после 401).
    void clearCsrf(const QString& cookie);

    // Низкоуровневый доступ (посторонние запросы).
    Http& http() { return m_http; }

signals:
    // Сервер выдал новый .ROBLOSECURITY через Set-Cookie: его нужно сохранить.
    void cookieRotated(const QString& oldCookie, const QString& newCookie);

private:
    // POST с автоматическим CSRF: 403+x-csrf-token -> retry. Второй аргумент
    // колбэка — effective cookie с учётом Set-Cookie-ротации (в т.ч. в 403).
    void authedPost(const QString& cookie, const QNetworkRequest& req, const QByteArray& body,
                    std::function<void(HttpResponse, QString)> cb, int attempts = 3);

    // Проверенный сетевой стек: curl.exe (Schannel) даёт auth-ticket там, где
    // Qt-клиент получает 401 на retry (воспроизведено на живой сессии).
    // Cookie передаётся через stdin конфигом curl, не через argv.
    void generateAuthTicketViaCurl(const QString& cookie, TicketCallback cb);
    void generateAuthTicketViaQt(const QString& cookie, TicketCallback cb);

    // Проверка cookie через системный curl (Schannel), если Qt-стек получил
    // отказ или сетевую ошибку. fallback возвращается, когда curl недоступен.
    void validateCookieViaCurl(const QString& cookie, CookieValidation fallback,
                               std::function<void(const CookieValidation&)> cb);

    // Разобрать Set-Cookie всех ответов; эмитит cookieRotated при изменении.
    void observeSetCookie(const QString& oldCookie, const HttpResponse& res);

    Http m_http;
    QHash<QString, QByteArray> m_csrf; // cookie-hash -> token
};

} // namespace ex::roblox

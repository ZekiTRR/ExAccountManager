#pragma once

#include "core/models/account.h"
#include "core/roblox/http.h"

#include <QList>
#include <QObject>
#include <QString>
#include <functional>

namespace ex::roblox {

// High-level Roblox API client.
// API quirks distilled from three account managers:
//  - session validation uses users.roblox.com/v1/users/authenticated (RM/Astro);
//  - CSRF: a POST with a cookie first gets 403 + x-csrf-token, then retry with it.
//    The token is cached per cookie (each account has its own);
//  - Roblox may rotate .ROBLOSECURITY via Set-Cookie (since 2026);
//    every rotation must be persisted, otherwise later requests get 401;
//  - 429: Retry-After + jitter, up to 3 attempts.
struct AuthedUser {
    qint64 userId = 0;
    QString username;
    QString displayName;
    bool hasPremium = false;
};

// Outcome of a cookie check. A network error MUST NOT be treated as an
// expired session, or a single offline request would mark every account dead.
enum class SessionCheck {
    Ok,
    InvalidSession, // 401/403 — the cookie really was rejected
    NetworkError,   // timeout/DNS/offline
    ServerError,    // 5xx/429 and other server responses
    Unexpected,     // 200, but the body could not be parsed
};

struct CookieValidation {
    SessionCheck check = SessionCheck::Unexpected;
    AuthedUser user;
    QString error;
    QString effectiveCookie; // cookie after any Set-Cookie rotation
};

struct ServerEntry {
    QString jobId;
    int playerCount = 0;
    int maxPlayers = 0;
    QString machineAddress; // not set by publicServers(); belongs to a join-game probe
    double fps = 0;         // taken from the "fps" field if the server includes it
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

// Share-link resolution (roblox.com/share?code=...) ported from Astro's
// read_private_server_invite: Roblox returns privateServerInviteData.
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

    // --- cookie validation / own account info ---
    void validateCookie(const QString& cookie,
                        std::function<void(const CookieValidation&)> cb);

    // --- auth ticket (for launching the client) ---
    // POST auth.roblox.com/v1/authentication-ticket; the ticket comes back in the
    // rbx-authentication-ticket header (or the JSON field ticket). CSRF is handled.
    // effectiveCookie reflects any Set-Cookie rotation within this same request;
    // it must be persisted, otherwise the next request may get 401.
    void generateAuthTicket(const QString& cookie, TicketCallback cb);

    // --- presence (batch) ---
    void presence(const QString& anyCookie, const QList<qint64>& userIds,
                  std::function<void(bool ok, QList<model::Presence>)> cb);

    // --- avatars (headshot, no authentication) ---
    void avatarHeadshots(const QList<qint64>& userIds,
                         std::function<void(bool ok, QMap<qint64, QString>)> cb);

    // --- public game servers ---
    void publicServers(const QString& anyCookie, qint64 placeId,
                       std::function<void(bool ok, QList<ServerEntry>, QString cursor)> cb);

    // --- game details ---
    void gameDetails(const QString& anyCookie, qint64 placeId,
                     std::function<void(bool ok, GameDetails)> cb);

    // --- share-link resolution (roblox.com/share?code=...) ---
    void resolveShareLink(const QString& cookie, const QString& code,
                          std::function<void(bool ok, ShareLinkInfo, QString error)> cb);

    // --- username lookup (for follow / adding accounts) ---
    void usernameToUserId(const QString& anyCookie, const QString& username,
                          std::function<void(bool ok, qint64 userId)> cb);

    // --- account moderation ---
    void moderationStatus(const QString& cookie, qint64 userId,
                          std::function<void(bool ok, model::ModerationInfo)> cb);

    // Drop the cached CSRF token (e.g. after a 401).
    void clearCsrf(const QString& cookie);

    // Low-level access for ad-hoc requests.
    Http& http() { return m_http; }

signals:
    // The server issued a new .ROBLOSECURITY via Set-Cookie; it must be persisted.
    void cookieRotated(const QString& oldCookie, const QString& newCookie);

private:
    // POST with automatic CSRF: 403 + x-csrf-token -> retry. The callback's second
    // argument is the effective cookie after Set-Cookie rotation (including in 403).
    void authedPost(const QString& cookie, const QNetworkRequest& req, const QByteArray& body,
                    std::function<void(HttpResponse, QString)> cb, int attempts = 3);

    // Verified network stack: curl.exe (Schannel) yields an auth ticket where the
    // Qt client gets 401 on retry (reproduced on a live session).
    // The cookie is passed to curl through a stdin config, never through argv.
    void generateAuthTicketViaCurl(const QString& cookie, TicketCallback cb);
    void generateAuthTicketViaQt(const QString& cookie, TicketCallback cb);

    // Cookie validation through the system curl (Schannel) when the Qt stack got a
    // rejection or a network error. fallback is returned when curl is unavailable.
    void validateCookieViaCurl(const QString& cookie, CookieValidation fallback,
                               std::function<void(const CookieValidation&)> cb);

    // Inspect Set-Cookie on responses; emits cookieRotated when the value changes.
    void observeSetCookie(const QString& oldCookie, const HttpResponse& res);

    Http m_http;
    QHash<QString, QByteArray> m_csrf; // cookie-hash -> token
};

} // namespace ex::roblox

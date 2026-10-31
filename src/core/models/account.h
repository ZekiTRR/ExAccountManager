#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantHash>

namespace ex::model {

enum class PresenceType {
    Unknown = 0,
    Offline,
    Online,
    InGame,
    InStudio,
};

struct Presence {
    PresenceType type = PresenceType::Unknown;
    qint64 userId = 0;      // to match a presence API response
    qint64 placeId = 0;
    qint64 universeId = 0;
    QString lastLocation;   // game/place name
    QDateTime updated;

    static QString toString(PresenceType t)
    {
        switch (t) {
        case PresenceType::Offline: return QStringLiteral("Offline");
        case PresenceType::Online: return QStringLiteral("Online");
        case PresenceType::InGame: return QStringLiteral("InGame");
        case PresenceType::InStudio: return QStringLiteral("InStudio");
        default: return QStringLiteral("Unknown");
        }
    }
};

// Account moderation (based on RM: reason + expiry).
struct ModerationInfo {
    bool moderated = false;
    QString reason;
    QDateTime endTime;          // may be empty = forever
    qint64 durationSeconds = 0; // 0 = unknown
};

// Account health is computed, not stored (as in Astro).
enum class AccountHealth {
    Ok = 0,
    SessionExpired,     // cookie is dead
    NeedsReauth,        // not launched for a long time / needs attention
    Moderated,          // BAN
    Unknown,
};

struct Account {
    qint64 id = 0;              // row id
    qint64 userId = 0;          // Roblox UserId
    QString username;
    QString displayName;
    QString alias;
    QString description;
    qint64 groupId = 0;
    QString avatarUrl;          // headshot (cached separately)
    bool favorite = false;
    int sortOrder = 0;
    int priority = 0;           // 0..10 (Astro)
    QStringList tags;
    QVariantHash fields;        // arbitrary fields (RAM): Level, Gems, ...

    bool hasSession = false;    // the vault holds a cookie (the secret itself is not here)
    bool cookieExpired = false; // the last revalidation failed
    bool verified = false;      // e-mail/account is verified
    ModerationInfo moderation;
    QDateTime lastUsed;
    QDateTime lastValidated;

    // Saved launch target (RAM: SavedPlaceId/SavedJobId)
    qint64 savedPlaceId = 0;
    QString savedJobId;
    // Private server link code, when the target is VIP
    QString savedPrivateServerCode;

    // Private session data (never stored in the DB)
    Presence presence;

    QString displayNameOrUsername() const { return displayName.isEmpty() ? username : displayName; }
    QString aliasOrUsername() const { return alias.isEmpty() ? displayNameOrUsername() : alias; }

    AccountHealth health() const
    {
        if (moderation.moderated)
            return AccountHealth::Moderated;
        if (!hasSession || cookieExpired)
            return AccountHealth::SessionExpired;
        return AccountHealth::Ok;
    }

    QJsonObject toJson(bool includePrivate = false) const;
    static Account fromJson(const QJsonObject& o);
};

using AccountList = QList<Account>;

} // namespace ex::model

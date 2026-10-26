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
    qint64 userId = 0;      // для сопоставления ответа presence API
    qint64 placeId = 0;
    qint64 universeId = 0;
    QString lastLocation;   // название игры/локации
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

// Модерация аккаунта (по мотивам RM: причина + срок).
struct ModerationInfo {
    bool moderated = false;
    QString reason;
    QDateTime endTime;          // может быть пустой = навсегда
    qint64 durationSeconds = 0; // 0 = unknown
};

// Здоровье аккаунта — вычисляется, не хранится (как в Astro).
enum class AccountHealth {
    Ok = 0,
    SessionExpired,     // cookie мёртв
    NeedsReauth,        // не запускался давно / требует внимания
    Moderated,          // бан
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
    QString avatarUrl;          // headshot (кэшируется отдельно)
    bool favorite = false;
    int sortOrder = 0;
    int priority = 0;           // 0..10 (Astro)
    QStringList tags;
    QVariantHash fields;        // произвольные поля (RAM): Level, Gems, ...

    bool hasSession = false;    // в vault есть cookie (самого секрета тут нет)
    bool cookieExpired = false; // последняя ревалидация провалилась
    bool verified = false;      // верифицирован e-mail/аккаунт
    ModerationInfo moderation;
    QDateTime lastUsed;
    QDateTime lastValidated;

    // Сохранённое назначение запуска (RAM: SavedPlaceId/SavedJobId)
    qint64 savedPlaceId = 0;
    QString savedJobId;
    // Код приватного сервера (link code), если назначение — VIP
    QString savedPrivateServerCode;

    // Приватные сессионные данные (не хранятся в БД)
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

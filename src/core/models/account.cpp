#include "core/models/account.h"

#include <QJsonArray>

namespace ex::model {

QJsonObject Account::toJson(bool includePrivate) const
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("userId")] = userId;
    o[QStringLiteral("username")] = username;
    o[QStringLiteral("displayName")] = displayName;
    o[QStringLiteral("alias")] = alias;
    o[QStringLiteral("description")] = description;
    o[QStringLiteral("groupId")] = groupId;
    o[QStringLiteral("avatarUrl")] = avatarUrl;
    o[QStringLiteral("favorite")] = favorite;
    o[QStringLiteral("sortOrder")] = sortOrder;
    o[QStringLiteral("priority")] = priority;
    QJsonArray tagsArr;
    for (const QString& t : tags)
        tagsArr.append(t);
    o[QStringLiteral("tags")] = tagsArr;
    QJsonObject fieldsObj;
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
        fieldsObj[it.key()] = QJsonValue::fromVariant(it.value());
    o[QStringLiteral("fields")] = fieldsObj;
    o[QStringLiteral("hasSession")] = hasSession;
    o[QStringLiteral("cookieExpired")] = cookieExpired;
    o[QStringLiteral("verified")] = verified;
    if (moderation.moderated) {
        o[QStringLiteral("moderation")] = moderation.reason;
        if (moderation.endTime.isValid())
            o[QStringLiteral("moderationEnd")] = moderation.endTime.toString(Qt::ISODate);
    }
    if (lastUsed.isValid())
        o[QStringLiteral("lastUsed")] = lastUsed.toString(Qt::ISODate);
    if (lastValidated.isValid())
        o[QStringLiteral("lastValidated")] = lastValidated.toString(Qt::ISODate);
    o[QStringLiteral("savedPlaceId")] = savedPlaceId;
    o[QStringLiteral("savedJobId")] = savedJobId;
    o[QStringLiteral("savedPrivateServerCode")] = savedPrivateServerCode;
    if (includePrivate) {
        o[QStringLiteral("presence")] = Presence::toString(presence.type);
        o[QStringLiteral("lastLocation")] = presence.lastLocation;
    }
    return o;
}

Account Account::fromJson(const QJsonObject& o)
{
    Account a;
    a.id = o[QStringLiteral("id")].toInteger();
    a.userId = o[QStringLiteral("userId")].toInteger();
    a.username = o[QStringLiteral("username")].toString();
    a.displayName = o[QStringLiteral("displayName")].toString();
    a.alias = o[QStringLiteral("alias")].toString();
    a.description = o[QStringLiteral("description")].toString();
    a.groupId = o[QStringLiteral("groupId")].toInteger();
    a.avatarUrl = o[QStringLiteral("avatarUrl")].toString();
    a.favorite = o[QStringLiteral("favorite")].toBool();
    a.sortOrder = o[QStringLiteral("sortOrder")].toInt();
    a.priority = o[QStringLiteral("priority")].toInt();
    const QJsonArray tagsArr = o[QStringLiteral("tags")].toArray();
    for (const auto& v : tagsArr)
        a.tags.append(v.toString());
    const QJsonObject fieldsObj = o[QStringLiteral("fields")].toObject();
    for (auto it = fieldsObj.constBegin(); it != fieldsObj.constEnd(); ++it)
        a.fields.insert(it.key(), it.value().toVariant());
    a.hasSession = o[QStringLiteral("hasSession")].toBool();
    a.cookieExpired = o[QStringLiteral("cookieExpired")].toBool();
    a.verified = o[QStringLiteral("verified")].toBool();
    a.moderation.moderated = o.contains(QStringLiteral("moderation"));
    if (a.moderation.moderated) {
        a.moderation.reason = o[QStringLiteral("moderation")].toString();
        const QString end = o[QStringLiteral("moderationEnd")].toString();
        if (!end.isEmpty())
            a.moderation.endTime = QDateTime::fromString(end, Qt::ISODate);
    }
    const QString lu = o[QStringLiteral("lastUsed")].toString();
    if (!lu.isEmpty())
        a.lastUsed = QDateTime::fromString(lu, Qt::ISODate);
    const QString lv = o[QStringLiteral("lastValidated")].toString();
    if (!lv.isEmpty())
        a.lastValidated = QDateTime::fromString(lv, Qt::ISODate);
    a.savedPlaceId = o[QStringLiteral("savedPlaceId")].toInteger();
    a.savedJobId = o[QStringLiteral("savedJobId")].toString();
    a.savedPrivateServerCode = o[QStringLiteral("savedPrivateServerCode")].toString();
    return a;
}

} // namespace ex::model

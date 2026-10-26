#include "core/models/domain.h"

namespace ex::model {

QJsonObject Group::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("color")] = colorHex;
    o[QStringLiteral("collapsed")] = collapsed;
    o[QStringLiteral("sortOrder")] = sortOrder;
    return o;
}

Group Group::fromJson(const QJsonObject& o)
{
    Group g;
    g.id = o[QStringLiteral("id")].toInteger();
    g.name = o[QStringLiteral("name")].toString();
    g.colorHex = o[QStringLiteral("color")].toString();
    g.collapsed = o[QStringLiteral("collapsed")].toBool();
    g.sortOrder = o[QStringLiteral("sortOrder")].toInt();
    return g;
}

QJsonObject WindowProfile::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("enabled")] = enabled;
    o[QStringLiteral("x")] = x;
    o[QStringLiteral("y")] = y;
    o[QStringLiteral("width")] = width;
    o[QStringLiteral("height")] = height;
    o[QStringLiteral("monitor")] = monitor;
    o[QStringLiteral("state")] = static_cast<int>(state);
    return o;
}

WindowProfile WindowProfile::fromJson(const QJsonObject& o)
{
    WindowProfile w;
    w.enabled = o[QStringLiteral("enabled")].toBool();
    w.x = o[QStringLiteral("x")].toInt(-1);
    w.y = o[QStringLiteral("y")].toInt(-1);
    w.width = o[QStringLiteral("width")].toInt(-1);
    w.height = o[QStringLiteral("height")].toInt(-1);
    w.monitor = o[QStringLiteral("monitor")].toInt(0);
    w.state = static_cast<State>(o[QStringLiteral("state")].toInt(0));
    return w;
}

QJsonObject RamLimit::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("mode")] = static_cast<int>(mode);
    o[QStringLiteral("limitMb")] = static_cast<double>(limitMb);
    o[QStringLiteral("trimThresholdMb")] = static_cast<double>(trimThresholdMb);
    return o;
}

RamLimit RamLimit::fromJson(const QJsonObject& o)
{
    RamLimit r;
    r.mode = static_cast<Mode>(o[QStringLiteral("mode")].toInt(0));
    r.limitMb = static_cast<qint64>(o[QStringLiteral("limitMb")].toDouble(0));
    r.trimThresholdMb = static_cast<qint64>(o[QStringLiteral("trimThresholdMb")].toDouble(0));
    return r;
}

QJsonObject LaunchPreset::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("name")] = name;
    QJsonObject t;
    t[QStringLiteral("kind")] = static_cast<int>(target.kind);
    t[QStringLiteral("placeId")] = static_cast<double>(target.placeId);
    t[QStringLiteral("jobId")] = target.jobId;
    t[QStringLiteral("privateServerCode")] = target.privateServerCode;
    t[QStringLiteral("followUserId")] = static_cast<double>(target.followUserId);
    t[QStringLiteral("shareCode")] = target.shareCode;
    t[QStringLiteral("teleport")] = target.teleport;
    o[QStringLiteral("target")] = t;
    o[QStringLiteral("fpsCap")] = fpsCap;
    o[QStringLiteral("groupId")] = static_cast<double>(groupId);
    o[QStringLiteral("note")] = note;
    o[QStringLiteral("window")] = window.toJson();
    return o;
}

LaunchPreset LaunchPreset::fromJson(const QJsonObject& o)
{
    LaunchPreset p;
    p.id = o[QStringLiteral("id")].toInteger();
    p.name = o[QStringLiteral("name")].toString();
    const QJsonObject t = o[QStringLiteral("target")].toObject();
    p.target.kind = static_cast<LaunchTarget::Kind>(t[QStringLiteral("kind")].toInt(0));
    p.target.placeId = static_cast<qint64>(t[QStringLiteral("placeId")].toDouble(0));
    p.target.jobId = t[QStringLiteral("jobId")].toString();
    p.target.privateServerCode = t[QStringLiteral("privateServerCode")].toString();
    p.target.followUserId = static_cast<qint64>(t[QStringLiteral("followUserId")].toDouble(0));
    p.target.shareCode = t[QStringLiteral("shareCode")].toString();
    p.target.teleport = t[QStringLiteral("teleport")].toBool();
    p.fpsCap = o[QStringLiteral("fpsCap")].toInt(0);
    p.groupId = static_cast<qint64>(o[QStringLiteral("groupId")].toDouble(0));
    p.note = o[QStringLiteral("note")].toString();
    p.window = WindowProfile::fromJson(o[QStringLiteral("window")].toObject());
    return p;
}

QJsonObject PrivateServer::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("ownerId")] = static_cast<double>(ownerId);
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("placeId")] = static_cast<double>(placeId);
    o[QStringLiteral("code")] = code;
    if (added.isValid())
        o[QStringLiteral("added")] = added.toString(Qt::ISODate);
    o[QStringLiteral("gameName")] = gameName;
    o[QStringLiteral("gameIconUrl")] = gameIconUrl;
    return o;
}

PrivateServer PrivateServer::fromJson(const QJsonObject& o)
{
    PrivateServer p;
    p.id = o[QStringLiteral("id")].toInteger();
    p.ownerId = static_cast<qint64>(o[QStringLiteral("ownerId")].toDouble(0));
    p.name = o[QStringLiteral("name")].toString();
    p.placeId = static_cast<qint64>(o[QStringLiteral("placeId")].toDouble(0));
    p.code = o[QStringLiteral("code")].toString();
    const QString a = o[QStringLiteral("added")].toString();
    if (!a.isEmpty())
        p.added = QDateTime::fromString(a, Qt::ISODate);
    p.gameName = o[QStringLiteral("gameName")].toString();
    p.gameIconUrl = o[QStringLiteral("gameIconUrl")].toString();
    return p;
}

} // namespace ex::model

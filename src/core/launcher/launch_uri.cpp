#include "core/launcher/launch_uri.h"

#include "core/crypto/sodium.h"

#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

#include <cstring>

namespace ex::launcher {

namespace {

QString encode(const QString& value)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

bool isPrivateServerUuid(const QString& code)
{
    return code.size() == 36 && code.count(QLatin1Char('-')) == 4;
}

} // namespace

QString LaunchUri::newLaunchToken()
{
    // epoch-ms, строго возрастающий: клиент получает launchtime в командной
    // строке, по нему InstanceRegistry атрибутирует PID (приём RM).
    static QMutex mutex;
    static qint64 last = 0;
    QMutexLocker lock(&mutex);
    last = qMax(QDateTime::currentMSecsSinceEpoch(), last + 1);
    return QString::number(last);
}

qint64 LaunchUri::newBrowserTrackerId()
{
    unsigned char buf[8];
    ex::crypto::randomBytes(buf, sizeof buf);
    quint64 raw = 0;
    static_assert(sizeof raw == sizeof buf);
    std::memcpy(&raw, buf, sizeof raw);
    return 100000000LL + static_cast<qint64>(raw % 899999999ULL); // ~9 цифр
}

QString LaunchUri::build(const ex::model::LaunchTarget& target,
                         const QString& authTicket,
                         const QString& launchToken,
                         qint64 browserTrackerId)
{
    if (authTicket.isEmpty() || launchToken.isEmpty() || !target.isValid())
        return {};

    const QString ticketEnc = encode(authTicket);
    const QString tracker = QString::number(browserTrackerId);
    QString launcherUrl;

    switch (target.kind) {
    case ex::model::LaunchTarget::Kind::Place:
        launcherUrl = QStringLiteral(
            "https://assetgame.roblox.com/game/PlaceLauncher.ashx"
            "?request=RequestGame&browserTrackerId=%1&placeId=%2&isPlayTogetherGame=false")
            .arg(tracker, QString::number(target.placeId));
        break;
    case ex::model::LaunchTarget::Kind::Job:
        launcherUrl = QStringLiteral(
            "https://www.roblox.com/game/PlaceLauncher.ashx"
            "?request=RequestGameJob&browserTrackerId=%1&placeId=%2&gameId=%3&joinAttemptId=%4")
            .arg(tracker, QString::number(target.placeId), encode(target.jobId),
                 QUuid::createUuid().toString(QUuid::WithoutBraces));
        break;
    case ex::model::LaunchTarget::Kind::PrivateServer:
        launcherUrl = QStringLiteral(
            "https://assetgame.roblox.com/game/PlaceLauncher.ashx"
            "?request=RequestPrivateGame&browserTrackerId=%1&placeId=%2")
            .arg(tracker, QString::number(target.placeId));
        // UUID — это accessCode; короткий код — linkCode (форма Astro/RAM).
        launcherUrl += isPrivateServerUuid(target.privateServerCode)
            ? QStringLiteral("&accessCode=%1").arg(encode(target.privateServerCode))
            : QStringLiteral("&linkCode=%1").arg(encode(target.privateServerCode));
        break;
    case ex::model::LaunchTarget::Kind::FollowUser:
        launcherUrl = QStringLiteral(
            "https://assetgame.roblox.com/game/PlaceLauncher.ashx"
            "?request=RequestFollowUser&browserTrackerId=%1&userId=%2")
            .arg(tracker, QString::number(target.followUserId));
        break;
    case ex::model::LaunchTarget::Kind::ShareLink:
        // Share-ссылки резолвятся до place/private на уровне сервиса.
        return {};
    }

    if (target.teleport)
        launcherUrl += QStringLiteral("&isTeleport=true");

    const QString encodedUrl = encode(launcherUrl);
    return QStringLiteral(
        "roblox-player:1+launchmode:play+gameinfo:%1+launchtime:%2"
        "+placelauncherurl:%3+browsertrackerid:%4"
        "+robloxLocale:en_us+gameLocale:en_us+channel:+LaunchExp:InApp")
        .arg(ticketEnc, launchToken, encodedUrl, tracker);
}

ParsedInput LaunchUri::parseInput(const QString& input)
{
    ParsedInput out;
    const QString t = input.trimmed();
    if (t.isEmpty())
        return out;

    // Голое число — placeId.
    bool okNum = false;
    const qint64 num = t.toLongLong(&okNum);
    if (okNum && num > 0) {
        out.placeId = num;
        return out;
    }

    // roblox://experiences/start?placeId=...&gameInstanceId=...
    if (t.startsWith(QStringLiteral("roblox://"), Qt::CaseInsensitive)) {
        const QUrl url(t);
        const QUrlQuery q(url);
        out.placeId = q.queryItemValue(QStringLiteral("placeId")).toLongLong();
        out.jobId = q.queryItemValue(QStringLiteral("gameInstanceId"));
        if (out.jobId.isEmpty())
            out.jobId = q.queryItemValue(QStringLiteral("jobId"));
        return out;
    }

    // URL игры / VIP-ссылка / share-ссылка. QUrlQuery корректно разбирает
    // query и с якорем, и без него.
    const QUrl url(t, QUrl::TolerantMode);
    if (url.isValid() && url.host().contains(QStringLiteral("roblox.com"), Qt::CaseInsensitive)) {
        const QUrlQuery q(url);
        out.privateServerCode = q.queryItemValue(QStringLiteral("privateServerLinkCode"));
        out.jobId = q.queryItemValue(QStringLiteral("gameInstanceId"));
        if (out.jobId.isEmpty())
            out.jobId = q.queryItemValue(QStringLiteral("jobId"));
        if (url.path().startsWith(QStringLiteral("/share"), Qt::CaseInsensitive))
            out.shareCode = q.queryItemValue(QStringLiteral("code"));

        static const QRegularExpression gamesRx{
            QStringLiteral(R"(roblox\.com/(?:games|game-start)/(\d+))"),
            QRegularExpression::CaseInsensitiveOption};
        const auto m = gamesRx.match(t);
        if (m.hasMatch())
            out.placeId = m.captured(1).toLongLong();
        if (out.placeId > 0 || !out.shareCode.isEmpty() || !out.privateServerCode.isEmpty())
            return out;
    }

    // URL без схемы (www.roblox.com/...) — QUrl считает host только при схеме,
    // поэтому fallback-регэкспы.
    static const QRegularExpression vipRx{
        QStringLiteral(R"(roblox\.com/(?:games|share)/.*[?&]privateServerLinkCode=([A-Za-z0-9\-_]+))"),
        QRegularExpression::CaseInsensitiveOption};
    const auto vip = vipRx.match(t);
    if (vip.hasMatch())
        out.privateServerCode = vip.captured(1);

    static const QRegularExpression shareRx{
        QStringLiteral(R"(roblox\.com/share/?\?[^\s]*\bcode=([A-Za-z0-9\-_]{8,}))"),
        QRegularExpression::CaseInsensitiveOption};
    const auto share = shareRx.match(t);
    if (share.hasMatch())
        out.shareCode = share.captured(1);

    static const QRegularExpression gamesRx{
        QStringLiteral(R"(roblox\.com/(?:games|game-start)/(\d+))"),
        QRegularExpression::CaseInsensitiveOption};
    const auto game = gamesRx.match(t);
    if (game.hasMatch())
        out.placeId = game.captured(1).toLongLong();

    return out;
}

qint64 LaunchUri::parsePlaceInput(const QString& input, QString* outPrivateCode)
{
    const ParsedInput parsed = parseInput(input);
    if (outPrivateCode)
        *outPrivateCode = parsed.privateServerCode;
    return parsed.placeId;
}

} // namespace ex::launcher

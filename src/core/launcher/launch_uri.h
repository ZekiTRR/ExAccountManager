#pragma once

#include "core/models/domain.h"

#include <QString>

namespace ex::launcher {

// Разобранный ввод поля Place: id/URL/VIP/share/roblox://.
struct ParsedInput {
    qint64 placeId = 0;
    QString jobId;               // gameInstanceId из URL, если был
    QString privateServerCode;   // privateServerLinkCode
    QString shareCode;           // code из roblox.com/share

    bool hasAnyTarget() const { return placeId > 0 || !shareCode.isEmpty(); }
};

// Построение URI запуска roblox-player: (ядро, одинаковое у всех трёх менеджеров)
//
//   roblox-player:1+launchmode:play+gameinfo:<ticket>+launchtime:<ms>+
//                  placelauncherurl:<percent-encoded URL>+browsertrackerid:<id>
//                  +robloxLocale:en_us+gameLocale:en_us+channel:+LaunchExp:InApp
//
// Формат проверен RM (process.rs) и Astro (auth_tools.py):
//  - placelauncherurl кодируется целиком (включая ':' '/' '?' '&' '=');
//  - auth ticket тоже percent-encoded: в base64 бывают '+' '/' '=', которые
//    иначе ломают '+' -разделитель полей URI;
//  - launchtime — epoch-ms, строго возрастающий (атрибуция PID).
//
// PlaceLauncher.ashx параметры:
//   RequestGame:        browserTrackerId + placeId + isPlayTogetherGame=false
//   RequestGameJob:     browserTrackerId + placeId + gameId + joinAttemptId
//                       (форма веб-клиента; приём RM)
//   RequestPrivateGame: browserTrackerId + placeId + accessCode / linkCode
//   RequestFollowUser:  userId
class LaunchUri {
public:
    static QString newLaunchToken();
    static qint64 newBrowserTrackerId();

    static QString build(const ex::model::LaunchTarget& target,
                         const QString& authTicket,
                         const QString& launchToken,
                         qint64 browserTrackerId);

    // Полный разбор ввода (id, URL игры, VIP-ссылка, share-ссылка, roblox://).
    static ParsedInput parseInput(const QString& input);

    // Совместимость: placeId + код приватного сервера.
    static qint64 parsePlaceInput(const QString& input, QString* outPrivateCode = nullptr);
};

} // namespace ex::launcher

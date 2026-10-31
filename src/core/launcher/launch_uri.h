#pragma once

#include "core/models/domain.h"

#include <QString>

namespace ex::launcher {

// Parsed Place field input: id/URL/VIP/share/roblox://.
struct ParsedInput {
    qint64 placeId = 0;
    QString jobId;               // gameInstanceId from the URL, when present
    QString privateServerCode;   // privateServerLinkCode
    QString shareCode;           // code from roblox.com/share

    bool hasAnyTarget() const { return placeId > 0 || !shareCode.isEmpty(); }
};

// Builds the roblox-player: launch URI (the core shared by all three managers)
//
//   roblox-player:1+launchmode:play+gameinfo:<ticket>+launchtime:<ms>+
//                  placelauncherurl:<percent-encoded URL>+browsertrackerid:<id>
//                  +robloxLocale:en_us+gameLocale:en_us+channel:+LaunchExp:InApp
//
// Format verified by RM (process.rs) and Astro (auth_tools.py):
//  - placelauncherurl is encoded as a whole (':' '/' '?' '&' '=' too);
//  - the auth ticket is percent-encoded too: base64 may hold '+' '/' '=' which
//    otherwise they break the URI field's '+' separator;
//  - launchtime is epoch-ms and strictly increasing (PID attribution).
//
// PlaceLauncher.ashx parameters:
//   RequestGame:        browserTrackerId + placeId + isPlayTogetherGame=false
//   RequestGameJob:     browserTrackerId + placeId + gameId + joinAttemptId
//                       (web-client form; RM's approach)
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

    // Full parsing of the input (id, game URL, VIP link, share link, roblox://).
    static ParsedInput parseInput(const QString& input);

    // Compatibility: placeId + private server code.
    static qint64 parsePlaceInput(const QString& input, QString* outPrivateCode = nullptr);
};

} // namespace ex::launcher

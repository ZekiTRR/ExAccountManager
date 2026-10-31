#pragma once

#include <QJsonObject>
#include <QString>

namespace ex::model {

// ---------------------------------------------------------------- Groups
struct Group {
    qint64 id = 0;
    QString name;
    QString colorHex;     // "#RRGGBB" or empty
    bool collapsed = false;
    int sortOrder = 0;

    QJsonObject toJson() const;
    static Group fromJson(const QJsonObject& o);
};

// ---------------------------------------------------------------- Window profile [feature #2]
// Arbitrary size/position for the Roblox client window from the first frame.
struct WindowProfile {
    bool enabled = false;
    int x = -1, y = -1;          // -1 = unset (center / as-is)
    int width = -1, height = -1; // -1 = unset
    int monitor = 0;             // monitor index (0 = primary)
    enum class State {
        Normal = 0,
        Minimized,
        Hidden,      // hide without focus (for background clients)
        Maximized,
    } state = State::Normal;

    bool isTrivial() const { return !enabled; }
    QJsonObject toJson() const;
    static WindowProfile fromJson(const QJsonObject& o);
};

// ---------------------------------------------------------------- RAM limit [feature #1]
struct RamLimit {
    enum class Mode {
        Off = 0,
        Soft,   // working-set trimming + potato + adaptive FPS
        Hard,   // hard working-set maximum (paging/freezes)
    } mode = Mode::Off;
    qint64 limitMb = 0;          // 0 = unset
    qint64 trimThresholdMb = 0;  // for Soft: trim once the working set exceeds

    QJsonObject toJson() const;
    static RamLimit fromJson(const QJsonObject& o);
};

// ---------------------------------------------------------------- Launch target
// Where to launch the account. Pure data: a plan is not an action.
struct LaunchTarget {
    enum class Kind {
        Place = 0,        // just a placeId (random/best server)
        Job,              // a specific server (placeId + jobId)
        PrivateServer,    // VIP by accessCode/linkCode
        FollowUser,       // join a player's server (userId)
        ShareLink,        // roblox.com/share?code=... (resolved through the API)
    } kind = Kind::Place;

    qint64 placeId = 0;
    QString jobId;
    QString privateServerCode;   // accessCode or linkCode
    qint64 followUserId = 0;
    QString shareCode;
    bool teleport = false;       // isTeleport flag in the URI

    bool isValid() const
    {
        switch (kind) {
        case Kind::Place: return placeId > 0;
        case Kind::Job: return placeId > 0 && !jobId.isEmpty();
        case Kind::PrivateServer: return placeId > 0 && !privateServerCode.isEmpty();
        case Kind::FollowUser: return followUserId > 0;
        case Kind::ShareLink: return !shareCode.isEmpty();
        }
        return false;
    }

    QString describe() const
    {
        switch (kind) {
        case Kind::Place: return QStringLiteral("place %1").arg(placeId);
        case Kind::Job: return QStringLiteral("job %1/%2").arg(placeId).arg(jobId);
        case Kind::PrivateServer: return QStringLiteral("private %1").arg(placeId);
        case Kind::FollowUser: return QStringLiteral("follow %1").arg(followUserId);
        case Kind::ShareLink: return QStringLiteral("share %1").arg(shareCode);
        }
        return QStringLiteral("invalid");
    }
};

// ---------------------------------------------------------------- Launch preset
// Launch preset (RM: files on disk; Astro: launch profile).
struct LaunchPreset {
    qint64 id = 0;
    QString name;
    LaunchTarget target;
    int fpsCap = 0;              // 0 = leave unchanged
    qint64 groupId = 0;          // 0 = all
    QString note;
    WindowProfile window;

    QJsonObject toJson() const;
    static LaunchPreset fromJson(const QJsonObject& o);
};

// ---------------------------------------------------------------- VIP server [feature #4]
struct PrivateServer {
    qint64 id = 0;
    qint64 ownerId = 0;          // owning account (who holds the VIP)
    QString name;                // human-readable
    qint64 placeId = 0;
    QString code;                // privateServerLinkCode or accessCode
    QDateTime added;
    QString gameName;            // game name cache
    QString gameIconUrl;         // icon cache

    QJsonObject toJson() const;
    static PrivateServer fromJson(const QJsonObject& o);
};

// ---------------------------------------------------------------- Instance
// A running client (or one about to start).
struct InstanceInfo {
    enum class State {
        Launching = 0,  // launch requested, waiting for the process
        Running,        // process exists and is attributed
        Unattributed,   // process exists, account unknown
        Crashed,        // crashed
        Stopped,
    } state = State::Launching;

    qint64 accountId = 0;
    QString accountUsername;
    qint64 pid = 0;
    qint64 processCreateTimeMs = 0; // PID-reuse protection
    QString launchToken;            // launchtime token from the command line
    bool attributionExact = false;  // false = Inferred (best guess)
    QDateTime startedAt;
    QDateTime lastSeenAt;

    // Telemetry (refreshed by the monitor)
    double cpuPercent = 0.0;
    qint64 workingSetMb = 0;
    qint64 privateBytesMb = 0;
    QString gameName;
    qint64 placeId = 0;
    QString jobId;
};

} // namespace ex::model

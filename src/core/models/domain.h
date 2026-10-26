#pragma once

#include <QJsonObject>
#include <QString>

namespace ex::model {

// ---------------------------------------------------------------- Groups
struct Group {
    qint64 id = 0;
    QString name;
    QString colorHex;     // "#RRGGBB" или пусто
    bool collapsed = false;
    int sortOrder = 0;

    QJsonObject toJson() const;
    static Group fromJson(const QJsonObject& o);
};

// ---------------------------------------------------------------- Window profile [feature №2]
// Произвольный размер/позиция окна клиента Roblox с самого запуска.
struct WindowProfile {
    bool enabled = false;
    int x = -1, y = -1;          // -1 = не задано (центр/как есть)
    int width = -1, height = -1; // -1 = не задано
    int monitor = 0;             // индекс монитора (0 = primary)
    enum class State {
        Normal = 0,
        Minimized,
        Hidden,      // спрятано без фокуса (для фоновых)
        Maximized,
    } state = State::Normal;

    bool isTrivial() const { return !enabled; }
    QJsonObject toJson() const;
    static WindowProfile fromJson(const QJsonObject& o);
};

// ---------------------------------------------------------------- RAM limit [feature №1]
struct RamLimit {
    enum class Mode {
        Off = 0,
        Soft,   // тримминг working set + potato + адаптивный FPS
        Hard,   // жёсткий максимум рабочего набора (подкачка/фризы)
    } mode = Mode::Off;
    qint64 limitMb = 0;          // 0 = не задано
    qint64 trimThresholdMb = 0;  // для Soft: триммить, когда working set выше

    QJsonObject toJson() const;
    static RamLimit fromJson(const QJsonObject& o);
};

// ---------------------------------------------------------------- Launch target
// Куда запускать аккаунт. Чистые данные: план ≠ действие.
struct LaunchTarget {
    enum class Kind {
        Place = 0,        // просто placeId (случайный/лучший сервер)
        Job,              // конкретный сервер (placeId + jobId)
        PrivateServer,    // VIP по accessCode/linkCode
        FollowUser,       // зайти в сервер игрока (userId)
        ShareLink,        // roblox.com/share?code=... (резолвится API)
    } kind = Kind::Place;

    qint64 placeId = 0;
    QString jobId;
    QString privateServerCode;   // accessCode или linkCode
    qint64 followUserId = 0;
    QString shareCode;
    bool teleport = false;       // флаг isTeleport в URI

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
// Пресет запуска (RM: файлы на диске; Astro: launch profile).
struct LaunchPreset {
    qint64 id = 0;
    QString name;
    LaunchTarget target;
    int fpsCap = 0;              // 0 = не менять
    qint64 groupId = 0;          // 0 = все
    QString note;
    WindowProfile window;

    QJsonObject toJson() const;
    static LaunchPreset fromJson(const QJsonObject& o);
};

// ---------------------------------------------------------------- VIP server [feature №4]
struct PrivateServer {
    qint64 id = 0;
    qint64 ownerId = 0;          // аккаунт-владелец (у кого VIP)
    QString name;                // человекочитаемое
    qint64 placeId = 0;
    QString code;                // privateServerLinkCode или accessCode
    QDateTime added;
    QString gameName;            // кэш названия игры
    QString gameIconUrl;         // кэш иконки

    QJsonObject toJson() const;
    static PrivateServer fromJson(const QJsonObject& o);
};

// ---------------------------------------------------------------- Instance
// Запущенный клиент (или ожидаемый к запуску).
struct InstanceInfo {
    enum class State {
        Launching = 0,  // запрошен запуск, ждём процесс
        Running,        // процесс есть и атрибутирован
        Unattributed,   // процесс есть, аккаунт неизвестен
        Crashed,        // упал
        Stopped,
    } state = State::Launching;

    qint64 accountId = 0;
    QString accountUsername;
    qint64 pid = 0;
    qint64 processCreateTimeMs = 0; // защита от переиспользования PID
    QString launchToken;            // launchtime-токен из командной строки
    bool attributionExact = false;  // false = Inferred (best guess)
    QDateTime startedAt;
    QDateTime lastSeenAt;

    // Телеметрия (обновляется монитором)
    double cpuPercent = 0.0;
    qint64 workingSetMb = 0;
    qint64 privateBytesMb = 0;
    QString gameName;
    qint64 placeId = 0;
    QString jobId;
};

} // namespace ex::model

#pragma once

#include "core/models/domain.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QTimer>

namespace ex::platform { struct ProcessInfo; }

namespace ex::instances {

// Registry mapping PID -> account.
// Two-way attribution:
//  - Exact: launchtime token read from the command line (PEB) matched
//    matched against the token we wrote into the launch URI (RM's approach);
//  - Inferred: fallback by appearance order (marked as a best guess,
//    killing by it is FORBIDDEN).
// Key (pid, create_time) — guards against PID reuse.
class InstanceRegistry : public QObject {
    Q_OBJECT
public:
    explicit InstanceRegistry(QObject* parent = nullptr);

    // Register an expected launch (before the URI goes to ShellExecute,
    // so a fast client is never an orphan — a race taken from Astro).
    void expectLaunch(qint64 accountId, const QString& launchToken);

    // Drop the expectation (the client never appeared).
    void giveUpLaunch(qint64 accountId, const QString& launchToken);

    // One sweep tick: matches live Roblox processes against expectations/attributions.
    void sweep(const QList<ex::platform::ProcessInfo>& liveProcesses);

    QList<ex::model::InstanceInfo> instances() const;
    // Refresh an attributed process's telemetry (CPU/RAM from the monitor).
    void updateTelemetry(qint64 pid, double cpuPercent, qint64 workingSetMb,
                         qint64 privateBytesMb);
    std::optional<ex::model::InstanceInfo> byAccount(qint64 accountId) const;
    std::optional<qint64> accountIdForPid(qint64 pid) const;

signals:
    void instanceStarted(const ex::model::InstanceInfo& info);   // attribution obtained
    void instanceCrashed(const ex::model::InstanceInfo& info);   // process disappeared
    void instanceUnattributed(qint64 pid);                        // orphan
    void instancesChanged();

private:
    struct Pending {
        qint64 accountId = 0;
        QString launchToken;
        QDateTime since;
    };
    struct Entry {
        ex::model::InstanceInfo info;
        QDateTime lastSeen;
    };

    QHash<QString, Pending> m_pending;     // launchToken -> pending
    QHash<qint64, Entry> m_byPid;          // pid -> entry
    QHash<qint64, qint64> m_accountToPid;  // accountId -> pid

    static constexpr int kPendingTimeoutMs = 60000;   // 60 s for the process to appear
    static constexpr int kInferredTtlMs = 45000;      // TTL of inferred attributions (RM)
    static constexpr int kExactTtlMs = 300000;        // TTL exact (RM)
};

} // namespace ex::instances

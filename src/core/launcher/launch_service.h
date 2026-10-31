#pragma once

#include "core/instances/instance_registry.h"
#include "core/models/domain.h"
#include "core/roblox/api.h"
#include "core/store/account_store.h"
#include "core/store/settings.h"
#include "core/store/vault.h"

#include <QHash>
#include <QObject>
#include <QQueue>
#include <QTimer>

namespace ex::launcher {

// Options for a single launch.
struct LaunchOptions {
    ex::model::WindowProfile window;     // [#2]
    ex::model::RamLimit ramLimit;        // [#1]
    bool closePreviousInstance = true;   // close this account's previous instance (RAM)
    bool rememberWindowPosition = false; // remember the position on exit
    // Roblox picked in settings (exe or folder). Empty = the Windows handler.
    QString robloxPath;
};

// Bulk launch mode [#3].
enum class BatchMode {
    SameServer,       // everyone on one server (jobId given, or best public)
    DifferentServers, // each on its own random/least-filled server
    PrivateServer,    // everyone on the VIP server (by link code)
};

struct BatchRequest {
    QList<qint64> accountIds;
    ex::model::LaunchTarget target;   // base assignment
    BatchMode mode = BatchMode::SameServer;
    LaunchOptions options;
    int concurrency = 3;              // concurrent launches
    int delaySeconds = 4;             // pause between launches (anti rate-limit)
};

// Result of one launch (emitted as a signal).
struct LaunchResult {
    qint64 accountId = 0;
    bool ok = false;
    QString error;
    QString launchToken;
};

// Launch service: queue + ticket + URI + attribution + window/RAM application.
class LaunchService : public QObject {
    Q_OBJECT
public:
    LaunchService(ex::roblox::RobloxApi& api,
                  ex::store::AccountStore& store,
                  ex::store::Vault& vault,
                  ex::instances::InstanceRegistry& registry,
                  QObject* parent = nullptr);
    ~LaunchService() override;

    // Single launch.
    void launch(qint64 accountId, const ex::model::LaunchTarget& target,
                const LaunchOptions& options = {});

    // Bulk launch [#3].
    void launchBatch(const BatchRequest& request);

    // Close an account's instance (kill-verified).
    bool closeInstance(qint64 accountId, bool graceful = false);

    // Close all ATTRIBUTED clients (foreign processes stay untouched).
    int closeAll();

    void cancelQueue();

    bool isQueueEmpty() const { return m_queue.isEmpty() && m_active == 0; }

signals:
    void launchQueued(qint64 accountId);
    void launchFinished(const ex::launcher::LaunchResult& result);
    void batchProgress(int done, int total);
    void batchFinished(int okCount, int failCount);
    // 401 from Roblox: the account cookie no longer works.
    void sessionInvalid(qint64 accountId, const QString& error);

private:
    struct Job {
        qint64 accountId = 0;
        ex::model::LaunchTarget target;
        LaunchOptions options;
        bool randomServer = false; // DifferentServers: pick a server to launch into
    };

    void enqueueJobs(QList<Job> jobs);
    void pumpQueue();
    void runJob(const Job& job);
    void finishJob(const Job& job, bool ok, const QString& error, const QString& token);

    ex::roblox::RobloxApi& m_api;
    ex::store::AccountStore& m_store;
    ex::store::Vault& m_vault;
    ex::instances::InstanceRegistry& m_registry;

    QQueue<Job> m_queue;
    QTimer m_delayTimer;
    int m_active = 0;
    int m_concurrency = 1;
    int m_delayMs = 0;
    int m_batchDone = 0;
    int m_batchOk = 0;
    int m_batchFail = 0;
    int m_batchTotal = 0;
    QHash<qint64, qint64> m_trackerIds; // accountId -> browserTrackerId
    QHash<qint64, ex::model::LaunchTarget> m_launchTargets;
    QHash<qint64, ex::model::WindowProfile> m_launchWindows;
    QHash<qint64, qint64> m_ledgerIds;  // accountId -> session_ledger.id
};

} // namespace ex::launcher

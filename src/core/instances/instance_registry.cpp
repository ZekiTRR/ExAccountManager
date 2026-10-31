#include "core/instances/instance_registry.h"

#include "core/util/log.h"
#include "platform/windows/process.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QSet>

#include <optional>

namespace ex::instances {

InstanceRegistry::InstanceRegistry(QObject* parent)
    : QObject(parent)
{
}

void InstanceRegistry::expectLaunch(qint64 accountId, const QString& launchToken)
{
    Pending p;
    p.accountId = accountId;
    p.launchToken = launchToken;
    p.since = QDateTime::currentDateTime();
    m_pending.insert(launchToken, p);
    EXLOG_DEBUG("registry") << "expecting launch, account" << accountId;
}

void InstanceRegistry::giveUpLaunch(qint64 accountId, const QString& launchToken)
{
    Q_UNUSED(accountId);
    m_pending.remove(launchToken);
}

void InstanceRegistry::sweep(const QList<ex::platform::ProcessInfo>& liveProcesses)
{
    const QDateTime now = QDateTime::currentDateTime();

    // 1) Find processes whose command line carries one of our tokens.
    QSet<qint64> livePids;
    for (const auto& proc : liveProcesses)
        livePids.insert(proc.pid);

    for (const auto& proc : liveProcesses) {
        // Already attributed and alive — refresh lastSeen.
        auto it = m_byPid.find(proc.pid);
        if (it != m_byPid.end() && it->info.processCreateTimeMs == proc.createTimeMs) {
            it->lastSeen = now;
            it->info.lastSeenAt = now;
            it->info.state = ex::model::InstanceInfo::State::Running;
            continue;
        }

        // New process: read its command line through the PEB and look for launchtime:TOKEN.
        const QString cmdline = ex::platform::processCommandLine(proc.pid);
        // URI format: ...+launchtime:123456789+...
        static const QRegularExpression rx{QStringLiteral(R"(launchtime:(\d+))")};
        const auto m = rx.match(cmdline);
        if (!m.hasMatch())
            continue;
        const QString token = m.captured(1);

        auto pit = m_pending.find(token);
        if (pit != m_pending.end()) {
            // Exact attribution!
            Entry e;
            e.info.accountId = pit->accountId;
            e.info.pid = proc.pid;
            e.info.processCreateTimeMs = proc.createTimeMs;
            e.info.launchToken = token;
            e.info.attributionExact = true;
            e.info.state = ex::model::InstanceInfo::State::Running;
            e.info.startedAt = pit->since;
            e.info.lastSeenAt = now;
            m_byPid.insert(proc.pid, e);
            m_accountToPid.insert(pit->accountId, proc.pid);
            m_pending.erase(pit);
            EXLOG_INFO("registry") << "exact attribution: pid" << proc.pid
                                    << "account" << e.info.accountId;
            emit instanceStarted(e.info);
            emit instancesChanged();
        }
        // Nothing pending — a foreign/old client, leave it (Unattributed).
    }

    // 2) Gone processes: mark or remove.
    QList<qint64> deadPids;
    for (auto it = m_byPid.begin(); it != m_byPid.end(); ++it) {
        if (!livePids.contains(it.key()))
            deadPids.append(it.key());
    }
    for (qint64 pid : deadPids) {
        Entry e = m_byPid.take(pid);
        m_accountToPid.remove(e.info.accountId);
        e.info.state = ex::model::InstanceInfo::State::Crashed;
        EXLOG_INFO("registry") << "process gone: pid" << pid << "account" << e.info.accountId;
        emit instanceCrashed(e.info);
        emit instancesChanged();
    }

    // 3) Expired pending entries.
    QStringList expired;
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it)
        if (it->since.msecsTo(now) > kPendingTimeoutMs)
            expired.append(it.key());
    for (const QString& tok : expired) {
        EXLOG_WARN("registry") << "pending launch timed out, token" << tok;
        m_pending.remove(tok);
    }
}

QList<ex::model::InstanceInfo> InstanceRegistry::instances() const
{
    QList<ex::model::InstanceInfo> out;
    for (const auto& e : m_byPid)
        out.append(e.info);
    return out;
}

void InstanceRegistry::updateTelemetry(qint64 pid, double cpuPercent, qint64 workingSetMb,
                                       qint64 privateBytesMb)
{
    auto it = m_byPid.find(pid);
    if (it == m_byPid.end())
        return;
    it->info.cpuPercent = cpuPercent;
    it->info.workingSetMb = workingSetMb;
    it->info.privateBytesMb = privateBytesMb;
    it->info.lastSeenAt = QDateTime::currentDateTime();
}

std::optional<ex::model::InstanceInfo> InstanceRegistry::byAccount(qint64 accountId) const
{
    const qint64 pid = m_accountToPid.value(accountId, 0);
    if (!pid)
        return std::nullopt;
    auto it = m_byPid.constFind(pid);
    if (it == m_byPid.constEnd())
        return std::nullopt;
    return it->info;
}

std::optional<qint64> InstanceRegistry::accountIdForPid(qint64 pid) const
{
    auto it = m_byPid.constFind(pid);
    if (it == m_byPid.constEnd())
        return std::nullopt;
    return it->info.accountId;
}

} // namespace ex::instances

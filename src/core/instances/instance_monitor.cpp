#include "core/instances/instance_monitor.h"

#include "core/util/log.h"
#include "platform/windows/process.h"
#include "platform/windows/ram_limits.h"

#include <windows.h>

#include <QDateTime>

namespace ex::instances {

InstanceMonitor::InstanceMonitor(InstanceRegistry& registry, QObject* parent)
    : QObject(parent)
    , m_registry(registry)
{
    m_timer.setInterval(6000); // как Astro
    connect(&m_timer, &QTimer::timeout, this, &InstanceMonitor::refresh);
}

void InstanceMonitor::setIntervalMs(int ms)
{
    m_timer.setInterval(qMax(1000, ms));
}

void InstanceMonitor::start()
{
    m_timer.start();
    refresh();
    // Второй замер через секунду — сразу получить дельту CPU (первый = 0).
    QTimer::singleShot(1200, this, &InstanceMonitor::refresh);
}

void InstanceMonitor::stop()
{
    m_timer.stop();
}

void InstanceMonitor::refresh()
{
    const QList<ex::platform::ProcessInfo> procs =
        ex::platform::processesByName(QStringLiteral("RobloxPlayerBeta.exe"));
    const int before = m_registry.instances().size();
    m_registry.sweep(procs);
    if (m_registry.instances().size() > before) {
        // Первый замер нового инстанса не имеет дельты CPU — добираем через сек.
        QTimer::singleShot(1200, this, &InstanceMonitor::refresh);
    }

    // Телеметрия: CPU% как дельта (kernel+user) времени процесса, RAM — working set.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const QList<ex::model::InstanceInfo> instances = m_registry.instances();
    for (const auto& snapshot : instances) {
        ex::model::InstanceInfo info = snapshot;
        // Проверка валидности кэша (PID мог быть переиспользован)
        qint64 cachedCreate = m_lastCreateTime.value(info.pid, -1);
        if (cachedCreate != -1 && cachedCreate != info.processCreateTimeMs) {
            m_lastCpuTimes.remove(info.pid);
            m_lastCreateTime.remove(info.pid);
        }

        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                               static_cast<DWORD>(info.pid));
        if (h) {
            FILETIME ftCreate{}, ftExit{}, ftKernel{}, ftUser{};
            if (GetProcessTimes(h, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
                const qint64 total100ns =
                    (static_cast<qint64>(ftKernel.dwHighDateTime) << 32 | ftKernel.dwLowDateTime)
                    + (static_cast<qint64>(ftUser.dwHighDateTime) << 32 | ftUser.dwLowDateTime);
                auto it = m_lastCpuTimes.constFind(info.pid);
                if (it != m_lastCpuTimes.constEnd() && nowMs > it.value().second) {
                    const qint64 dTime = total100ns - it.value().first;
                    const qint64 dWall = (nowMs - it.value().second) * 10000; // ms -> 100ns
                    if (dWall > 0)
                        info.cpuPercent = qMin(100.0, 100.0 * static_cast<double>(dTime) / static_cast<double>(dWall));
                }
                m_lastCpuTimes.insert(info.pid, {total100ns, nowMs});
                m_lastCreateTime.insert(info.pid, info.processCreateTimeMs);
            }
            CloseHandle(h);
        }
        qint64 ws = 0, priv = 0;
        if (ex::platform::RamLimiter::queryMemory(info.pid, ws, priv)) {
            info.workingSetMb = ws;
            info.privateBytesMb = priv;
        }
        // Раньше обновлялась локальная копия — в UI всегда были нули.
        m_registry.updateTelemetry(info.pid, info.cpuPercent, info.workingSetMb,
                                   info.privateBytesMb);
    }
    emit telemetryUpdated();
}

} // namespace ex::instances

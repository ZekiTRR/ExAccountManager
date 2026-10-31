#pragma once

#include "core/instances/instance_registry.h"

#include <QTimer>

namespace ex::instances {

// Periodic monitor of Roblox clients: process sweep -> attribution registry,
// CPU/RAM telemetry [#6], events. Settings: interval comes from settings
// (6 s by default — as in Astro).
class InstanceMonitor : public QObject {
    Q_OBJECT
public:
    explicit InstanceMonitor(InstanceRegistry& registry, QObject* parent = nullptr);

    void setIntervalMs(int ms);
    void start();
    void stop();

    // Refresh telemetry (CPU% from process-time deltas, RAM from
    // queryMemory) and hand out a fresh list.
    void refresh();

signals:
    void telemetryUpdated();

private:
    InstanceRegistry& m_registry;
    QTimer m_timer;
    QHash<qint64, std::pair<qint64, qint64>> m_lastCpuTimes; // pid -> (kernel+user, timestamp)
    QHash<qint64, qint64> m_lastCreateTime;                  // pid -> create_time (validation)
};

} // namespace ex::instances

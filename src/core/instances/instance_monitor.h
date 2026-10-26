#pragma once

#include "core/instances/instance_registry.h"

#include <QTimer>

namespace ex::instances {

// Периодический монитор клиентов Roblox: свип процессов -> реестр атрибуции,
// телеметрия CPU/RAM [№6], события. Настройки: интервал из settings
// (по умолчанию 6 с — как в Astro).
class InstanceMonitor : public QObject {
    Q_OBJECT
public:
    explicit InstanceMonitor(InstanceRegistry& registry, QObject* parent = nullptr);

    void setIntervalMs(int ms);
    void start();
    void stop();

    // Обновить телеметрию (CPU% через дельту process times, RAM через
    // queryMemory) и раздать свежий список.
    void refresh();

signals:
    void telemetryUpdated();

private:
    InstanceRegistry& m_registry;
    QTimer m_timer;
    QHash<qint64, std::pair<qint64, qint64>> m_lastCpuTimes; // pid -> (kernel+user, timestamp)
    QHash<qint64, qint64> m_lastCreateTime;                  // pid -> create_time (валидация)
};

} // namespace ex::instances

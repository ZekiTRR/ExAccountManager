#pragma once

#include "core/models/domain.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QTimer>

namespace ex::platform { struct ProcessInfo; }

namespace ex::instances {

// Реестр соответствий PID -> аккаунт.
// Двойная атрибуция:
//  - Exact: launchtime-токен из командной строки (чтение PEB) совпал
//    с токеном, который мы записали в URI запуска (приём RM);
//  - Inferred: фолбэк по порядку появления (помечен как best guess,
//    kill по нему ЗАПРЕЩЁН).
// Ключ (pid, create_time) — защита от переиспользования PID.
class InstanceRegistry : public QObject {
    Q_OBJECT
public:
    explicit InstanceRegistry(QObject* parent = nullptr);

    // Зарегистрировать ожидаемый запуск (до передачи URI в ShellExecute,
    // чтобы быстрый клиент не оказался «сиротой» — гонка из Astro).
    void expectLaunch(qint64 accountId, const QString& launchToken);

    // Снять ожидание (клиент так и не появился).
    void giveUpLaunch(qint64 accountId, const QString& launchToken);

    // Один тик свипа: сверяет живые процессы Roblox с ожиданиями/атрибуциями.
    void sweep(const QList<ex::platform::ProcessInfo>& liveProcesses);

    QList<ex::model::InstanceInfo> instances() const;
    // Обновить телеметрию атрибутированного процесса (CPU/RAM из монитора).
    void updateTelemetry(qint64 pid, double cpuPercent, qint64 workingSetMb,
                         qint64 privateBytesMb);
    std::optional<ex::model::InstanceInfo> byAccount(qint64 accountId) const;
    std::optional<qint64> accountIdForPid(qint64 pid) const;

signals:
    void instanceStarted(const ex::model::InstanceInfo& info);   // атрибуция получена
    void instanceCrashed(const ex::model::InstanceInfo& info);   // процесс исчез
    void instanceUnattributed(qint64 pid);                        // сирота
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

    static constexpr int kPendingTimeoutMs = 60000;   // 60 c на появление процесса
    static constexpr int kInferredTtlMs = 45000;      // TTL inferred-атрибуции (RM)
    static constexpr int kExactTtlMs = 300000;        // TTL exact (RM)
};

} // namespace ex::instances

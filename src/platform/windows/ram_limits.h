#pragma once

#include <QString>

namespace ex::platform {

// [Feature №1] Лимиты потребления ОЗУ/CPU клиентами Roblox.
//
// ОЗУ: жёсткий максимум рабочего набора через SetProcessWorkingSetSizeEx.
// Не требует Job Object (Roblox-клиент часто уже находится в своём job),
// не роняет аллокации: лишние страницы вытесняются в pagefile.
//
// CPU: Job Object CPU rate control. Если процесс уже в чужом job и назначение
// невозможно — мягкий режим: пониженный приоритет + EcoQoS.
class RamLimiter {
public:
    struct LimitReport {
        bool memory = false;   // лимит рабочего набора применён/снят
        bool cpuHard = false;  // CPU ограничен через Job Object
        bool cpuSoft = false;  // CPU ограничен мягко (приоритет/EcoQoS)

        [[nodiscard]] bool any() const
        {
            return memory || cpuHard || cpuSoft;
        }
    };

    // Тримминг working set процесса (мягкий режим). Возвращает false при ошибке.
    static bool trimWorkingSet(qint64 pid);

    // Текущий working set и private bytes (МБ).
    static bool queryMemory(qint64 pid, qint64& workingSetMb, qint64& privateMb);

    // Жёсткий лимит ОЗУ (рабочий набор). Не требует Job Object.
    static bool applyHardLimit(qint64 pid, qint64 memoryMb);

    // Лимиты CPU/ОЗУ. cpuPercent=0 и/или memoryMb=0 означают «без ограничения».
    // ОЗУ ограничивается всегда (working set), CPU — Job Object с мягким
    // фолбэком, если процесс уже в чужом job. Handle job удерживается до
    // clearLimits (имя/лимиты job без handle не снять).
    static LimitReport applyLimits(qint64 pid, int cpuPercent, qint64 memoryMb);
    // Снять лимиты CPU/ОЗУ и вернуть приоритет/EcoQoS к прежним значениям.
    static bool clearLimits(qint64 pid);

    // CPU: приоритет процесса (для performance-режима).
    static bool lowerPriority(qint64 pid);

    // Снять лимит: завершить job (процесс продолжает жить).
    static bool removeHardLimit(qint64 pid);
};

} // namespace ex::platform

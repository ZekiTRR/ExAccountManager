#pragma once

#include <QString>

namespace ex::platform {

// [Feature #1] RAM/CPU limits for the Roblox client.
//
// RAM: hard working-set maximum through SetProcessWorkingSetSizeEx.
// Needs no Job Object (the Roblox client often already sits in its own job),
// does not fail allocations: extra pages are evicted to the pagefile.
//
// CPU: Job Object CPU rate control. When the process is already in a foreign job and
// assignment is impossible — soft mode: lower priority + EcoQoS.
class RamLimiter {
public:
    struct LimitReport {
        bool memory = false;   // working-set limit applied / cleared
        bool cpuHard = false;  // CPU limited through a Job Object
        bool cpuSoft = false;  // CPU limited softly (priority/EcoQoS)

        [[nodiscard]] bool any() const
        {
            return memory || cpuHard || cpuSoft;
        }
    };

    // Trim the process working set (soft mode). Returns false on error.
    static bool trimWorkingSet(qint64 pid);

    // Current working set and private bytes (MB).
    static bool queryMemory(qint64 pid, qint64& workingSetMb, qint64& privateMb);

    // Hard RAM limit (working set). Needs no Job Object.
    static bool applyHardLimit(qint64 pid, qint64 memoryMb);

    // CPU/RAM limits. cpuPercent=0 and/or memoryMb=0 mean "unlimited".
    // RAM is always limited (working set); CPU uses a Job Object with a soft
    // with a fallback when the process is already in a foreign job. The job handle is kept
    // clearLimits (a job's name/limits cannot be lifted without its handle).
    static LimitReport applyLimits(qint64 pid, int cpuPercent, qint64 memoryMb);
    // Clear CPU/RAM limits and restore the previous priority/EcoQoS.
    static bool clearLimits(qint64 pid);

    // CPU: process priority (for performance mode).
    static bool lowerPriority(qint64 pid);

    // Clear the limit: terminate the job (the process keeps running).
    static bool removeHardLimit(qint64 pid);
};

} // namespace ex::platform

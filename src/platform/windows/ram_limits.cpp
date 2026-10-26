#include "platform/windows/ram_limits.h"

#include "core/util/log.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>

#include <windows.h>
#include <psapi.h>

namespace ex::platform {

bool RamLimiter::trimWorkingSet(qint64 pid)
{
    HANDLE h = OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_INFORMATION,
                           FALSE, static_cast<DWORD>(pid));
    if (!h)
        return false;
    // EmptyWorkingSet вытесняет страницы, не сбрасывая жёсткий лимит
    // (SetProcessWorkingSetSizeEx(-1,-1,0) удалил бы его).
    const BOOL ok = EmptyWorkingSet(h);
    CloseHandle(h);
    if (!ok)
        EXLOG_DEBUG("ram") << "trim failed for pid" << pid << "err" << GetLastError();
    return ok == TRUE;
}

bool RamLimiter::queryMemory(qint64 pid, qint64& workingSetMb, qint64& privateMb)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h) {
        // Фолбэк для систем, где LIMITED недостаточно для GetProcessMemoryInfo.
        h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE,
                        static_cast<DWORD>(pid));
    }
    if (!h)
        return false;

    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof pmc;
    const bool ok = GetProcessMemoryInfo(
                        h, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof pmc)
        == TRUE;
    CloseHandle(h);
    if (!ok)
        return false;
    workingSetMb = static_cast<qint64>(pmc.WorkingSetSize / (1024 * 1024));
    privateMb = static_cast<qint64>(pmc.PrivateUsage / (1024 * 1024));
    return true;
}

namespace {

// CPU-job на процесс. Handle держим открытым: без него лимит не снять.
QHash<qint64, HANDLE> g_jobs;

struct SoftCpuState {
    DWORD priority = NORMAL_PRIORITY_CLASS;
    bool hadThrottle = false;
    PROCESS_POWER_THROTTLING_STATE throttle{};
};
QHash<qint64, SoftCpuState> g_softCpu;

QMutex g_mutex;

bool processGone(DWORD error)
{
    return error == ERROR_INVALID_PARAMETER || error == ERROR_NOT_FOUND;
}

bool setWorkingSetLimit(qint64 pid, qint64 memoryMb)
{
    HANDLE proc = OpenProcess(
        PROCESS_SET_QUOTA | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
        static_cast<DWORD>(pid));
    if (!proc) {
        const DWORD err = GetLastError();
        if (!processGone(err))
            EXLOG_WARN("limits") << "OpenProcess(WS) failed for pid" << pid
                                 << ":" << err;
        return processGone(err);
    }
    const SIZE_T max = static_cast<SIZE_T>(memoryMb) * 1024 * 1024;
    const SIZE_T min = qMin<SIZE_T>(max, 1024 * 1024);
    SetLastError(0);
    const BOOL ok = SetProcessWorkingSetSizeEx(
        proc, min, max,
        QUOTA_LIMITS_HARDWS_MAX_ENABLE | QUOTA_LIMITS_HARDWS_MIN_DISABLE);
    const DWORD err = GetLastError();
    CloseHandle(proc);
    if (!ok)
        EXLOG_WARN("limits") << "SetProcessWorkingSetSizeEx failed for pid"
                             << pid << ":" << err;
    return ok == TRUE;
}

bool clearWorkingSetLimit(qint64 pid)
{
    HANDLE proc = OpenProcess(
        PROCESS_SET_QUOTA | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
        static_cast<DWORD>(pid));
    if (!proc) {
        const DWORD err = GetLastError();
        return processGone(err);
    }
    // Флаги MAX_DISABLE/MIN_DISABLE действительно снимают жёсткие пределы;
    // (-1, -1, 0) только триммит и оставляет hard max в силе.
    SetLastError(0);
    const BOOL ok = SetProcessWorkingSetSizeEx(
        proc, 1024 * 1024, 64 * 1024 * 1024,
        QUOTA_LIMITS_HARDWS_MAX_DISABLE | QUOTA_LIMITS_HARDWS_MIN_DISABLE);
    CloseHandle(proc);
    return ok == TRUE;
}

bool softenCpu(qint64 pid)
{
    HANDLE proc = OpenProcess(
        PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
        static_cast<DWORD>(pid));
    if (!proc)
        return false;

    SoftCpuState previous;
    if (const DWORD priority = GetPriorityClass(proc))
        previous.priority = priority;
    previous.throttle.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    if (GetProcessInformation(proc, ProcessPowerThrottling,
                              &previous.throttle, sizeof previous.throttle)) {
        previous.hadThrottle = true;
    }

    const bool priorityOk =
        SetPriorityClass(proc, BELOW_NORMAL_PRIORITY_CLASS) != FALSE;
    PROCESS_POWER_THROTTLING_STATE eco{};
    eco.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    eco.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    eco.StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    const bool ecoOk =
        SetProcessInformation(proc, ProcessPowerThrottling,
                              &eco, sizeof eco) != FALSE;
    CloseHandle(proc);

    if (!priorityOk && !ecoOk)
        return false;

    QMutexLocker lock(&g_mutex);
    g_softCpu.insert(pid, previous);
    return true;
}

bool restoreSoftCpu(qint64 pid)
{
    SoftCpuState previous;
    {
        QMutexLocker lock(&g_mutex);
        if (!g_softCpu.contains(pid))
            return true;
        previous = g_softCpu.take(pid);
    }

    HANDLE proc = OpenProcess(
        PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
        static_cast<DWORD>(pid));
    if (!proc)
        return true; // процесс уже завершился — восстанавливать нечего

    const bool priorityOk =
        SetPriorityClass(proc, previous.priority) != FALSE;
    PROCESS_POWER_THROTTLING_STATE target{};
    if (previous.hadThrottle) {
        target = previous.throttle;
    } else {
        target.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        target.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        target.StateMask = 0;
    }
    const bool throttleOk =
        SetProcessInformation(proc, ProcessPowerThrottling,
                              &target, sizeof target) != FALSE;
    CloseHandle(proc);
    return priorityOk && throttleOk;
}

bool applyCpuJob(qint64 pid, int cpuPercent)
{
    QMutexLocker lock(&g_mutex);
    HANDLE job = g_jobs.value(pid, nullptr);
    const bool created = !job;
    if (!job) {
        job = CreateJobObjectW(nullptr, nullptr);
        if (!job) {
            EXLOG_WARN("limits") << "CreateJobObject failed:" << GetLastError();
            return false;
        }
        g_jobs.insert(pid, job);
    }
    const auto fail = [pid, job, created]() {
        if (created) {
            g_jobs.remove(pid);
            CloseHandle(job);
        }
    };

    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpu{};
    cpu.ControlFlags = JOB_OBJECT_CPU_RATE_CONTROL_ENABLE
        | JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;
    cpu.CpuRate = static_cast<DWORD>(cpuPercent * 100); // 0..10000
    if (!SetInformationJobObject(job, JobObjectCpuRateControlInformation,
                                 &cpu, sizeof cpu)) {
        EXLOG_WARN("limits") << "SetInformationJobObject(CPU) failed:"
                             << GetLastError();
        fail();
        return false;
    }

    HANDLE proc = OpenProcess(
        PROCESS_SET_QUOTA | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE, static_cast<DWORD>(pid));
    if (!proc) {
        fail();
        return false;
    }

    BOOL alreadyInJob = FALSE;
    IsProcessInJob(proc, job, &alreadyInJob);
    BOOL assigned = TRUE;
    if (!alreadyInJob) {
        assigned = AssignProcessToJobObject(proc, job);
        if (!assigned)
            EXLOG_WARN("limits") << "AssignProcessToJobObject failed for pid"
                                 << pid << ":" << GetLastError();
    }
    CloseHandle(proc);
    if (!assigned) {
        fail();
        return false;
    }
    return true;
}

bool clearCpuJob(qint64 pid)
{
    QMutexLocker lock(&g_mutex);
    HANDLE job = g_jobs.take(pid);
    if (!job)
        return true;

    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpu{};
    const BOOL ok = SetInformationJobObject(
        job, JobObjectCpuRateControlInformation, &cpu, sizeof cpu);
    if (!ok) {
        g_jobs.insert(pid, job);
        EXLOG_WARN("limits") << "clear CPU job failed for pid" << pid
                             << ":" << GetLastError();
        return false;
    }
    CloseHandle(job);
    return true;
}

} // namespace

RamLimiter::LimitReport RamLimiter::applyLimits(qint64 pid, int cpuPercent,
                                                qint64 memoryMb)
{
    LimitReport report;
    if (pid <= 0 || cpuPercent < 0 || cpuPercent > 100 || memoryMb < 0)
        return report;

    // ОЗУ не зависит от Job Object: лимит рабочего набора работает даже
    // внутри чужого job. 0 снимает прежний лимит.
    report.memory = memoryMb > 0 ? setWorkingSetLimit(pid, memoryMb)
                                 : clearWorkingSetLimit(pid);

    if (cpuPercent > 0) {
        report.cpuHard = applyCpuJob(pid, cpuPercent);
        if (!report.cpuHard) {
            report.cpuSoft = softenCpu(pid);
            EXLOG_INFO("limits") << "CPU job unavailable for pid" << pid
                                 << "— soft mode" << report.cpuSoft;
        }
    } else {
        clearCpuJob(pid);
        restoreSoftCpu(pid);
    }

    EXLOG_INFO("limits") << "applied pid" << pid << "cpu" << cpuPercent
                         << "% ram" << memoryMb << "MB"
                         << "hard" << report.cpuHard
                         << "soft" << report.cpuSoft;
    return report;
}

bool RamLimiter::applyHardLimit(qint64 pid, qint64 memoryMb)
{
    if (pid <= 0 || memoryMb < 0)
        return false;
    return memoryMb > 0 ? setWorkingSetLimit(pid, memoryMb)
                        : clearWorkingSetLimit(pid);
}

bool RamLimiter::clearLimits(qint64 pid)
{
    if (pid <= 0)
        return false;
    const bool memoryCleared = clearWorkingSetLimit(pid);
    const bool cpuCleared = clearCpuJob(pid);
    const bool softRestored = restoreSoftCpu(pid);
    const bool ok = memoryCleared && cpuCleared && softRestored;
    EXLOG_INFO("limits") << "cleared pid" << pid << "ok" << ok;
    return ok;
}

bool RamLimiter::lowerPriority(qint64 pid)
{
    HANDLE h = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION,
                           FALSE, static_cast<DWORD>(pid));
    if (!h)
        return false;
    const BOOL ok = SetPriorityClass(h, BELOW_NORMAL_PRIORITY_CLASS);
    CloseHandle(h);
    return ok == TRUE;
}

bool RamLimiter::removeHardLimit(qint64 pid)
{
    return clearLimits(pid);
}

} // namespace ex::platform

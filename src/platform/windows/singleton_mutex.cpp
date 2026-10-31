#include "platform/windows/singleton_mutex.h"

#include "core/util/log.h"

#include <windows.h>

namespace ex::platform {

namespace {
struct WaitCtx {
    HANDLE mutex = nullptr;
    volatile bool* holding = nullptr;
    volatile bool* stop = nullptr;
};

DWORD WINAPI waitThreadProc(LPVOID lp)
{
    auto* ctx = static_cast<WaitCtx*>(lp);
    // Honest queue: wait for the mutex while Roblox holds it. Once the client
    // releases it — WaitForSingleObject returns and ownership becomes ours.
    while (!*ctx->stop) {
        const DWORD r = WaitForSingleObject(ctx->mutex, 500);
        if (r == WAIT_OBJECT_0) {
            *ctx->holding = true;
            EXLOG_INFO("mutex") << "singleton mutex acquired after wait";
            delete ctx;
            return 0;
        }
        if (r == WAIT_ABANDONED) {
            // Roblox crashed without releasing the mutex — take the abandoned one.
            *ctx->holding = true;
            EXLOG_INFO("mutex") << "singleton mutex adopted (abandoned)";
            delete ctx;
            return 0;
        }
    }
    delete ctx;
    return 0;
}
} // namespace

SingletonMutex::~SingletonMutex()
{
    release();
}

SingletonMutex::AcquireResult SingletonMutex::acquire()
{
    if (m_holding)
        return AcquireResult::Acquired;

    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"ROBLOX_singletonMutex");
    if (!mutex) {
        EXLOG_WARN("mutex") << "CreateMutexW failed:" << GetLastError();
        return AcquireResult::Error;
    }
    const DWORD err = GetLastError();

    if (err == ERROR_ALREADY_EXISTS) {
        // Roblox is already running and holds the mutex. Check whether it is free now.
        const DWORD r = WaitForSingleObject(mutex, 0);
        if (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED) {
            m_mutex = mutex;
            m_holding = true;
            EXLOG_INFO("mutex") << "singleton mutex acquired immediately";
            return AcquireResult::Acquired;
        }
        // Wait in the queue honestly (the user must close Roblox).
        m_mutex = mutex;
        auto* ctx = new WaitCtx{mutex, &m_holding, &m_stopWait};
        m_waitThread = CreateThread(nullptr, 0, waitThreadProc, ctx, 0, nullptr);
        EXLOG_INFO("mutex") << "roblox running; waiting in queue for singleton mutex";
        return AcquireResult::AlreadyRunning;
    }

    m_mutex = mutex;
    const DWORD r = WaitForSingleObject(mutex, 0);
    m_holding = (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED);
    return m_holding ? AcquireResult::Acquired : AcquireResult::Error;
}

void SingletonMutex::release()
{
    m_stopWait = true;
    if (m_waitThread) {
        WaitForSingleObject(m_waitThread, 2000);
        CloseHandle(m_waitThread);
        m_waitThread = nullptr;
    }
    if (m_mutex) {
        if (m_holding)
            ReleaseMutex(m_mutex);
        CloseHandle(m_mutex);
        m_mutex = nullptr;
    }
    m_holding = false;
}

} // namespace ex::platform

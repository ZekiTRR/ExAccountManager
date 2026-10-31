#pragma once

#include <QString>

namespace ex::platform {

// Holding the global Roblox mutex (ROBLOX_singletonMutex) enables
// multi-instance. Astro's approach: one dedicated thread OWNS the mutex
// (CreateMutexW + WaitForSingleObject); never closes foreign handles
// in Roblox processes — closing foreign handles crashed the client with
// ERROR_INVALID_HANDLE.
//
// Usage:
//   SingletonMutex m;
//   auto res = m.acquire(); // see AcquireResult
//   ...
//   m.release();            // or the destructor
class SingletonMutex {
public:
    enum class AcquireResult {
        Acquired,       // we own it — multi-instance works
        AlreadyRunning, // Roblox is already running; wait in the queue honestly
        Error,
    };

    SingletonMutex() = default;
    ~SingletonMutex();

    SingletonMutex(const SingletonMutex&) = delete;
    SingletonMutex& operator=(const SingletonMutex&) = delete;

    AcquireResult acquire();
    void release();
    bool holding() const { return m_holding; }

private:
    void* m_mutex = nullptr;  // HANDLE
    void* m_waitThread = nullptr;
    volatile bool m_holding = false;
    volatile bool m_stopWait = false;
};

} // namespace ex::platform

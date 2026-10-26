#pragma once

#include <QString>

namespace ex::platform {

// Владение глобальным мьютексом Roblox (ROBLOX_singletonMutex) — включает
// мультиинстанс. Подход Astro: выделенный поток ВЛАДЕЕТ мьютексом честно
// (CreateMutexW + WaitForSingleObject), никогда не закрывает чужие хэндлы
// в процессах Roblox — закрытие чужих хэндлов валило клиент с
// ERROR_INVALID_HANDLE.
//
// Использование:
//   SingletonMutex m;
//   auto res = m.acquire(); // см. AcquireResult
//   ...
//   m.release();            // или деструктор
class SingletonMutex {
public:
    enum class AcquireResult {
        Acquired,       // мы владеем — мультиинстанс работает
        AlreadyRunning, // Roblox уже запущен; честно ждём в очереди
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

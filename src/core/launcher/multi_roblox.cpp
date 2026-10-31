#include "core/launcher/multi_roblox.h"

#include "core/util/log.h"

#include <QTimer>

namespace ex::launcher {

namespace {
constexpr int kPollWaitingMs = 2000;
}

MultiRoblox::MultiRoblox(QObject* parent)
    : QObject(parent)
{
    m_poll = new QTimer(this);
    m_poll->setInterval(kPollWaitingMs);
    connect(m_poll, &QTimer::timeout, this, &MultiRoblox::pollWaiting);
}

MultiRoblox::State MultiRoblox::setEnabled(bool on)
{
    m_enabled = on;
    if (!on) {
        release();
        EXLOG_INFO("multiroblox") << "disabled; singleton mutex released";
        return m_state;
    }

    const auto result = m_mutex.acquire();
    switch (result) {
    case ex::platform::SingletonMutex::AcquireResult::Acquired:
        setState(State::Holding);
        EXLOG_INFO("multiroblox") << "ROBLOX_singletonMutex held — multi-instance enabled";
        break;
    case ex::platform::SingletonMutex::AcquireResult::AlreadyRunning:
        // A client owns the mutex; the platform thread hands it to us later.
        setState(State::Waiting);
        m_poll->start();
        EXLOG_INFO("multiroblox") << "roblox is running; will take the mutex when it exits";
        break;
    case ex::platform::SingletonMutex::AcquireResult::Error:
        setState(State::Error);
        EXLOG_WARN("multiroblox") << "cannot acquire ROBLOX_singletonMutex";
        break;
    }
    return m_state;
}

void MultiRoblox::release()
{
    m_poll->stop();
    m_mutex.release();
    setState(State::Off);
}

void MultiRoblox::pollWaiting()
{
    if (!m_mutex.holding())
        return;
    m_poll->stop();
    setState(State::Holding);
    EXLOG_INFO("multiroblox") << "mutex taken over; multi-instance active";
}

void MultiRoblox::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged();
}

} // namespace ex::launcher

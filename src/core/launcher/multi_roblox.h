#pragma once

#include "platform/windows/singleton_mutex.h"

#include <QObject>

class QTimer;

namespace ex::launcher {

// "Multi Roblox" (RAM's EnableMultiRbx, RM's multi_instance_enabled): the manager
// takes ROBLOX_singletonMutex before any client starts, so the client never becomes
// the singleton owner and several clients can run side by side.
//
// Opt-in: the trick is visible to Roblox's anti-cheat, so the default is off and the
// UI states the trade-off. While Roblox itself holds the mutex we queue for it and
// flip to Holding as soon as the last client exits (see SingletonMutex).
class MultiRoblox : public QObject {
    Q_OBJECT
public:
    enum class State {
        Off,        // preference disabled, mutex released
        Holding,    // we own ROBLOX_singletonMutex — multi-instance is active
        Waiting,    // a Roblox client owns it; we take over once it exits
        Error,      // the OS refused the mutex
    };

    explicit MultiRoblox(QObject* parent = nullptr);

    bool enabled() const { return m_enabled; }
    State state() const { return m_state; }
    // True when clients can be opened in parallel right now.
    bool active() const { return m_state == State::Holding; }

    // Applies the preference immediately and returns the resulting state.
    State setEnabled(bool on);
    // Gives the mutex back without changing the preference (safe to repeat).
    void release();

signals:
    void stateChanged();

private:
    void pollWaiting();
    void setState(State state);

    ex::platform::SingletonMutex m_mutex;
    QTimer* m_poll = nullptr;
    State m_state = State::Off;
    bool m_enabled = false;
};

} // namespace ex::launcher

#pragma once

#include "core/models/domain.h"

#include <QList>
#include <QRect>

namespace ex::platform {

// Roblox client windows: position/size [feature #2], hiding
// without stealing focus, grid auto-layout (RM), title renaming.
class WindowMgr {
public:
    // Apply a WindowProfile to the process's main window.
    // state=Hidden uses ShowWindow(SW_HIDE) — hidden, not minimized.
    static bool applyProfile(qint64 pid, const ex::model::WindowProfile& profile);

    // Show/hide without stealing focus (SW_SHOW_NO_ACTIVATE / SW_HIDE).
    static bool showNoActivate(qint64 pid);
    static bool hide(qint64 pid);

    // Minimize via PostMessage WM_SYSCOMMAND/SC_MINIMIZE —
    // does not activate the window (RAM's approach).
    static bool minimizeNoFocus(qint64 pid);

    // Focus a window (with the AttachThreadInput fallback RM uses).
    static bool focus(qint64 pid);

    // Window title: read/set (renamed to the account name).
    static QString title(qint64 pid);
    static bool setTitle(qint64 pid, const QString& title);

    // Auto-arrange the PID list into a grid over the virtual desktop
    // (1 = whole screen, 2 = side by side, 3 = 2+1, 4 = 2x2, ...). Offsets
    // invisible DWM borders (RM's approach).
    static bool arrangeGrid(const QList<qint64>& pids);

    // Arrange windows in a grid with an arbitrary cell size (e.g. 100x100).
    // Windows run left to right and wrap onto the next row.
    static bool arrangeGridSized(const QList<qint64>& pids, int width, int height,
                                 int gap = 8);

    // Resize a window while keeping its current position.
    static bool resizeWindow(qint64 pid, int width, int height);

    // Current rect of the process's main window.
    static QRect windowRect(qint64 pid);

    // Primary monitor work area (excluding the taskbar).
    static QRect primaryWorkArea();
};

} // namespace ex::platform

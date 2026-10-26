#include "platform/windows/window_mgr.h"

#include "platform/windows/process.h"

#include <windows.h>

#include <QList>
#include <QRect>

namespace ex::platform {

namespace {
HWND hwndFor(qint64 pid)
{
    return reinterpret_cast<HWND>(ex::platform::findMainWindow(pid));
}

struct MonCtx {
    int wantedIndex = 0;
    int currentIndex = 0;
    RECT work{};      // рабочая область (без панели задач)
    bool found = false;
};

BOOL CALLBACK monitorEnum(HMONITOR mon, HDC, LPRECT, LPARAM lp)
{
    auto* ctx = reinterpret_cast<MonCtx*>(lp);
    MONITORINFO mi{};
    mi.cbSize = sizeof mi;
    if (GetMonitorInfoW(mon, &mi)) {
        if (ctx->currentIndex == ctx->wantedIndex) {
            ctx->work = mi.rcWork;
            ctx->found = true;
            return FALSE;
        }
        ++ctx->currentIndex;
    }
    return TRUE;
}

RECT workAreaForMonitor(int monitorIndex)
{
    MonCtx ctx;
    ctx.wantedIndex = monitorIndex;
    EnumDisplayMonitors(nullptr, nullptr, monitorEnum, reinterpret_cast<LPARAM>(&ctx));
    if (!ctx.found) {
        RECT wa{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        return wa;
    }
    return ctx.work;
}
} // namespace

bool WindowMgr::applyProfile(qint64 pid, const ex::model::WindowProfile& p)
{
    if (p.isTrivial())
        return true;
    HWND hwnd = hwndFor(pid);
    if (!hwnd)
        return false;

    const RECT mon = workAreaForMonitor(p.monitor);

    int x = p.x, y = p.y;
    int w = p.width > 0 ? p.width : -1;
    int h = p.height > 0 ? p.height : -1;

    RECT cur{};
    GetWindowRect(hwnd, &cur);
    if (w < 0)
        w = cur.right - cur.left;
    if (h < 0)
        h = cur.bottom - cur.top;
    if (x < 0)
        x = mon.left + ((mon.right - mon.left) - w) / 2;
    if (y < 0)
        y = mon.top + ((mon.bottom - mon.top) - h) / 2;

    // Ограничить размер рабочей областью монитора
    if (w > mon.right - mon.left) w = mon.right - mon.left;
    if (h > mon.bottom - mon.top) h = mon.bottom - mon.top;

    // SWP_NOSENDCHANGING обходит WM_GETMINMAXINFO/WM_WINDOWPOSCHANGING:
    // Roblox иначе не даёт уменьшить окно ниже своего минимума.
    SetWindowPos(hwnd, nullptr, x, y, w, h,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING);

    switch (p.state) {
    case ex::model::WindowProfile::State::Minimized:
        PostMessageW(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
        break;
    case ex::model::WindowProfile::State::Hidden:
        ShowWindow(hwnd, SW_HIDE);
        break;
    case ex::model::WindowProfile::State::Maximized:
        ShowWindow(hwnd, SW_MAXIMIZE);
        break;
    case ex::model::WindowProfile::State::Normal:
        break;
    }
    return true;
}

bool WindowMgr::showNoActivate(qint64 pid)
{
    HWND hwnd = hwndFor(pid);
    if (!hwnd)
        return false;
    if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_RESTORE);
    else if (!IsWindowVisible(hwnd))
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    return true;
}

bool WindowMgr::hide(qint64 pid)
{
    HWND hwnd = hwndFor(pid);
    if (!hwnd)
        return false;
    return ShowWindow(hwnd, SW_HIDE) != FALSE;
}

bool WindowMgr::minimizeNoFocus(qint64 pid)
{
    HWND hwnd = hwndFor(pid);
    if (!hwnd)
        return false;
    return PostMessageW(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0) != FALSE;
}

bool WindowMgr::focus(qint64 pid)
{
    HWND hwnd = hwndFor(pid);
    if (!hwnd)
        return false;
    // Скрытое окно сначала показываем, свёрнутое — восстанавливаем.
    if (!IsWindowVisible(hwnd))
        ShowWindow(hwnd, SW_SHOW);
    else if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_RESTORE);
    BringWindowToTop(hwnd);
    if (SetForegroundWindow(hwnd))
        return true;

    // Фолбэк из RM: присоединить input-поток к foreground-потоку и повторить.
    const DWORD fgThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const DWORD thisThread = GetCurrentThreadId();
    if (fgThread && fgThread != thisThread) {
        AttachThreadInput(thisThread, fgThread, TRUE);
        const BOOL ok = SetForegroundWindow(hwnd);
        AttachThreadInput(thisThread, fgThread, FALSE);
        return ok != FALSE;
    }
    return false;
}

QString WindowMgr::title(qint64 pid)
{
    HWND hwnd = hwndFor(pid);
    if (!hwnd)
        return {};
    wchar_t buf[256]{};
    GetWindowTextW(hwnd, buf, 256);
    return QString::fromWCharArray(buf);
}

bool WindowMgr::setTitle(qint64 pid, const QString& title)
{
    HWND hwnd = hwndFor(pid);
    if (!hwnd)
        return false;
    return SetWindowTextW(hwnd, reinterpret_cast<const wchar_t*>(title.utf16())) != FALSE;
}

bool WindowMgr::arrangeGrid(const QList<qint64>& pids)
{
    if (pids.isEmpty())
        return false;

    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    const int sw = wa.right - wa.left;
    const int sh = wa.bottom - wa.top;

    const int n = pids.size();
    const int cols = n <= 1 ? 1 : n <= 4 ? 2 : n <= 9 ? 3 : 4;
    const int rows = (n + cols - 1) / cols;

    const int cellW = sw / cols;
    const int cellH = sh / rows;

    int placed = 0;
    for (int r = 0; r < rows; ++r) {
        const int inRow = qMin(cols, n - r * cols);
        for (int c = 0; c < inRow; ++c) {
            HWND hwnd = hwndFor(pids[placed]);
            if (hwnd) {
                // Свёрнутое/максимизированное окно не примет новый rect,
                // пока не восстановлено; скрытое — не видно.
                if (IsIconic(hwnd) || IsZoomed(hwnd))
                    ShowWindow(hwnd, SW_RESTORE);
                if (!IsWindowVisible(hwnd))
                    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
                // Центрировать последнюю неполную строку (приём RM)
                const int rowOffset = (r == rows - 1 && inRow < cols)
                                          ? (cols - inRow) * cellW / 2 : 0;
                const int x = wa.left + c * cellW + rowOffset;
                const int y = wa.top + r * cellH;
                SetWindowPos(hwnd, nullptr, x, y, cellW, cellH,
                             SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW
                                 | SWP_NOSENDCHANGING);
            }
            ++placed;
        }
    }
    return true;
}

bool WindowMgr::arrangeGridSized(const QList<qint64>& pids, int width, int height,
                                 int gap)
{
    if (pids.isEmpty() || width <= 0 || height <= 0)
        return false;

    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    const int workW = wa.right - wa.left;
    const int workH = wa.bottom - wa.top;
    const int cellW = qBound(64, width, qMax(64, workW));
    const int cellH = qBound(64, height, qMax(64, workH));
    const int gapPx = qMax(0, gap);
    const int cols = qMax(1, (workW + gapPx) / (cellW + gapPx));

    int placed = 0;
    for (qint64 pid : pids) {
        HWND hwnd = hwndFor(pid);
        if (!hwnd)
            continue;
        if (IsIconic(hwnd) || IsZoomed(hwnd))
            ShowWindow(hwnd, SW_RESTORE);
        if (!IsWindowVisible(hwnd))
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        const int row = placed / cols;
        const int col = placed % cols;
        const int x = wa.left + col * (cellW + gapPx);
        const int y = wa.top + row * (cellH + gapPx);
        SetWindowPos(hwnd, nullptr, x, y, cellW, cellH,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW
                         | SWP_NOSENDCHANGING);
        ++placed;
    }
    return placed > 0;
}

bool WindowMgr::resizeWindow(qint64 pid, int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;
    HWND hwnd = hwndFor(pid);
    if (!hwnd)
        return false;
    if (IsIconic(hwnd) || IsZoomed(hwnd))
        ShowWindow(hwnd, SW_RESTORE);
    RECT rc{};
    if (!GetWindowRect(hwnd, &rc))
        return false;
    return SetWindowPos(hwnd, nullptr, rc.left, rc.top, width, height,
                        SWP_NOZORDER | SWP_NOACTIVATE
                            | SWP_NOSENDCHANGING) != FALSE;
}

QRect WindowMgr::windowRect(qint64 pid)
{
    HWND hwnd = hwndFor(pid);
    if (!hwnd)
        return {};
    RECT rc{};
    if (!GetWindowRect(hwnd, &rc))
        return {};
    return QRect(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
}

QRect WindowMgr::primaryWorkArea()
{
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    return QRect(wa.left, wa.top, wa.right - wa.left, wa.bottom - wa.top);
}

} // namespace ex::platform

#pragma once

#include <QString>
#include <QList>

struct _FILETIME;

namespace ex::platform {

// Process snapshot taken from a Toolhelp snapshot.
struct ProcessInfo {
    qint64 pid = 0;
    qint64 parentPid = 0;
    QString name;            // e.g. "RobloxPlayerBeta.exe"
    qint64 createTimeMs = 0; // Creation FILETIME in ms since epoch — guards against PID reuse
};

// Enumerate processes by name (case-insensitive): "RobloxPlayerBeta.exe".
QList<ProcessInfo> processesByName(const QString& nameLower);

// Process command line via a PEB read (NtQueryInformationProcess +
// ReadProcessMemory). Toolhelp/WMIC cannot do it for the Hyperion-protected client,
// refuse to give it — only the direct PEB path (from RM).
QString processCommandLine(qint64 pid);

// Verified kill: re-checks the image name and (optionally)
// launchToken in the command line through ONE handle, then only
// TerminateProcess. Never kills a process that failed the checks.
bool killVerified(qint64 pid, const QString& expectedImageLower,
                  const QString& expectedTokenInCmdline = QString());

// Soft close (CloseMainWindow equivalent: WM_CLOSE/WM_QUIT).
bool requestGracefulClose(qint64 pid);

// Process window: the largest visible top-level window of this PID.
qint64 findMainWindow(qint64 pid);

// Open a URI through ShellExecute (roblox-player:/roblox:// protocols).
bool openUri(const QString& uri);

// Same, but when elevated the URI opens through the explorer token
// (the Roblox client dislikes running elevated). Falls back to openUri.
bool openUriDeElevated(const QString& uri);

// Start a specific RobloxPlayerBeta.exe with the launch URI (no injection).
// When elevated, the de-elevated --launch-player helper is used;
// fallback is a direct start. Working dir = client folder (like a shortcut).
bool launchPlayerDeElevated(const QString& exePath, const QString& uri);

// Clear RobloxCookies.dat before launching the client (RM's approach:
// %LOCALAPPDATA%\Roblox\LocalStorage\RobloxCookies.dat). The file is truncated to
// zero so Roblox does not link accounts across launches.
// Returns true when the file is absent or cleared successfully.
bool clearRobloxCookieFile();

// FILETIME (100 ns ticks since 1601) -> unix ms.
qint64 fileTimeToMs(const _FILETIME& ft);
} // namespace ex::platform

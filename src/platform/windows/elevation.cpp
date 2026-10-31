#include "platform/windows/elevation.h"

#include "core/util/log.h"

#include <windows.h>

namespace ex::platform {

bool isElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elev{};
    DWORD size = 0;
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elev, sizeof elev, &size);
    CloseHandle(token);
    return ok && elev.TokenIsElevated != 0;
}

namespace {
// Command line in Windows form: program + escaped arguments.
QString buildCommandLine(const QString& exe, const QStringList& args)
{
    QString cmd = QStringLiteral("\"%1\"").arg(exe);
    for (const QString& a : args) {
        if (a.isEmpty() || a.contains(QLatin1Char(' '))) {
            QString quoted = a;
            quoted.replace(QLatin1Char('\"'), QStringLiteral("\\\""));
            cmd += QStringLiteral(" \"%1\"").arg(quoted);
        } else {
            cmd += QLatin1Char(' ') + a;
        }
    }
    return cmd;
}
} // namespace

void* launchDeelevated(const QString& exePath, const QStringList& args,
                       const QString& workingDir)
{
    // 1) Desktop HWND (explorer) -> PID
    const HWND shellWnd = GetShellWindow();
    if (!shellWnd) {
        EXLOG_WARN("elev") << "no shell window";
        return nullptr;
    }
    DWORD shellPid = 0;
    GetWindowThreadProcessId(shellWnd, &shellPid);
    if (!shellPid) {
        EXLOG_WARN("elev") << "no shell pid";
        return nullptr;
    }

    // 2) Explorer token
    HANDLE shellProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, shellPid);
    if (!shellProc) {
        EXLOG_WARN("elev") << "cannot open shell process";
        return nullptr;
    }
    HANDLE shellToken = nullptr;
    if (!OpenProcessToken(shellProc, TOKEN_DUPLICATE, &shellToken)) {
        CloseHandle(shellProc);
        EXLOG_WARN("elev") << "cannot open shell token";
        return nullptr;
    }
    CloseHandle(shellProc);

    // 3) Duplicate primary token
    HANDLE dupToken = nullptr;
    if (!DuplicateTokenEx(shellToken, TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation,
                          TokenPrimary, &dupToken)) {
        CloseHandle(shellToken);
        EXLOG_WARN("elev") << "DuplicateTokenEx failed";
        return nullptr;
    }
    CloseHandle(shellToken);

    // 4) CreateProcessWithTokenW
    const QString cmdLine = buildCommandLine(exePath, args);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    const wchar_t* workdir = workingDir.isEmpty()
        ? nullptr
        : reinterpret_cast<const wchar_t*>(workingDir.utf16());

    // wide command buffer (CreateProcessWithTokenW needs it writable)
    wchar_t* mutableCmd = _wcsdup(reinterpret_cast<const wchar_t*>(cmdLine.utf16()));
    const BOOL ok = CreateProcessWithTokenW(
        dupToken, LOGON_WITH_PROFILE, nullptr, mutableCmd,
        CREATE_UNICODE_ENVIRONMENT, nullptr, workdir, &si, &pi);
    free(mutableCmd);
    CloseHandle(dupToken);
    if (!ok) {
        EXLOG_WARN("elev") << "CreateProcessWithTokenW failed:" << GetLastError();
        return nullptr;
    }
    CloseHandle(pi.hThread);
    EXLOG_INFO("elev") << "launched de-elevated pid" << pi.dwProcessId;
    return pi.hProcess;
}

void closeProcessHandle(void* handle)
{
    if (handle)
        CloseHandle(static_cast<HANDLE>(handle));
}

bool processHasExited(void* handle, unsigned long* exitCode)
{
    if (!handle)
        return true;
    const DWORD r = WaitForSingleObject(static_cast<HANDLE>(handle), 0);
    if (r != WAIT_OBJECT_0)
        return false;
    if (exitCode)
        GetExitCodeProcess(static_cast<HANDLE>(handle), exitCode);
    return true;
}

bool terminateProcessByHandle(void* handle)
{
    if (!handle)
        return false;
    return TerminateProcess(static_cast<HANDLE>(handle), 1) != FALSE;
}

} // namespace ex::platform

#include "platform/windows/process.h"

#include "core/util/log.h"
#include "platform/windows/elevation.h"

#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <winternl.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdlib>

namespace ex::platform {

bool clearRobloxCookieFile()
{
    QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (localAppData.isEmpty()) {
        localAppData = QStandardPaths::writableLocation(
            QStandardPaths::GenericDataLocation);
    }
    if (localAppData.isEmpty()) {
        EXLOG_WARN("privacy") << "LOCALAPPDATA is unavailable; "
                                 "cannot clear RobloxCookies.dat";
        return false;
    }

    const QString path = QDir(localAppData).filePath(
        QStringLiteral("Roblox/LocalStorage/RobloxCookies.dat"));
    QFile file(path);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        EXLOG_WARN("privacy") << "cannot clear RobloxCookies.dat:"
                              << file.errorString();
        return false;
    }
    file.close();
    EXLOG_INFO("privacy") << "cleared RobloxCookies.dat before launch";
    return true;
}

qint64 fileTimeToMs(const FILETIME& ft)
{
    // 116444736000000000 = offset from 1601-01-01 to the unix epoch (100ns)
    const ULARGE_INTEGER u{{ft.dwLowDateTime, ft.dwHighDateTime}};
    return static_cast<qint64>((u.QuadPart - 116444736000000000ULL) / 10000ULL);
}

QList<ProcessInfo> processesByName(const QString& nameLower)
{
    QList<ProcessInfo> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return out;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof pe;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (QString::fromWCharArray(pe.szExeFile)
                    .compare(nameLower, Qt::CaseInsensitive) == 0) {
                ProcessInfo info;
                info.pid = pe.th32ProcessID;
                info.parentPid = pe.th32ParentProcessID;
                info.name = QString::fromWCharArray(pe.szExeFile);

                HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                if (h) {
                    FILETIME ftCreate{}, ftExit{}, ftKernel{}, ftUser{};
                    if (GetProcessTimes(h, &ftCreate, &ftExit, &ftKernel, &ftUser))
                        info.createTimeMs = fileTimeToMs(ftCreate);
                    CloseHandle(h);
                }
                out.append(info);
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return out;
}

namespace {

// The public winternl.h hides the PEB/ProcessParameters fields we need —
// we define our own x64 layouts with static_asserts (RM's approach).
struct UstrX {
    unsigned short Length;        // in bytes
    unsigned short MaximumLength;
    unsigned int _pad;
    const void* Buffer;
};

// offsetof(RTL_USER_PROCESS_PARAMETERS, CommandLine) on x64 = 112
constexpr size_t kCmdLineOffset = 112;
// offsetof(PEB, ProcessParameters) on x64 = 32
constexpr size_t kPebParamsOffset = 32;

using NtQIPFn = LONG (WINAPI*)(HANDLE, LONG, PVOID, ULONG, PULONG);

struct ReadCtx {
    HANDLE h;
    bool read(size_t remoteAddr, void* out, size_t size) const
    {
        SIZE_T n = 0;
        return ReadProcessMemory(h, reinterpret_cast<LPCVOID>(remoteAddr), out, size, &n)
               && n == size;
    }
};

} // namespace

QString processCommandLine(qint64 pid)
{
    // RM's path: OpenProcess(VM_READ|QUERY_INFORMATION) ->
    // NtQueryInformationProcess(ProcessBasicInformation) -> PEB ->
    // ProcessParameters -> CommandLine (UNICODE_STRING).
    HANDLE h = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE,
                           static_cast<DWORD>(pid));
    if (!h)
        return {};

    QString result;
    do {
        static NtQIPFn ntQip = reinterpret_cast<NtQIPFn>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
        if (!ntQip)
            break;

        PROCESS_BASIC_INFORMATION pbi{};
        if (ntQip(h, 0 /* ProcessBasicInformation */, &pbi, sizeof pbi, nullptr) != 0)
            break;
        if (!pbi.PebBaseAddress)
            break;

        ReadCtx rd{h};

        // PEB: read the window around ProcessParameters (32..39)
        quintptr params = 0;
        if (!rd.read(reinterpret_cast<size_t>(pbi.PebBaseAddress) + kPebParamsOffset,
                     &params, sizeof params) || !params)
            break;

        // UNICODE_STRING CommandLine at offset 112
        UstrX us{};
        if (!rd.read(params + kCmdLineOffset, &us, sizeof us) || us.Length == 0 || !us.Buffer)
            break;

        const int charCount = us.Length / 2;
        if (charCount <= 0 || charCount > 32768)
            break;
        QVector<wchar_t> buf(static_cast<qsizetype>(charCount) + 1, L'\0');
        if (!rd.read(reinterpret_cast<size_t>(us.Buffer), buf.data(),
                     static_cast<size_t>(us.Length)))
            break;
        result = QString::fromWCharArray(buf.constData(), charCount);
    } while (false);

    CloseHandle(h);
    return result;
}

bool killVerified(qint64 pid, const QString& expectedImageLower,
                  const QString& expectedTokenInCmdline)
{
    // Triple check through ONE handle (RM's approach):
    //   1) image name == the expected one;
    //   2) if a token was given, the command line contains the launchtime token;
    //   3) the PID is still alive at TerminateProcess time — same handle.
    HANDLE h = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                           FALSE, static_cast<DWORD>(pid));
    if (!h) {
        EXLOG_DEBUG("proc") << "killVerified: cannot open pid" << pid;
        return false; // already dead or access denied — either way we do NOT kill
    }
    bool verified = false;
    do {
        wchar_t img[MAX_PATH]{};
        DWORD sz = MAX_PATH;
        if (!QueryFullProcessImageNameW(h, 0, img, &sz))
            break;
        const QString image = QString::fromWCharArray(img);
        if (!image.endsWith(expectedImageLower, Qt::CaseInsensitive))
            break;
        if (!expectedTokenInCmdline.isEmpty()) {
            const QString cmd = processCommandLine(pid);
            if (!cmd.contains(expectedTokenInCmdline)) {
                EXLOG_WARN("proc") << "killVerified: token mismatch for pid" << pid;
                break;
            }
        }
        verified = true;
    } while (false);

    if (!verified) {
        CloseHandle(h);
        return false;
    }
    const BOOL ok = TerminateProcess(h, 1);
    CloseHandle(h);
    return ok == TRUE;
}

bool requestGracefulClose(qint64 pid)
{
    const qint64 hwnd = findMainWindow(pid);
    if (!hwnd)
        return false;
    PostMessageW(reinterpret_cast<HWND>(hwnd), WM_CLOSE, 0, 0);
    return true;
}

namespace {
struct EnumWindowsCtx {
    qint64 pid = 0;
    HWND best = nullptr;
    qint64 bestArea = 0;
};

BOOL CALLBACK enumProc(HWND hwnd, LPARAM lp)
{
    auto* ctx = reinterpret_cast<EnumWindowsCtx*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (static_cast<qint64>(pid) != ctx->pid)
        return TRUE;
    // Hidden windows (SW_HIDE) count too: otherwise show/focus after Hide
    // do not find the client's main window. Pick the largest by area.
    char cls[64]{};
    GetClassNameA(hwnd, cls, sizeof cls - 1);
    if (strcmp(cls, "Windows.UI.Composition.DesktopWindowContentBridge") == 0)
        return TRUE; // UWP leftovers
    RECT rc{};
    if (!GetWindowRect(hwnd, &rc))
        return TRUE;
    const qint64 area = static_cast<qint64>(rc.right - rc.left) * (rc.bottom - rc.top);
    if (area > ctx->bestArea) {
        ctx->bestArea = area;
        ctx->best = hwnd;
    }
    return TRUE;
}
} // namespace

qint64 findMainWindow(qint64 pid)
{
    EnumWindowsCtx ctx;
    ctx.pid = pid;
    EnumWindows(enumProc, reinterpret_cast<LPARAM>(&ctx));
    return reinterpret_cast<qint64>(ctx.best);
}

bool openUri(const QString& uri)
{
    const HINSTANCE r = ShellExecuteW(nullptr, L"open",
                                      reinterpret_cast<const wchar_t*>(uri.utf16()),
                                      nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<qintptr>(r) > 32;
}

bool openUriDeElevated(const QString& uri)
{
    if (!isElevated())
        return openUri(uri);

    // Hand-off through our own exe in the explorer context: helper mode
    // --open-uri runs ShellExecute with the normal (de-elevated) user token.
    // Keeps the URI away from cmd (its '%'/'&' would be escaped twice).
    const QString exe = QCoreApplication::applicationFilePath();
    const QStringList args{QStringLiteral("--open-uri"), uri};
    void* handle = launchDeelevated(exe, args, QCoreApplication::applicationDirPath());
    if (!handle) {
        EXLOG_WARN("proc") << "de-elevated URI hand-off failed, using ShellExecute";
        return openUri(uri);
    }

    const DWORD wait = WaitForSingleObject(static_cast<HANDLE>(handle), 5000);
    DWORD exitCode = 1;
    if (wait == WAIT_OBJECT_0)
        GetExitCodeProcess(static_cast<HANDLE>(handle), &exitCode);
    closeProcessHandle(handle);

    if (wait == WAIT_TIMEOUT) {
        // The helper may still be running, but ShellExecute already happened.
        EXLOG_WARN("proc") << "de-elevated URI helper did not exit in time";
        return true;
    }
    if (exitCode != 0) {
        EXLOG_WARN("proc") << "de-elevated URI helper failed:" << exitCode;
        return openUri(uri);
    }
    EXLOG_INFO("proc") << "URI opened de-elevated";
    return true;
}

bool launchPlayerDeElevated(const QString& exePath, const QString& uri)
{
    const QFileInfo info(exePath);
    if (!info.isFile()
        || info.fileName().compare(QStringLiteral("RobloxPlayerBeta.exe"),
                                   Qt::CaseInsensitive) != 0
        || !uri.startsWith(QStringLiteral("roblox-player:"), Qt::CaseInsensitive)) {
        return false;
    }

    const QString exe = info.absoluteFilePath();
    const QString workDir = info.absolutePath();
    const auto startDirect = [&exe, &uri, &workDir]() {
        return QProcess::startDetached(exe, {uri}, workDir);
    };

    if (!isElevated())
        return startDirect();

    // An elevated manager starts the client through the de-elevated helper, else
    // Roblox and the browser stack may refuse to run elevated.
    void* handle = launchDeelevated(
        QCoreApplication::applicationFilePath(),
        {QStringLiteral("--launch-player"), exe, uri},
        QCoreApplication::applicationDirPath());
    if (!handle) {
        EXLOG_WARN("proc") << "de-elevated player hand-off failed, starting directly";
        return startDirect();
    }

    const DWORD wait = WaitForSingleObject(static_cast<HANDLE>(handle), 5000);
    DWORD exitCode = 1;
    if (wait == WAIT_OBJECT_0)
        GetExitCodeProcess(static_cast<HANDLE>(handle), &exitCode);
    closeProcessHandle(handle);

    if (wait == WAIT_TIMEOUT) {
        EXLOG_WARN("proc") << "de-elevated player helper did not exit in time";
        return true;
    }
    if (exitCode != 0) {
        EXLOG_WARN("proc") << "de-elevated player helper failed:" << exitCode;
        return startDirect();
    }
    EXLOG_INFO("proc") << "player started de-elevated";
    return true;
}

} // namespace ex::platform

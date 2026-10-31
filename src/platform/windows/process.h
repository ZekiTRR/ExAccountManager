#pragma once

#include <QString>
#include <QList>

struct _FILETIME;

namespace ex::platform {

// Снимок процесса из Toolhelp-снапшота.
struct ProcessInfo {
    qint64 pid = 0;
    qint64 parentPid = 0;
    QString name;            // например "RobloxPlayerBeta.exe"
    qint64 createTimeMs = 0; // FILETIME создания, ms since epoch — защита от PID reuse
};

// Перечисление процессов по имени (без регистра): "RobloxPlayerBeta.exe".
QList<ProcessInfo> processesByName(const QString& nameLower);

// Командная строка процесса через чтение PEB (NtQueryInformationProcess +
// ReadProcessMemory). Toolhelp/WMIC для Hyperion-защищённого клиента её
// не отдают — только прямой PEB-путь (приём из RM).
QString processCommandLine(qint64 pid);

// Верифицированное закрытие: перепроверяет имя образа и (опционально)
// наличие launchToken в командной строке через ОДИН handle, только потом
// TerminateProcess. Никогда не убивает процесс, не прошедший проверку.
bool killVerified(qint64 pid, const QString& expectedImageLower,
                  const QString& expectedTokenInCmdline = QString());

// Мягкое закрытие (CloseMainWindow-эквивалент: WM_CLOSE/WM_QUIT).
bool requestGracefulClose(qint64 pid);

// Окно процесса: самый большой видимый top-level window данного PID.
qint64 findMainWindow(qint64 pid);

// Простой запуск URI через ShellExecute (roblox-player:/roblox:// протоколы).
bool openUri(const QString& uri);

// То же, но при elevated-процессе URI открывается через токен explorer
// (Roblox-клиент не любит запуск от администратора). Fallback — openUri.
bool openUriDeElevated(const QString& uri);

// Запуск конкретного RobloxPlayerBeta.exe с launch URI (без инжекта).
// При elevated-процессе используется de-elevated helper --launch-player;
// fallback — прямой запуск. Рабочий каталог — папка клиента (как ярлык).
bool launchPlayerDeElevated(const QString& exePath, const QString& uri);

// Очистить RobloxCookies.dat перед запуском клиента (приём RM:
// %LOCALAPPDATA%\Roblox\LocalStorage\RobloxCookies.dat). Файл усекается до
// нуля, чтобы Roblox не ассоциировал аккаунты между запусками.
// Возвращает true, если файла нет или он успешно очищен.
bool clearRobloxCookieFile();

// FILETIME (100ns с 1601) -> unix ms.
qint64 fileTimeToMs(const _FILETIME& ft);
} // namespace ex::platform

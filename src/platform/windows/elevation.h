#pragma once

#include <QString>
#include <QStringList>

namespace ex::platform {

// Текущий процесс запущен с повышенными правами (admin)?
bool isElevated();

// Запуск процесса с понижением прав — от токена оболочки (explorer).
// Приём из RAM Updater (RunAsDesktopUser): дублируем токен explorer.exe
// и создаём процесс через CreateProcessWithTokenW.
// Нужно, потому что Chrome 136+ отказывается от remote debugging,
// если браузер запущен с повышением.
//
// Возвращает HANDLE процесса (закрывать через closeProcessHandle)
// или nullptr при неудаче. Работает только из elevated-контекста
// (требуется SeImpersonatePrivilege).
void* launchDeelevated(const QString& exePath, const QStringList& args,
                       const QString& workingDir = QString());

void closeProcessHandle(void* handle);
bool processHasExited(void* handle, unsigned long* exitCode = nullptr);
bool terminateProcessByHandle(void* handle);

} // namespace ex::platform

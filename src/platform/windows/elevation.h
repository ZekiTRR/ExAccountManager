#pragma once

#include <QString>
#include <QStringList>

namespace ex::platform {

// Is the current process elevated (admin)?
bool isElevated();

// Start a process with reduced rights — from the shell (explorer) token.
// From the RAM Updater (RunAsDesktopUser): duplicate the explorer.exe token
// and create the process with CreateProcessWithTokenW.
// Needed because Chrome 136+ refuses remote debugging
// when the browser runs elevated.
//
// Returns a process HANDLE (close it with closeProcessHandle)
// or nullptr on failure. Only works from an elevated context
// (needs SeImpersonatePrivilege).
void* launchDeelevated(const QString& exePath, const QStringList& args,
                       const QString& workingDir = QString());

void closeProcessHandle(void* handle);
bool processHasExited(void* handle, unsigned long* exitCode = nullptr);
bool terminateProcessByHandle(void* handle);

} // namespace ex::platform

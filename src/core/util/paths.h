#pragma once

#include <QString>

namespace ex::util {

// Application data location.
// Resolution order:
//   1. Portable mode: a "portable" file/folder next to the exe means
//      all data lives next to the exe (data/).
//   2. %APPDATA%\ExAccountManager (standard mode).
class AppPaths {
public:
    static void init();
    static bool portable();

    static QString dataDir();      // data root
    static QString dbPath();       // exmanager.db
    static QString backupsDir();
    static QString logsDir();
    static QString cacheDir();
    static QString exportsDir();
    static QString themesDir();    // user themes
    static QString presetsDir();   // launch presets (files, hand-editable)
    static QString screenshotsDir();

private:
    static QString s_rootOverride; // for tests
    static bool s_portable;
};

} // namespace ex::util

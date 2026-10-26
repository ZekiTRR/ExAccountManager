#pragma once

#include <QString>

namespace ex::util {

// Расположение данных приложения.
// Порядок разрешения:
//   1. Портативный режим: если рядом с exe лежит файл/папка "portable" —
//      все данные хранятся рядом с exe (data/).
//   2. %APPDATA%\ExAccountManager (стандартный режим).
class AppPaths {
public:
    static void init();
    static bool portable();

    static QString dataDir();      // корень данных
    static QString dbPath();       // exmanager.db
    static QString backupsDir();
    static QString logsDir();
    static QString cacheDir();
    static QString exportsDir();
    static QString themesDir();    // пользовательские темы
    static QString presetsDir();   // пресеты запуска (файлы, hand-editable)
    static QString screenshotsDir();

private:
    static QString s_rootOverride; // для тестов
    static bool s_portable;
};

} // namespace ex::util

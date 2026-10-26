#pragma once

#include <QColor>
#include <QString>

// Material You (Material Design 3) токены приложения.
//
// Схемы — канонический baseline M3 (seed #6750A4) тёмная/светлая; дополненные
// нашими расширениями: success/warning, presence-цвета, оверлеи состояний.
// Палитра применяется вместе с QPalette (примитивы стиля: стрелки деревьев,
// combobox и т.п.) и глобальным QSS.

namespace ex::ui::theme {

// Полный набор семантических токенов текущей темы.
struct Palette {
    // Primary
    QColor primary;           // фон filled-кнопок
    QColor accent;            // акцентный текст/иконки (в тёмной теме светлее)
    QColor onPrimary;
    QColor primaryHover;      // фон filled-кнопки при hover
    QColor primaryPressed;
    QColor primaryContainer;
    QColor onPrimaryContainer;

    // Secondary (для tonal-кнопок, чипов, выделения)
    QColor secondaryContainer;
    QColor onSecondaryContainer;

    // Поверхности
    QColor surface;
    QColor surfaceContainerLow;
    QColor surfaceContainer;
    QColor surfaceContainerHigh;
    QColor surfaceContainerHighest;
    QColor onSurface;
    QColor onSurfaceVariant;
    QColor outline;
    QColor outlineVariant;

    // Error
    QColor error;
    QColor onError;
    QColor errorHover;
    QColor errorPressed;
    QColor errorContainer;
    QColor onErrorContainer;

    // Расширения (не из спецификации M3)
    QColor success;
    QColor warning;
    QColor presenceOnline;
    QColor presenceGame;
    QColor presenceStudio;

    QColor tooltipBase;
    QColor tooltipText;

    // Предвычисленные состояния для QSS и делегатов
    QColor fieldBg;           // фон полей ввода
    QColor fieldBorder;       // рамка полей в покое
    QColor hoverOverlay;      // rgba поверх для hover текст-кнопок
    QColor pressedOverlay;
    QColor rowAlt;            // чередование строк таблиц
    QColor icon;              // цвет иконок на кнопках
    QColor iconDim;

    bool dark = true;
};

// Текущая применённая палитра (до первого apply* — тёмная).
const Palette& currentPalette();
bool isDark();

// Применить тему по имени ("dark"/"light", иначе — тёмная):
// QSS + QPalette + перерисовка иконок кнопок.
void applyTheme(const QString& name);

// QSS схем (используются applyTheme; отдельно — для loadCustom).
QString darkQss();
QString lightQss();

// Совместимость со старым API: применить готовый QSS без смены палитры.
[[deprecated("use applyTheme(name)")]] void apply(const QString& qss);

// Пользовательская тема из JSON (themes/*.json): {"dark":true,"colors":{...}}.
// Переопределяет акцент поверх baseline-схемы, с фолбеком на тёмную.
QString loadCustom(const QString& jsonPath, QString* error = nullptr);

} // namespace ex::ui::theme

#pragma once

#include <QColor>
#include <QString>

// Material You (Material Design 3) tokens for the app.
//
// Schemes — canonical M3 baseline (seed #6750A4) dark/light, extended with
// our own tokens: success/warning, presence colours, state overlays.
// The palette is applied with a QPalette (style primitives: tree arrows,
// combobox, etc.) and the global QSS.

namespace ex::ui::theme {

// All semantic tokens of the current theme.
struct Palette {
    // Primary
    QColor primary;           // filled button background
    QColor accent;            // accent text/icons (lighter in the dark theme)
    QColor onPrimary;
    QColor primaryHover;      // filled button background on hover
    QColor primaryPressed;
    QColor primaryContainer;
    QColor onPrimaryContainer;

    // Secondary (for tonal buttons, chips, selection)
    QColor secondaryContainer;
    QColor onSecondaryContainer;

    // Surfaces
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

    // Extensions (not part of the M3 spec)
    QColor success;
    QColor warning;
    QColor presenceOnline;
    QColor presenceGame;
    QColor presenceStudio;

    QColor tooltipBase;
    QColor tooltipText;

    // Precomputed states for QSS and delegates
    QColor fieldBg;           // input field background
    QColor fieldBorder;       // resting field outline
    QColor hoverOverlay;      // rgba overlay for text-button hover
    QColor pressedOverlay;
    QColor rowAlt;            // table row banding
    QColor icon;              // icon colour on buttons
    QColor iconDim;

    bool dark = true;
};

// Currently applied palette (dark before the first apply* call).
const Palette& currentPalette();
bool isDark();

// Apply a theme by name ("dark"/"light", anything else = dark):
// QSS + QPalette + repainting of button icons.
void applyTheme(const QString& name);

// QSS schemes (used by applyTheme; separately by loadCustom).
QString darkQss();
QString lightQss();

// Old-API compatibility: apply prepared QSS without changing the palette.
[[deprecated("use applyTheme(name)")]] void apply(const QString& qss);

// User theme from JSON (themes/*.json): {"dark":true,"colors":{...}}.
// Overrides the accent on the baseline scheme, falling back to dark.
QString loadCustom(const QString& jsonPath, QString* error = nullptr);

} // namespace ex::ui::theme

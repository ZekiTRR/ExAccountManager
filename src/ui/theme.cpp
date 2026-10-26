#include "ui/theme.h"

#include "ui/material/icons.h"

#include <QApplication>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>

namespace ex::ui::theme {

namespace {

QColor hoverOf(const QColor& c, bool dark)
{
    return dark ? c.lighter(110) : c.darker(108);
}

QColor pressedOf(const QColor& c, bool dark)
{
    return dark ? c.lighter(118) : c.darker(116);
}

QString rgba(const QColor& c, qreal alpha)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(c.red()).arg(c.green()).arg(c.blue()).arg(qRound(alpha * 255));
}

QString qssColor(const QColor& c)
{
    return c.alpha() == 255 ? c.name() : rgba(c, c.alphaF());
}

// Графит + ирис. В тёмной теме filled-кнопки чуть глубже акцента,
// чтобы белый текст оставался читаемым; accent — для иконок и активных
// состояний (в макете он светлее).
Palette paletteDark()
{
    Palette c;
    c.dark = true;

    c.primary = QColor(0x74, 0x5C, 0xFF);
    c.accent = QColor(0x9B, 0x8C, 0xFF);
    c.onPrimary = QColor(0xFF, 0xFF, 0xFF);
    c.primaryHover = QColor(0x8A, 0x78, 0xFF);
    c.primaryPressed = QColor(0x62, 0x49, 0xE8);
    c.primaryContainer = QColor(0x26, 0x20, 0x43);
    c.onPrimaryContainer = QColor(0xCF, 0xC5, 0xFF);

    c.secondaryContainer = QColor(0x1D, 0x1B, 0x30);
    c.onSecondaryContainer = QColor(0xF0, 0xEE, 0xF8);

    c.surface = QColor(0x0A, 0x0C, 0x12);
    c.surfaceContainerLow = QColor(0x0D, 0x10, 0x17);
    c.surfaceContainer = QColor(0x12, 0x16, 0x1E);
    c.surfaceContainerHigh = QColor(0x16, 0x1B, 0x24);
    c.surfaceContainerHighest = QColor(0x1B, 0x21, 0x30);

    c.onSurface = QColor(0xF0, 0xEE, 0xF8);
    c.onSurfaceVariant = QColor(0xA6, 0xA2, 0xB8);
    c.outline = QColor(0x8A, 0x84, 0x99);
    c.outlineVariant = QColor(0x22, 0x26, 0x34);

    c.error = QColor(0xFF, 0x8A, 0x8A);
    c.onError = QColor(0x2B, 0x0E, 0x12);
    c.errorHover = QColor(0xFF, 0xA0, 0xA0);
    c.errorPressed = QColor(0xEC, 0x72, 0x72);
    c.errorContainer = QColor(0x3A, 0x20, 0x26);
    c.onErrorContainer = QColor(0xFF, 0xD9, 0xD9);

    c.success = QColor(0x5B, 0xE0, 0xB0);
    c.warning = QColor(0xF2, 0xC6, 0x6D);
    c.presenceOnline = QColor(0x6F, 0xC3, 0xFF);
    c.presenceGame = c.success;
    c.presenceStudio = QColor(0xB5, 0x9C, 0xFF);

    c.tooltipBase = QColor(0x1C, 0x21, 0x30);
    c.tooltipText = QColor(0xF3, 0xF1, 0xFA);

    c.fieldBg = QColor(0x0E, 0x12, 0x19);
    c.fieldBorder = QColor(255, 255, 255, 24);
    c.rowAlt = QColor(255, 255, 255, 6);
    c.icon = c.onSurfaceVariant;
    c.iconDim = c.outline;

    c.hoverOverlay = QColor(240, 238, 248, 12);
    c.pressedOverlay = QColor(240, 238, 248, 20);
    return c;
}

Palette paletteLight()
{
    Palette c;
    c.dark = false;

    c.primary = QColor(0x5B, 0x4B, 0xE0);
    c.accent = QColor(0x5B, 0x4B, 0xE0);
    c.onPrimary = QColor(0xFF, 0xFF, 0xFF);
    c.primaryHover = QColor(0x6E, 0x5C, 0xF6);
    c.primaryPressed = QColor(0x4C, 0x3C, 0xCB);
    c.primaryContainer = QColor(0xEF, 0xEA, 0xFF);
    c.onPrimaryContainer = QColor(0x3A, 0x2B, 0x9E);

    c.secondaryContainer = QColor(0xF0, 0xED, 0xFF);
    c.onSecondaryContainer = QColor(0x1B, 0x18, 0x28);

    c.surface = QColor(0xF3, 0xF2, 0xF8);
    c.surfaceContainerLow = QColor(0xF8, 0xF7, 0xFC);
    c.surfaceContainer = QColor(0xFF, 0xFF, 0xFF);
    c.surfaceContainerHigh = QColor(0xF7, 0xF5, 0xFB);
    c.surfaceContainerHighest = QColor(0xEE, 0xEC, 0xF5);

    c.onSurface = QColor(0x1B, 0x18, 0x28);
    c.onSurfaceVariant = QColor(0x63, 0x5E, 0x75);
    c.outline = QColor(0x74, 0x6F, 0x82);
    c.outlineVariant = QColor(0xE9, 0xE6, 0xF0);

    c.error = QColor(0xC4, 0x3D, 0x4E);
    c.onError = QColor(0xFF, 0xFF, 0xFF);
    c.errorHover = QColor(0xD1, 0x4B, 0x5C);
    c.errorPressed = QColor(0xA9, 0x2F, 0x40);
    c.errorContainer = QColor(0xFB, 0xEB, 0xED);
    c.onErrorContainer = QColor(0x6B, 0x1F, 0x28);

    c.success = QColor(0x14, 0x85, 0x5F);
    c.warning = QColor(0x9C, 0x6C, 0x00);
    c.presenceOnline = QColor(0x12, 0x73, 0xB8);
    c.presenceGame = c.success;
    c.presenceStudio = QColor(0x7E, 0x57, 0xC2);

    c.tooltipBase = QColor(0x23, 0x1F, 0x35);
    c.tooltipText = QColor(0xF7, 0xF5, 0xFF);

    c.fieldBg = QColor(0xF4, 0xF2, 0xF9);
    c.fieldBorder = QColor(30, 24, 70, 30);
    c.rowAlt = QColor(25, 20, 60, 4);
    c.icon = c.onSurfaceVariant;
    c.iconDim = c.outline;

    c.hoverOverlay = QColor(25, 20, 60, 11);
    c.pressedOverlay = QColor(25, 20, 60, 18);
    return c;
}

Palette& mutableCurrent()
{
    static Palette instance = paletteDark();
    return instance;
}

QPalette qtPalette(const Palette& c)
{
    QPalette p;
    QColor disabledText = c.onSurface;
    disabledText.setAlpha(99);

    p.setColor(QPalette::Window, c.surface);
    p.setColor(QPalette::WindowText, c.onSurface);
    p.setColor(QPalette::Base, c.fieldBg);
    p.setColor(QPalette::AlternateBase, c.rowAlt);
    p.setColor(QPalette::Text, c.onSurface);
    p.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    p.setColor(QPalette::Button, c.surfaceContainerHigh);
    p.setColor(QPalette::ButtonText, c.onSurface);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
    p.setColor(QPalette::Highlight, c.secondaryContainer);
    p.setColor(QPalette::HighlightedText, c.onSecondaryContainer);
    p.setColor(QPalette::ToolTipBase, c.tooltipBase);
    p.setColor(QPalette::ToolTipText, c.tooltipText);
    p.setColor(QPalette::PlaceholderText, c.outline);
    p.setColor(QPalette::Link, c.accent);
    p.setColor(QPalette::LinkVisited, c.accent);
    p.setColor(QPalette::Accent, c.accent);
    p.setColor(QPalette::BrightText, c.onSurface);
    return p;
}

QHash<QString, QString> qssTokens(const Palette& c)
{
    QHash<QString, QString> t;
    const auto put = [&t](const char* name, const QColor& color) {
        t.insert(QLatin1String(name), qssColor(color));
    };

    put("primary", c.primary);
    put("primaryHover", c.primaryHover);
    put("primaryPressed", c.primaryPressed);
    put("primaryContainer", c.primaryContainer);
    put("onPrimaryContainer", c.onPrimaryContainer);
    put("accent", c.accent);
    put("onPrimary", c.onPrimary);
    put("secondaryContainer", c.secondaryContainer);
    put("onSecondaryContainer", c.onSecondaryContainer);
    put("surface", c.surface);
    put("rail", c.surfaceContainerLow);
    put("card", c.surfaceContainer);
    put("raised", c.surfaceContainerHigh);
    put("highest", c.surfaceContainerHighest);
    put("text", c.onSurface);
    put("text2", c.onSurfaceVariant);
    put("text3", c.outline);
    put("hairline", c.outlineVariant);
    put("fieldBg", c.fieldBg);
    put("fieldBorder", c.fieldBorder);
    put("error", c.error);
    put("onError", c.onError);
    put("errorHover", c.errorHover);
    put("errorPressed", c.errorPressed);
    put("errorContainer", c.errorContainer);
    put("onErrorContainer", c.onErrorContainer);
    put("success", c.success);
    put("warning", c.warning);
    put("sky", c.presenceOnline);
    put("tooltipBase", c.tooltipBase);
    put("tooltipText", c.tooltipText);

    t.insert(QStringLiteral("accentSoft"), rgba(c.accent, 0.13));
    t.insert(QStringLiteral("accentSofter"), rgba(c.accent, 0.08));
    t.insert(QStringLiteral("selectionBorder"), rgba(c.accent, 0.22));
    t.insert(QStringLiteral("successSoft"), rgba(c.success, 0.12));
    t.insert(QStringLiteral("warningSoft"), rgba(c.warning, 0.12));
    t.insert(QStringLiteral("errorSoft"), rgba(c.error, 0.10));
    t.insert(QStringLiteral("hover"), rgba(c.onSurface, c.dark ? 0.045 : 0.035));
    t.insert(QStringLiteral("pressed"), rgba(c.onSurface, 0.07));
    t.insert(QStringLiteral("disabledText"), rgba(c.onSurface, 0.38));
    t.insert(QStringLiteral("scrollHandle"), rgba(c.onSurfaceVariant, 0.28));
    t.insert(QStringLiteral("scrollHandleHover"), rgba(c.onSurfaceVariant, 0.48));
    t.insert(QStringLiteral("placeholder"), rgba(c.onSurfaceVariant, 0.72));
    t.insert(QStringLiteral("glow"), rgba(c.accent, c.dark ? 0.12 : 0.08));
    return t;
}

QString buildQss(const Palette& c)
{
    QString qss = QStringLiteral(R"(
* { outline: none; }

QWidget {
    font-family: "Segoe UI Variable Text", "Segoe UI Variable", "Segoe UI", "Inter", sans-serif;
    font-size: 13px;
    color: %text%;
}
QMainWindow { background: %surface%; }
QDialog { background: %card%; }
QWidget#pageRoot, QWidget#pageBody, QWidget#settingsStack { background: transparent; }
QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }

/* ---- типографика ---- */
QLabel[class="pageTitle"] {
    font-family: "Segoe UI Variable Display", "Segoe UI Variable", "Segoe UI", sans-serif;
    font-size: 23px;
    font-weight: 500;
    color: %text%;
}
QLabel[class="pageSub"] { font-size: 12px; color: %text2%; }
QLabel[class="titleLarge"] { font-size: 20px; font-weight: 500; color: %text%; }
QLabel[class="titleMedium"] { font-size: 16px; font-weight: 500; color: %text%; }
QLabel[class="titleSmall"] { font-size: 13px; font-weight: 500; color: %text%; }
QLabel[class="sectionTitle"] { font-size: 13.5px; font-weight: 500; color: %text%; }
QLabel[class="sectionSub"] { font-size: 11px; color: %text3%; }
QLabel[class="dim"] { color: %text2%; }
QLabel[class="tertiary"] { color: %text3%; }
QLabel[class="errorText"] { color: %error%; }
QLabel[class="successText"] { color: %success%; }
QLabel[class="warningText"] { color: %warning%; }
QLabel[class="mono"] { font-family: "Cascadia Mono", "Consolas", monospace; font-size: 11px; }
QLabel[class="brandName"] {
    font-family: "Segoe UI Variable Display", "Segoe UI Variable", "Segoe UI", sans-serif;
    font-size: 13.5px;
    font-weight: 500;
    color: %text%;
}
QLabel[class="brandSub"] { font-size: 10px; font-weight: 500; color: %text3%; }

QLabel[class="chip"] {
    background: %highest%;
    border: 1px solid %hairline%;
    border-radius: 6px;
    padding: 3px 8px;
    font-size: 11px;
    font-weight: 500;
    color: %text2%;
}
QLabel[class="chipAccent"] {
    background: %accentSoft%;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 3px 8px;
    font-size: 11px;
    font-weight: 500;
    color: %accent%;
}
QLabel[class="chipMint"] {
    background: %successSoft%;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 3px 8px;
    font-size: 11px;
    font-weight: 500;
    color: %success%;
}
QLabel[class="fieldLabel"] { font-size: 11.5px; font-weight: 500; color: %text2%; }
QLabel[class="fieldTag"] { font-family: "Cascadia Mono", "Consolas", monospace; font-size: 11px; color: %text3%; }
QLabel[class="fieldHint"] { font-size: 11px; color: %text3%; }
QLabel[class="fieldError"] { font-size: 11px; color: %error%; }
QLabel[class="statusText"] { font-size: 11px; color: %text3%; }
QLabel[class="statusText"][tone="success"] { color: %success%; }
QLabel[class="statusText"][tone="busy"] { color: %text2%; }
QLabel[class="statusText"][tone="error"] { color: %error%; }
QLabel[class="statusDot"] { color: %text3%; font-size: 11px; }
QLabel[class="statusDot"][tone="success"] { color: %success%; }
QLabel[class="statusDot"][tone="busy"] { color: %warning%; }
QLabel[class="statusDot"][tone="error"] { color: %error%; }

/* ---- специализированные подписи ---- */
QLabel[class="selTitle"] { font-size: 12.5px; font-weight: 500; color: %text%; }
QLabel[class="selSub"] { font-size: 11px; color: %text3%; }
QLabel[class="stackMore"] {
    background: %highest%;
    border: 1px solid %hairline%;
    border-radius: 9px;
    color: %text2%;
    font-size: 11px;
    font-weight: 500;
}
QLabel[class="statLabel"] { font-size: 11.5px; color: %text3%; }
QLabel[class="statValue"] {
    font-family: "Segoe UI Variable Display", "Segoe UI Variable", "Segoe UI", sans-serif;
    font-size: 22px;
    font-weight: 500;
    color: %text%;
}
QLabel[class="statFoot"] { font-size: 11px; color: %text3%; }
QLabel[class="metricLabel"] { font-size: 11px; color: %text3%; }
QLabel[class="metricValue"] {
    font-family: "Segoe UI Variable Display", "Segoe UI Variable", "Segoe UI", sans-serif;
    font-size: 17px;
    font-weight: 500;
    color: %text%;
}
QLabel[class="tableHead"] { font-size: 11px; color: %text3%; }
QLabel[class="instName"] { font-size: 12.5px; font-weight: 500; color: %text%; }
QLabel[class="instUser"] { font-size: 11px; color: %text3%; }
QLabel[class="instValue"] { font-size: 11.5px; color: %text2%; }
QLabel[class="setLabel"] { font-size: 12.5px; color: %text%; }
QLabel[class="setDesc"] { font-size: 11px; color: %text3%; }
QFrame[class="stateChip"] {
    background: %highest%;
    border: 1px solid %hairline%;
    border-radius: 999px;
}
QFrame[class="stateChip"][foreign="true"] {
    background: %warningSoft%;
    border-color: transparent;
}
QLabel[class="stateChipText"] { font-size: 11px; color: %text2%; }
QFrame[class="stateChip"][foreign="true"] QLabel[class="stateChipText"] {
    color: %warning%;
}

/* ---- карточки ---- */
QFrame[class="card"] {
    background: %card%;
    border: 1px solid %hairline%;
    border-radius: 18px;
}
QFrame[class="innerBox"] {
    background: %fieldBg%;
    border: 1px solid %hairline%;
    border-radius: 12px;
}
QFrame[class="innerBox"][empty="true"] {
    background: transparent;
    border-style: dashed;
}
QFrame[class="tableHeader"] {
    background: transparent;
    border: none;
    border-bottom: 1px solid %hairline%;
}
QFrame[class="instRow"] {
    background: transparent;
    border: none;
    border-bottom: 1px solid %hairline%;
}
QFrame[class="instRow"]:hover { background: %hover%; }
QFrame[class="instRow"][selected="true"] { background: %secondaryContainer%; }
QFrame[class="instRow"][selected="true"]:hover { background: %secondaryContainer%; }
QFrame[class="miniSeg"] {
    background: %fieldBg%;
    border: 1px solid %fieldBorder%;
    border-radius: 8px;
}
QPushButton[class="miniSegItem"] {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 0 8px;
    min-height: 0;
    color: %text2%;
    font-size: 11px;
    font-weight: 500;
}
QPushButton[class="miniSegItem"]:hover { color: %text%; }
QPushButton[class="miniSegItem"]:focus {
    border-color: transparent;
    outline: none;
}
QPushButton[class="miniSegItem"]:checked {
    background: %secondaryContainer%;
    border-color: transparent;
    color: %text%;
}
QPushButton[class="miniSegItem"]:checked:focus {
    border-color: %hairline%;
    outline: none;
}
QFrame[class="launchMark"] {
    background: %accentSoft%;
    border: none;
    border-radius: 9px;
}
QFrame[class="iconTile"] {
    background: %accentSoft%;
    border: none;
    border-radius: 10px;
}
QFrame[class="separator"] { background: %hairline%; }
QFrame[class="themeSwitch"] {
    background: %fieldBg%;
    border: 1px solid %hairline%;
    border-radius: 11px;
}
QFrame[class="themePreview"] {
    background: %highest%;
    border: 1px solid %hairline%;
    border-radius: 12px;
}

/* ---- кнопки ---- */
QPushButton {
    background: %raised%;
    color: %text%;
    border: 1px solid %hairline%;
    border-radius: 11px;
    padding: 8px 14px;
    min-height: 20px;
    font-size: 12px;
    font-weight: 500;
}
QPushButton:hover { background: %highest%; border-color: %text3%; }
QPushButton:pressed { background: %pressed%; }
QPushButton:focus { border-color: %accent%; }
QPushButton:disabled { color: %disabledText%; background: %raised%; border-color: %hairline%; }

QPushButton[variant="filled"], QPushButton[primary="true"] {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                stop:0 %primaryHover%, stop:1 %primary%);
    color: %onPrimary%;
    border: 1px solid transparent;
}
QPushButton[variant="filled"]:hover, QPushButton[primary="true"]:hover {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                stop:0 %primaryHover%, stop:1 %primaryHover%);
}
QPushButton[variant="filled"]:pressed, QPushButton[primary="true"]:pressed {
    background: %primaryPressed%;
}
QPushButton[variant="filled"]:disabled, QPushButton[primary="true"]:disabled {
    color: %disabledText%;
    background: %raised%;
    border-color: %hairline%;
}

QPushButton[variant="text"] {
    background: transparent;
    border: 1px solid transparent;
    color: %text2%;
    padding: 8px 10px;
}
QPushButton[variant="text"]:hover { background: %hover%; color: %text%; }
QPushButton[variant="danger"] {
    background: transparent;
    border: 1px solid transparent;
    color: %error%;
}
QPushButton[variant="danger"]:hover { background: %errorSoft%; }
QPushButton[variant="danger"]:pressed { background: %errorContainer%; }

QPushButton[class="iconBtn"] {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 9px;
    padding: 0;
    min-width: 34px;
    max-width: 34px;
    min-height: 34px;
    max-height: 34px;
}
QPushButton[class="iconBtn"]:hover { background: %hover%; border-color: transparent; }
QPushButton[class="iconBtn"]:pressed { background: %pressed%; }
QPushButton[class="iconBtn"]:focus { border-color: %accent%; }
QPushButton[class="iconBtn"][danger="true"]:hover { background: %errorSoft%; }

QPushButton[class="fieldAction"] {
    background: transparent;
    border: none;
    border-radius: 6px;
    padding: 0;
    min-width: 24px;
    max-width: 24px;
    min-height: 24px;
    max-height: 24px;
}
QPushButton[class="fieldAction"]:hover { background: %hover%; }

QPushButton[class="linkBtn"] {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 4px 6px;
    color: %text2%;
    font-size: 11px;
    font-weight: 400;
    text-align: left;
}
QPushButton[class="linkBtn"]:hover { background: %hover%; color: %text%; }

QPushButton[class="themeOption"] {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 8px;
    padding: 0;
    min-width: 0;
    min-height: 0;
}
QPushButton[class="themeOption"]:hover { background: %hover%; border-color: transparent; }
QPushButton[class="themeOption"]:checked { background: %card%; border-color: %hairline%; }

QPushButton[class="indexBtn"] {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 8px;
    padding: 7px 10px;
    color: %text2%;
    font-size: 12px;
    font-weight: 400;
    text-align: left;
}
QPushButton[class="indexBtn"]:hover { background: %hover%; color: %text%; }
QPushButton[class="indexBtn"]:checked {
    background: %accentSoft%;
    color: %text%;
    font-weight: 500;
}

QPushButton[class="dialogTab"] {
    background: transparent;
    border: none;
    border-bottom: 2px solid transparent;
    border-radius: 0;
    padding: 6px 10px;
    min-height: 0;
    color: %text2%;
    font-size: 12px;
    font-weight: 500;
}
QPushButton[class="dialogTab"]:hover { color: %text%; }
QPushButton[class="dialogTab"]:checked {
    color: %text%;
    border-bottom-color: %accent%;
}

QPushButton[class="rowIcon"] {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 7px;
    padding: 0;
    min-width: 0;
    min-height: 0;
}
QPushButton[class="rowIcon"]:hover { background: %hover%; border-color: transparent; }
QPushButton[class="rowIcon"]:pressed { background: %pressed%; }

/* ---- поля ---- */
QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background: %fieldBg%;
    border: 1px solid %fieldBorder%;
    border-radius: 11px;
    padding: 8px 11px;
    color: %text%;
    font-size: 13px;
    selection-background-color: %accent%;
    selection-color: %onPrimary%;
}
QSpinBox, QDoubleSpinBox {
    padding: 0 6px;
    min-height: 30px;
}
QLineEdit:hover, QPlainTextEdit:hover, QTextEdit:hover, QSpinBox:hover, QComboBox:hover {
    border-color: %text3%;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QSpinBox:focus, QComboBox:focus {
    border-color: %accent%;
}
QLineEdit:disabled, QPlainTextEdit:disabled, QTextEdit:disabled, QSpinBox:disabled {
    color: %disabledText%;
    background: %raised%;
}
/* Внутренний QLineEdit спинбокса не должен наследовать padding/border поля:
   иначе текст обрезается по высоте. */
QSpinBox QLineEdit, QDoubleSpinBox QLineEdit {
    background: transparent;
    border: none;
    border-radius: 0;
    padding: 0;
    margin: 0;
    min-height: 0;
}
QFrame[class="field"] {
    background: %fieldBg%;
    border: 1px solid %fieldBorder%;
    border-radius: 11px;
    min-height: 40px;
    max-height: 40px;
}
QFrame[class="field"]:hover { border-color: %text3%; }
QFrame[class="field"][focused="true"] { border-color: %accent%; }
QFrame[class="field"][invalid="true"] { border-color: %error%; }
QFrame[class="field"] QLineEdit {
    background: transparent;
    border: none;
    border-radius: 0;
    padding: 0;
    margin: 0;
    min-height: 0;
    font-size: 12.5px;
}
QFrame[class="field"] QLineEdit:focus { border: none; }
QFrame[class="field"] QLineEdit::placeholder { color: %placeholder%; }

QComboBox { padding-right: 28px; }
QComboBox::drop-down { border: none; width: 24px; background: transparent; }
QComboBox::down-arrow {
    image: none;
    width: 0;
    height: 0;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid %text2%;
}
QComboBox QAbstractItemView {
    background: %card%;
    border: 1px solid %hairline%;
    border-radius: 10px;
    padding: 4px;
    selection-background-color: %secondaryContainer%;
    selection-color: %onSecondaryContainer%;
}

/* ---- segmented ---- */
QFrame[class="seg"] {
    background: %fieldBg%;
    border: 1px solid %fieldBorder%;
    border-radius: 11px;
}
QPushButton[class="segItem"] {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 8px;
    padding: 4px 10px;
    min-height: 24px;
    color: %text2%;
    font-size: 12px;
    font-weight: 500;
}
QPushButton[class="segItem"]:hover { background: %hover%; color: %text%; }
QPushButton[class="segItem"]:checked {
    background: %card%;
    border: 1px solid %hairline%;
    color: %text%;
}
QPushButton[class="segItem"]:disabled { color: %disabledText%; }

/* ---- списки и таблицы ---- */
QTreeView, QTableView, QListView {
    background: transparent;
    border: none;
    alternate-background-color: transparent;
    gridline-color: transparent;
    selection-background-color: transparent;
    selection-color: %text%;
}
QTreeView#accountTree::item, QTreeView#accountTree::item:hover,
QTreeView#accountTree::item:selected { background: transparent; color: %text%; }
QTreeView::branch { background: transparent; }
QHeaderView::section {
    background: transparent;
    border: none;
    border-bottom: 1px solid %hairline%;
    padding: 7px 8px 8px 8px;
    color: %text3%;
    font-size: 11px;
    font-weight: 400;
}
QTableCornerButton::section { background: transparent; border: none; }

/* ---- скроллбары ---- */
QScrollBar:vertical { background: transparent; width: 10px; margin: 3px 2px; }
QScrollBar::handle:vertical {
    background: %scrollHandle%;
    border-radius: 3px;
    min-height: 36px;
}
QScrollBar::handle:vertical:hover { background: %scrollHandleHover%; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px 3px; }
QScrollBar::handle:horizontal {
    background: %scrollHandle%;
    border-radius: 3px;
    min-width: 36px;
}
QScrollBar::handle:horizontal:hover { background: %scrollHandleHover%; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* ---- меню ---- */
QMenu {
    background: %card%;
    border: 1px solid %hairline%;
    border-radius: 12px;
    padding: 5px;
}
QMenu::item {
    padding: 8px 22px 8px 10px;
    border-radius: 7px;
    color: %text%;
    font-size: 12px;
}
QMenu::item:selected { background: %hover%; color: %text%; }
QMenu::item:disabled { color: %disabledText%; }
QMenu::separator { height: 1px; background: %hairline%; margin: 4px 8px; }
QMenu::icon { padding-left: 6px; }

/* ---- прочее ---- */
QToolTip {
    background: %tooltipBase%;
    color: %tooltipText%;
    border: none;
    border-radius: 7px;
    padding: 6px 9px;
    font-size: 11px;
}
QStatusBar { background: transparent; color: %text3%; border: none; }
QTabWidget::pane { border: none; top: 2px; }
QTabBar::tab {
    background: transparent;
    color: %text2%;
    padding: 8px 10px;
    margin-right: 2px;
    border: none;
    border-bottom: 2px solid transparent;
    font-size: 12px;
    font-weight: 500;
}
QTabBar::tab:hover { color: %text%; }
QTabBar::tab:selected { color: %text%; border-bottom: 2px solid %accent%; }
QSplitter::handle { background: %hairline%; }
QFrame[class="divider"] { background: %hairline%; }

/* ---- stepper ---- */
QFrame[class="stepper"] {
    background: %fieldBg%;
    border: 1px solid %fieldBorder%;
    border-radius: 9px;
}
QPushButton[class="stepBtn"] {
    background: transparent;
    border: none;
    border-radius: 6px;
    padding: 0;
    min-width: 0;
    min-height: 0;
}
QPushButton[class="stepBtn"]:hover { background: %hover%; }
QPushButton[class="stepBtn"]:disabled { background: transparent; }
QLabel[class="stepperValue"] {
    font-size: 12px;
    font-weight: 500;
    color: %text%;
}
)");
    const auto tokens = qssTokens(c);
    for (auto it = tokens.constBegin(); it != tokens.constEnd(); ++it)
        qss.replace(QStringLiteral("%") + it.key() + QStringLiteral("%"), it.value());
    return qss;
}

void applyPalette(const Palette& c)
{
    mutableCurrent() = c;
    qApp->setPalette(qtPalette(c));
    qApp->setStyleSheet(buildQss(c));
    icons::refreshAllWidgets();
}

} // namespace

const Palette& currentPalette()
{
    return mutableCurrent();
}

bool isDark()
{
    return mutableCurrent().dark;
}

void applyTheme(const QString& name)
{
    applyPalette(name == QLatin1String("light") ? paletteLight() : paletteDark());
}

QString darkQss()
{
    return buildQss(paletteDark());
}

QString lightQss()
{
    return buildQss(paletteLight());
}

void apply(const QString& qss)
{
    qApp->setStyleSheet(qss);
}

QString loadCustom(const QString& jsonPath, QString* error)
{
    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("cannot-open");
        return darkQss();
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.isEmpty()) {
        if (error)
            *error = QStringLiteral("bad-json");
        return darkQss();
    }
    const bool dark = o[QStringLiteral("dark")].toBool(true);
    Palette p = dark ? paletteDark() : paletteLight();

    const QJsonObject colors = o[QStringLiteral("colors")].toObject();
    const auto assignColor = [&colors](QColor& dst, const char* key) {
        const QString v = colors[QString::fromLatin1(key)].toString();
        if (v.startsWith(QLatin1Char('#')))
            dst = QColor(v);
    };
    assignColor(p.accent, "accent");
    assignColor(p.primary, "accent");
    assignColor(p.onPrimary, "onAccent");
    assignColor(p.secondaryContainer, "panel");
    assignColor(p.onSecondaryContainer, "textDim");
    assignColor(p.error, "danger");
    assignColor(p.success, "success");
    assignColor(p.warning, "warning");
    p.primaryHover = hoverOf(p.primary, p.dark);
    p.primaryPressed = pressedOf(p.primary, p.dark);
    p.errorHover = hoverOf(p.error, p.dark);
    p.errorPressed = pressedOf(p.error, p.dark);
    return buildQss(p);
}

} // namespace ex::ui::theme

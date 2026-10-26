#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QScrollArea;

namespace ex::ui {

class AppContext;
class Stepper;
class Switch;
class ThemeOption;

// Настройки: внешний вид, мониторинг, параметры запуска.
class SettingsView : public QWidget {
    Q_OBJECT
public:
    explicit SettingsView(AppContext& ctx, QWidget* parent = nullptr);

    void setThemeName(const QString& name);

signals:
    void themeChangeRequested(const QString& name);

private slots:
    void save();

private:
    void buildUi();
    void markDirty();

    AppContext& m_ctx;
    QScrollArea* m_scroll = nullptr;
    ThemeOption* m_darkTheme = nullptr;
    ThemeOption* m_lightTheme = nullptr;
    Switch* m_startMinimized = nullptr;
    Stepper* m_monitorInterval = nullptr;
    Switch* m_presenceEnabled = nullptr;
    Stepper* m_launchConcurrency = nullptr;
    Stepper* m_launchDelay = nullptr;
    QLabel* m_savedHint = nullptr;
    QString m_themeName = QStringLiteral("dark");
};

} // namespace ex::ui

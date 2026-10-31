#pragma once

#include <QMainWindow>

class QStackedWidget;
class QSystemTrayIcon;

namespace ex::ui {

class AccountsView;
class AppContext;
class NavRail;
class SettingsView;

// Main window: nav rail + section stack, tray, DWM title bar.
// The close button quits the application.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(AppContext& ctx, QWidget* parent = nullptr);

    void showFromTray();

protected:
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void buildUi();
    void buildTray();
    void applyTitlebarColor();
    void setTheme(const QString& name);
    void syncInstanceBadge();

    AppContext& m_ctx;
    NavRail* m_rail = nullptr;
    QStackedWidget* m_stack = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    AccountsView* m_accounts = nullptr;
    SettingsView* m_settings = nullptr;
};

} // namespace ex::ui

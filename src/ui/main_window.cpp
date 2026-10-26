#include "ui/main_window.h"

#include "ui/accounts/accounts_view.h"
#include "ui/analytics_view.h"
#include "ui/app_context.h"
#include "ui/instances/instances_view.h"
#include "ui/material/icons.h"
#include "ui/material/nav_rail.h"
#include "ui/settings_view.h"
#include "ui/theme.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QRadialGradient>
#include <QStackedWidget>
#include <QShowEvent>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

namespace ex::ui {
namespace {

// Фон рабочей области: графит + очень мягкие ирисовые ореолы из макета.
class ShellCanvas : public QWidget {
public:
    explicit ShellCanvas(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAutoFillBackground(false);
        setAttribute(Qt::WA_OpaquePaintEvent, true);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        const auto& c = theme::currentPalette();
        p.fillRect(rect(), c.surface);

        QColor topGlow = c.accent;
        topGlow.setAlpha(c.dark ? 26 : 20);
        QRadialGradient top(width() * 0.94, -height() * 0.06,
                            width() * 0.62);
        top.setColorAt(0.0, topGlow);
        top.setColorAt(1.0, QColor(topGlow.red(), topGlow.green(),
                                   topGlow.blue(), 0));
        p.fillRect(rect(), QBrush(top));

        QColor bottomGlow = c.primary;
        bottomGlow.setAlpha(c.dark ? 16 : 12);
        QRadialGradient bottom(-width() * 0.05, height() * 1.08,
                               width() * 0.44);
        bottom.setColorAt(0.0, bottomGlow);
        bottom.setColorAt(1.0, QColor(bottomGlow.red(), bottomGlow.green(),
                                      bottomGlow.blue(), 0));
        p.fillRect(rect(), QBrush(bottom));
    }
};

} // namespace

MainWindow::MainWindow(AppContext& ctx, QWidget* parent)
    : QMainWindow(parent)
    , m_ctx(ctx)
{
    buildUi();
    buildTray();

    m_ctx.monitor().start();

    setWindowTitle(QStringLiteral("ExAccountManager"));
    setWindowIcon(icons::appIcon());
    resize(1280, 780);
    setMinimumSize(1100, 680);
}

void MainWindow::buildUi()
{
    const QList<NavRail::Destination> destinations = {
        {QStringLiteral("accounts"), tr("Аккаунты")},
        {QStringLiteral("instances"), tr("Инстансы")},
        {QStringLiteral("analytics"), tr("Аналитика")},
        {QStringLiteral("settings"), tr("Настройки")},
    };

    m_rail = new NavRail(destinations);
    m_rail->setThemeName(theme::isDark() ? QStringLiteral("dark")
                                         : QStringLiteral("light"));

    m_stack = new QStackedWidget;
    m_stack->setObjectName(QStringLiteral("mainStack"));

    m_accounts = new AccountsView(m_ctx);
    m_accounts->setObjectName(QStringLiteral("pageRoot"));
    auto* instances = new InstancesView(m_ctx);
    instances->setObjectName(QStringLiteral("pageRoot"));
    auto* analytics = new AnalyticsView(m_ctx);
    analytics->setObjectName(QStringLiteral("pageRoot"));
    m_settings = new SettingsView(m_ctx);
    m_settings->setObjectName(QStringLiteral("pageRoot"));

    m_stack->addWidget(m_accounts);
    m_stack->addWidget(instances);
    m_stack->addWidget(analytics);
    m_stack->addWidget(m_settings);

    connect(m_rail, &NavRail::currentChanged, m_stack, &QStackedWidget::setCurrentIndex);
    connect(m_rail, &NavRail::themeChangeRequested, this, &MainWindow::setTheme);
    connect(m_settings, &SettingsView::themeChangeRequested,
            this, &MainWindow::setTheme);
    connect(m_accounts, &AccountsView::settingsRequested, this, [this]() {
        m_rail->setCurrentIndex(3);
    });

    connect(&m_ctx.registry(), &ex::instances::InstanceRegistry::instancesChanged,
            this, &MainWindow::syncInstanceBadge);
    syncInstanceBadge();

    auto* canvas = new ShellCanvas;
    auto* canvasLayout = new QHBoxLayout(canvas);
    canvasLayout->setContentsMargins(0, 0, 0, 0);
    canvasLayout->setSpacing(0);
    canvasLayout->addWidget(m_stack, 1);

    auto* central = new QWidget;
    auto* row = new QHBoxLayout(central);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    row->addWidget(m_rail);
    row->addWidget(canvas, 1);
    setCentralWidget(central);

    applyTitlebarColor();
}

void MainWindow::buildTray()
{
    m_tray = new QSystemTrayIcon(this);
    m_tray->setIcon(icons::appIcon());
    m_tray->setToolTip(QStringLiteral("ExAccountManager"));

    auto* menu = new QMenu(this);
    QAction* showAction = menu->addAction(tr("Показать"));
    connect(showAction, &QAction::triggered, this, &MainWindow::showFromTray);
    QAction* quitAction = menu->addAction(tr("Выход"));
    connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);
    m_tray->setContextMenu(menu);

    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick)
            showFromTray();
    });
    m_tray->show();
}

void MainWindow::setTheme(const QString& name)
{
    const QString themeName = name == QLatin1String("light")
        ? QStringLiteral("light") : QStringLiteral("dark");
    const bool dark = themeName == QLatin1String("dark");
    if (dark == theme::isDark()) {
        m_rail->setThemeName(themeName);
        if (m_settings)
            m_settings->setThemeName(themeName);
        return;
    }
    theme::applyTheme(themeName);
    m_ctx.settings().set(QStringLiteral("general.theme"), themeName);
    m_rail->setThemeName(themeName);
    if (m_settings)
        m_settings->setThemeName(themeName);
    applyTitlebarColor();
}

void MainWindow::syncInstanceBadge()
{
    m_rail->setInstanceCount(m_ctx.registry().instances().size());
}

void MainWindow::applyTitlebarColor()
{
#ifdef Q_OS_WIN
    if (const HWND hwnd = reinterpret_cast<HWND>(winId())) {
        const BOOL dark = theme::isDark() ? TRUE : FALSE;
        DwmSetWindowAttribute(hwnd, 20, &dark, sizeof dark);
        // Не вызываем SetWindowPos(SWP_FRAMECHANGED): он синхронно шлёт
        // WM_STYLECHANGED и рекурсивно возвращает нас в changeEvent().
    }
#endif
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    QTimer::singleShot(0, this, [this]() { applyTitlebarColor(); });
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::StyleChange
        || event->type() == QEvent::PaletteChange) {
        applyTitlebarColor();
    }
}

void MainWindow::showFromTray()
{
    show();
    setWindowState(windowState() & ~Qt::WindowMinimized);
    raise();
    activateWindow();
}

} // namespace ex::ui

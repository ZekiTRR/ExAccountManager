#include "ui/instances/instances_view.h"

#include "core/models/domain.h"
#include "platform/windows/ram_limits.h"
#include "platform/windows/window_mgr.h"
#include "ui/accounts/avatar_cache.h"
#include "ui/app_context.h"
#include "ui/instances/client_options_dialog.h"
#include "ui/material/controls.h"
#include "ui/material/icons.h"
#include "ui/theme.h"

#include <QButtonGroup>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>

namespace ex::ui {
namespace {

constexpr int kPidWidth = 62;
constexpr int kCpuWidth = 116;
constexpr int kRamWidth = 72;
constexpr int kPrivateWidth = 72;
constexpr int kStateWidth = 112;
constexpr int kActionsWidth = 128;

enum class RowAction { Focus = 0, Show, Hide, Trim, Kill, Toggle };

class AvatarWidget : public QWidget {
public:
    explicit AvatarWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(32, 32);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void setData(const QPixmap& pixmap, ex::model::PresenceType presence)
    {
        m_pixmap = pixmap;
        m_presence = presence;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawPixmap(rect(), m_pixmap);

        const auto& c = theme::currentPalette();
        QColor dot = c.presenceGame;
        if (m_presence == ex::model::PresenceType::Online)
            dot = c.presenceOnline;
        else if (m_presence == ex::model::PresenceType::InStudio)
            dot = c.presenceStudio;
        else if (m_presence == ex::model::PresenceType::Offline
                 || m_presence == ex::model::PresenceType::Unknown) {
            dot = c.outline;
            dot.setAlpha(130);
        }
        const QPointF center(width() - 3.0, height() - 3.0);
        p.setPen(QPen(c.surfaceContainer, 2.2));
        p.setBrush(dot);
        p.drawEllipse(center, 4.6, 4.6);
    }

private:
    QPixmap m_pixmap;
    ex::model::PresenceType m_presence = ex::model::PresenceType::Unknown;
};

class Meter : public QWidget {
public:
    explicit Meter(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumWidth(34);
        setFixedHeight(4);
    }

    void setValue(int value)
    {
        m_value = qBound(0, value, 100);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const auto& c = theme::currentPalette();
        p.setPen(Qt::NoPen);
        p.setBrush(c.surfaceContainerHighest);
        p.drawRoundedRect(rect(), 2, 2);
        if (m_value <= 0)
            return;
        const QRectF fill(0, 0, width() * m_value / 100.0, height());
        QLinearGradient g(0, 0, width(), 0);
        g.setColorAt(0.0, c.primary);
        g.setColorAt(1.0, c.accent);
        p.setBrush(g);
        p.drawRoundedRect(fill, 2, 2);
    }

private:
    int m_value = 0;
};

class Sparkline : public QWidget {
public:
    explicit Sparkline(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(158, 40);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void setValues(const QList<double>& values)
    {
        m_values = values;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (m_values.size() < 2)
            return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const auto& c = theme::currentPalette();
        const qreal step = width() / qMax(1, m_values.size() - 1);
        QPainterPath line;
        for (int i = 0; i < m_values.size(); ++i) {
            const qreal x = i * step;
            const qreal y = height() - 3 - qBound(0.0, m_values.at(i), 100.0)
                                           / 100.0 * (height() - 8);
            if (i == 0)
                line.moveTo(x, y);
            else
                line.lineTo(x, y);
        }
        QPainterPath area = line;
        area.lineTo(width(), height());
        area.lineTo(0, height());
        area.closeSubpath();

        QLinearGradient fill(0, 0, 0, height());
        QColor accent = c.accent;
        accent.setAlpha(52);
        fill.setColorAt(0.0, accent);
        QColor clear = c.accent;
        clear.setAlpha(0);
        fill.setColorAt(1.0, clear);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawPath(area);

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(c.accent, 1.5, Qt::SolidLine, Qt::RoundCap,
                      Qt::RoundJoin));
        p.drawPath(line);
    }

private:
    QList<double> m_values;
};

class InstanceRow : public QFrame {
public:
    explicit InstanceRow(QWidget* parent = nullptr)
        : QFrame(parent)
    {
        setProperty("class", QStringLiteral("instRow"));
        setProperty("selected", false);
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(58);

        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(14, 0, 14, 0);
        row->setSpacing(10);

        auto* accountCell = new QWidget(this);
        auto* accountLayout = new QHBoxLayout(accountCell);
        accountLayout->setContentsMargins(0, 0, 0, 0);
        accountLayout->setSpacing(10);
        m_avatar = new AvatarWidget(accountCell);
        accountLayout->addWidget(m_avatar);
        auto* copy = new QVBoxLayout;
        copy->setContentsMargins(0, 0, 0, 0);
        copy->setSpacing(1);
        m_name = controls::label(QString(), QStringLiteral("instName"));
        m_user = controls::label(QString(), QStringLiteral("instUser"));
        copy->addWidget(m_name);
        copy->addWidget(m_user);
        accountLayout->addLayout(copy, 1);
        row->addWidget(accountCell, 1);

        m_pid = controls::label(QString(), QStringLiteral("mono"));
        m_pid->setFixedWidth(kPidWidth);
        row->addWidget(m_pid);

        auto* cpuCell = new QWidget(this);
        cpuCell->setFixedWidth(kCpuWidth);
        auto* cpuLayout = new QHBoxLayout(cpuCell);
        cpuLayout->setContentsMargins(0, 0, 0, 0);
        cpuLayout->setSpacing(8);
        m_meter = new Meter(cpuCell);
        m_cpu = controls::label(QString(), QStringLiteral("instValue"));
        m_cpu->setFixedWidth(38);
        m_cpu->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        cpuLayout->addWidget(m_meter, 1);
        cpuLayout->addWidget(m_cpu);
        row->addWidget(cpuCell);

        m_ram = controls::label(QString(), QStringLiteral("instValue"));
        m_ram->setFixedWidth(kRamWidth);
        row->addWidget(m_ram);

        m_private = controls::label(QString(), QStringLiteral("instValue"));
        m_private->setFixedWidth(kPrivateWidth);
        row->addWidget(m_private);

        auto* stateCell = new QWidget(this);
        stateCell->setFixedWidth(kStateWidth);
        auto* stateLayout = new QHBoxLayout(stateCell);
        stateLayout->setContentsMargins(0, 0, 0, 0);
        stateLayout->setSpacing(0);

        m_stateChip = new QFrame(stateCell);
        m_stateChip->setProperty("class", QStringLiteral("stateChip"));
        m_stateChip->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        auto* chipLayout = new QHBoxLayout(m_stateChip);
        chipLayout->setContentsMargins(8, 3, 8, 3);
        chipLayout->setSpacing(5);
        m_stateDot = new controls::StatusDot(5, m_stateChip);
        m_stateDot->setTone(QStringLiteral("success"));
        chipLayout->addWidget(m_stateDot, 0, Qt::AlignVCenter);
        m_stateLabel = controls::label(QString(),
                                       QStringLiteral("stateChipText"));
        chipLayout->addWidget(m_stateLabel);
        stateLayout->addWidget(m_stateChip, 0,
                               Qt::AlignLeft | Qt::AlignVCenter);
        stateLayout->addStretch(1);
        row->addWidget(stateCell);

        auto* actions = new QWidget(this);
        actions->setFixedWidth(kActionsWidth);
        auto* actionsLayout = new QHBoxLayout(actions);
        actionsLayout->setContentsMargins(0, 0, 0, 0);
        actionsLayout->setSpacing(1);
        actionsLayout->addStretch(1);
        m_focus = makeAction(actions, QStringLiteral("focus"), tr("Фокус"),
                             RowAction::Focus);
        m_toggle = makeAction(actions, QStringLiteral("eye"), tr("Скрыть окно"),
                              RowAction::Toggle);

        m_trim = makeAction(actions, QStringLiteral("ram"), tr("Тримминг ОЗУ"),
                            RowAction::Trim);
        m_kill = makeAction(actions, QStringLiteral("power"), tr("Закрыть клиент"),
                            RowAction::Kill);
        actionsLayout->addWidget(m_focus);
        actionsLayout->addWidget(m_toggle);
        actionsLayout->addWidget(m_trim);
        actionsLayout->addWidget(m_kill);
        row->addWidget(actions);

        // Clicking the row itself (not the buttons) selects the window.
        for (QLabel* label : findChildren<QLabel*>())
            label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_avatar->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_meter->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

    std::function<void(qint64, int)> action;
    std::function<void(qint64, Qt::KeyboardModifiers)> selectRequest;

    qint64 pid() const { return m_pidValue; }

    void setSelected(bool selected)
    {
        if (m_selected == selected)
            return;
        m_selected = selected;
        setProperty("selected", selected);
        controls::repolish(this);
    }

    void setWindowHidden(bool hidden)
    {
        m_windowHidden = hidden;
        icons::setGlyph(m_toggle,
                        hidden ? QStringLiteral("eye-off") : QStringLiteral("eye"),
                        QStringLiteral("muted"));
        m_toggle->setToolTip(hidden ? tr("Показать окно") : tr("Скрыть окно"));
        m_toggle->setAccessibleName(m_toggle->toolTip());
    }

    void setInstance(const ex::model::InstanceInfo& instance,
                     const ex::model::Account& account,
                     ex::model::PresenceType presence,
                     const QPixmap& avatar)
    {
        m_pidValue = instance.pid;
        const QString name = account.aliasOrUsername();
        m_name->setText(name);
        m_user->setText(account.username.isEmpty()
                            ? QString()
                            : QStringLiteral("@") + account.username);
        m_avatar->setData(avatar, presence);
        m_pid->setText(QString::number(instance.pid));
        m_cpu->setText(QStringLiteral("%1%").arg(instance.cpuPercent, 0, 'f', 1));
        m_meter->setValue(qMin(100, qRound(instance.cpuPercent * 3.0)));
        m_ram->setText(tr("%1 МБ").arg(instance.workingSetMb));
        m_private->setText(tr("%1 МБ").arg(instance.privateBytesMb));
        m_stateLabel->setText(instance.attributionExact ? tr("запущен нами")
                                                         : tr("чужой?"));
        m_stateDot->setTone(instance.attributionExact
                                ? QStringLiteral("success")
                                : QStringLiteral("busy"));
        m_stateChip->setProperty("foreign", !instance.attributionExact);
        controls::repolish(m_stateChip);
        setWindowHidden(m_windowHidden);
    }

private:
    QPushButton* makeAction(QWidget* parent, const QString& icon,
                            const QString& tooltip, RowAction actionId)
    {
        auto* button = new QPushButton(parent);
        button->setProperty("class", QStringLiteral("rowIcon"));
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(tooltip);
        button->setAccessibleName(tooltip);
        button->setFixedSize(28, 28);
        icons::setGlyph(button,
                        actionId == RowAction::Kill ? QStringLiteral("power") : icon,
                        actionId == RowAction::Kill ? QStringLiteral("error")
                                                     : QStringLiteral("muted"));
        connect(button, &QPushButton::clicked, this, [this, actionId]() {
            if (action)
                action(m_pidValue, static_cast<int>(actionId));
        });
        return button;
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && selectRequest) {
            selectRequest(m_pidValue, event->modifiers());
            event->accept();
            return;
        }
        QFrame::mousePressEvent(event);
    }

private:
    qint64 m_pidValue = 0;
    bool m_windowHidden = false;
    bool m_selected = false;
    AvatarWidget* m_avatar = nullptr;
    QLabel* m_name = nullptr;
    QLabel* m_user = nullptr;
    QLabel* m_pid = nullptr;
    Meter* m_meter = nullptr;
    QLabel* m_cpu = nullptr;
    QLabel* m_ram = nullptr;
    QLabel* m_private = nullptr;
    QFrame* m_stateChip = nullptr;
    controls::StatusDot* m_stateDot = nullptr;
    QLabel* m_stateLabel = nullptr;
    QPushButton* m_focus = nullptr;
    QPushButton* m_toggle = nullptr;
    QPushButton* m_trim = nullptr;
    QPushButton* m_kill = nullptr;
};

QLabel* fixedHead(const QString& text, int width)
{
    auto* label = controls::label(text, QStringLiteral("tableHead"));
    label->setFixedWidth(width);
    return label;
}

QString clientsWord(int count)
{
    const int mod10 = count % 10;
    const int mod100 = count % 100;
    if (mod10 == 1 && mod100 != 11)
        return QObject::tr("клиент");
    if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14))
        return QObject::tr("клиента");
    return QObject::tr("клиентов");
}

} // namespace

InstancesView::InstancesView(AppContext& ctx, QWidget* parent)
    : QWidget(parent)
    , m_ctx(ctx)
{
    buildUi();
    connect(&ctx.monitor(), &ex::instances::InstanceMonitor::telemetryUpdated,
            this, &InstancesView::reload);
    connect(&ctx.registry(), &ex::instances::InstanceRegistry::instancesChanged,
            this, &InstancesView::reload);
    connect(&m_avatars, &AvatarCache::avatarReady, this,
            &InstancesView::reload);
    reload();
}

void InstancesView::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(26, 22, 26, 24);
    root->setSpacing(14);

    auto* header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 4);
    auto* copy = new QVBoxLayout;
    copy->setContentsMargins(0, 0, 0, 0);
    copy->setSpacing(5);
    copy->addWidget(controls::label(tr("Инстансы"),
                                    QStringLiteral("pageTitle")));
    auto* subRow = new QHBoxLayout;
    subRow->setContentsMargins(0, 0, 0, 0);
    subRow->setSpacing(7);
    auto* dot = new controls::StatusDot(6);
    dot->setTone(QStringLiteral("success"));
    subRow->addWidget(dot, 0, Qt::AlignVCenter);
    subRow->addWidget(controls::label(tr("мониторинг активен"),
                                      QStringLiteral("pageSub")));
    subRow->addWidget(controls::label(QStringLiteral("·"),
                                      QStringLiteral("pageSub")));
    m_subtitle = controls::label(QString(), QStringLiteral("pageSub"));
    subRow->addWidget(m_subtitle);
    subRow->addStretch(1);
    copy->addLayout(subRow);
    header->addLayout(copy, 1);

    m_optionsButton = controls::button(tr("Параметры"), QString(), this);
    m_optionsButton->setFixedHeight(38);
    m_optionsButton->setToolTip(
        tr("Размер окон и лимиты CPU/ОЗУ для выбранных или всех клиентов"));
    icons::setGlyph(m_optionsButton, QStringLiteral("settings"));
    connect(m_optionsButton, &QPushButton::clicked, this,
            &InstancesView::showClientOptions);
    header->addWidget(m_optionsButton, 0, Qt::AlignBottom);

    auto* arrangeBtn = controls::button(tr("Разложить сеткой"),
                                        QString(), this);
    arrangeBtn->setFixedHeight(38);
    icons::setGlyph(arrangeBtn, QStringLiteral("grid"));
    connect(arrangeBtn, &QPushButton::clicked, this,
            [this]() {
        QList<qint64> pids;
        for (const auto& inst : m_ctx.registry().instances())
            pids.append(inst.pid);
        ex::platform::WindowMgr::arrangeGrid(pids);
        setStatus(tr("Окна разложены сеткой"), QStringLiteral("success"));
    });
    auto* closeAllBtn = controls::button(tr("Закрыть все"),
                                         QStringLiteral("danger"), this);
    closeAllBtn->setFixedHeight(38);
    icons::setGlyph(closeAllBtn, QStringLiteral("power"),
                    QStringLiteral("error"));
    connect(closeAllBtn, &QPushButton::clicked, this, [this]() {
        m_ctx.launcher().closeAll();
        setStatus(tr("Закрытие всех клиентов…"), QStringLiteral("busy"));
    });
    header->addWidget(arrangeBtn, 0, Qt::AlignBottom);
    header->addWidget(closeAllBtn, 0, Qt::AlignBottom);
    root->addLayout(header);

    // ---- live strip
    auto* strip = controls::card(this);
    strip->setFixedHeight(66);
    auto* stripLayout = new QHBoxLayout(strip);
    stripLayout->setContentsMargins(16, 10, 16, 10);
    stripLayout->setSpacing(0);

    const auto addMetric = [stripLayout, strip](const QString& iconName,
                                                 const QString& caption,
                                                 QLabel** valueOut) {
        auto* cell = new QWidget(strip);
        auto* layout = new QVBoxLayout(cell);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(1);
        auto* captionRow = new QHBoxLayout;
        captionRow->setContentsMargins(0, 0, 0, 0);
        captionRow->setSpacing(5);
        auto* icon = new icons::IconLabel(iconName, 13, cell);
        icon->setTone(QStringLiteral("muted"));
        captionRow->addWidget(icon);
        captionRow->addWidget(controls::label(caption,
                                              QStringLiteral("metricLabel")));
        captionRow->addStretch(1);
        layout->addLayout(captionRow);
        auto* value = controls::label(QStringLiteral("0"),
                                      QStringLiteral("metricValue"));
        layout->addWidget(value);
        *valueOut = value;
        stripLayout->addWidget(cell, 0);
        cell->setMinimumWidth(98);
    };

    addMetric(QStringLiteral("accounts"), tr("Клиенты"), &m_clientsValue);
    auto* sep1 = new QFrame(strip);
    sep1->setProperty("class", QStringLiteral("separator"));
    sep1->setFixedSize(1, 30);
    stripLayout->addSpacing(14);
    stripLayout->addWidget(sep1);
    stripLayout->addSpacing(14);
    addMetric(QStringLiteral("cpu"), tr("CPU"), &m_cpuValue);
    auto* sep2 = new QFrame(strip);
    sep2->setProperty("class", QStringLiteral("separator"));
    sep2->setFixedSize(1, 30);
    stripLayout->addSpacing(14);
    stripLayout->addWidget(sep2);
    stripLayout->addSpacing(14);
    addMetric(QStringLiteral("ram"), tr("RAM"), &m_ramValue);
    stripLayout->addStretch(1);
    m_spark = new Sparkline(strip);
    stripLayout->addWidget(m_spark, 0, Qt::AlignVCenter);
    root->addWidget(strip);

    // ---- table
    auto* tableCard = controls::card(this);
    auto* tableLayout = new QVBoxLayout(tableCard);
    tableLayout->setContentsMargins(0, 0, 0, 0);
    tableLayout->setSpacing(0);

    auto* toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(16, 9, 12, 9);
    toolbar->setSpacing(8);
    toolbar->addWidget(controls::label(tr("Активные клиенты"),
                                       QStringLiteral("titleSmall")));
    m_tableCount = controls::label(QStringLiteral("0"),
                                   QStringLiteral("tertiary"));
    toolbar->addWidget(m_tableCount);
    auto* liveBadge = controls::label(tr("LIVE"),
                                     QStringLiteral("chipMint"));
    toolbar->addWidget(liveBadge);
    toolbar->addStretch(1);
    m_pollHint = controls::label(QString(), QStringLiteral("pageSub"));
    toolbar->addWidget(m_pollHint);
    auto* refreshBtn = controls::iconButton(
        QStringLiteral("refresh"), tr("Обновить метрики"), tableCard);
    connect(refreshBtn, &QPushButton::clicked, this, &InstancesView::reload);
    toolbar->addWidget(refreshBtn);
    tableLayout->addLayout(toolbar);

    // ---- bulk actions: selected windows / all windows
    auto* bulkBar = new QHBoxLayout;
    bulkBar->setContentsMargins(16, 0, 12, 7);
    bulkBar->setSpacing(2);

    const auto addBulkButton = [this, tableCard, bulkBar](
                                   const QString& text, const QString& icon,
                                   RowAction action, bool danger = false) {
        auto* button = controls::button(text,
                                        danger ? QStringLiteral("danger")
                                               : QStringLiteral("text"),
                                        tableCard);
        button->setFixedHeight(30);
        icons::setGlyph(button, icon,
                        danger ? QStringLiteral("error")
                               : QStringLiteral("auto"));
        connect(button, &QPushButton::clicked, this, [this, action]() {
            performBulkAction(static_cast<int>(action));
        });
        m_bulkButtons.append(button);
        bulkBar->addWidget(button);
    };
    addBulkButton(tr("Фокус"), QStringLiteral("focus"), RowAction::Focus);
    addBulkButton(tr("Показать"), QStringLiteral("eye"), RowAction::Show);
    addBulkButton(tr("Скрыть"), QStringLiteral("eye-off"), RowAction::Hide);
    addBulkButton(tr("Тримминг"), QStringLiteral("ram"), RowAction::Trim);
    addBulkButton(tr("Закрыть"), QStringLiteral("power"), RowAction::Kill, true);
    bulkBar->addStretch(1);

    m_selectionInfo = controls::label(QString(), QStringLiteral("pageSub"));
    bulkBar->addWidget(m_selectionInfo);
    bulkBar->addSpacing(8);

    auto* scope = new QFrame(tableCard);
    scope->setProperty("class", QStringLiteral("miniSeg"));
    auto* scopeLayout = new QHBoxLayout(scope);
    scopeLayout->setContentsMargins(2, 2, 2, 2);
    scopeLayout->setSpacing(0);
    auto* scopeGroup = new QButtonGroup(scope);
    scopeGroup->setExclusive(true);
    m_scopeSelected = new QPushButton(tr("Выбранные"), scope);
    m_scopeSelected->setProperty("class", QStringLiteral("miniSegItem"));
    m_scopeSelected->setCheckable(true);
    m_scopeSelected->setChecked(true);
    m_scopeSelected->setCursor(Qt::PointingHandCursor);
    m_scopeSelected->setFixedHeight(24);
    m_scopeSelected->setToolTip(tr("Действия применяются к выбранным окнам"));
    m_scopeAllBtn = new QPushButton(tr("Все окна"), scope);
    m_scopeAllBtn->setProperty("class", QStringLiteral("miniSegItem"));
    m_scopeAllBtn->setCheckable(true);
    m_scopeAllBtn->setCursor(Qt::PointingHandCursor);
    m_scopeAllBtn->setFixedHeight(24);
    m_scopeAllBtn->setToolTip(tr("Действия применяются ко всем клиентам"));
    scopeGroup->addButton(m_scopeSelected);
    scopeGroup->addButton(m_scopeAllBtn);
    scopeLayout->addWidget(m_scopeSelected);
    scopeLayout->addWidget(m_scopeAllBtn);
    connect(m_scopeSelected, &QPushButton::clicked, this, [this]() {
        m_scopeAll = false;
        updateBulkUi();
    });
    connect(m_scopeAllBtn, &QPushButton::clicked, this, [this]() {
        m_scopeAll = true;
        updateBulkUi();
    });
    bulkBar->addWidget(scope);
    tableLayout->addLayout(bulkBar);

    auto* tableHeader = new QFrame(tableCard);
    tableHeader->setProperty("class", QStringLiteral("tableHeader"));
    tableHeader->setFixedHeight(34);
    auto* headLayout = new QHBoxLayout(tableHeader);
    headLayout->setContentsMargins(14, 0, 14, 0);
    headLayout->setSpacing(10);
    headLayout->addWidget(controls::label(tr("Аккаунт"),
                                          QStringLiteral("tableHead")), 1);
    headLayout->addWidget(fixedHead(tr("PID"), kPidWidth));
    headLayout->addWidget(fixedHead(tr("CPU"), kCpuWidth));
    headLayout->addWidget(fixedHead(tr("RAM"), kRamWidth));
    headLayout->addWidget(fixedHead(tr("Приват"), kPrivateWidth));
    headLayout->addWidget(fixedHead(tr("Статус"), kStateWidth));
    auto* actionHead = controls::label(QString(), QStringLiteral("tableHead"));
    actionHead->setFixedWidth(kActionsWidth);
    headLayout->addWidget(actionHead);
    tableLayout->addWidget(tableHeader);

    m_scroll = new QScrollArea(tableCard);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* rowsHost = new QWidget(m_scroll);
    rowsHost->setObjectName(QStringLiteral("pageRoot"));
    m_rowsLayout = new QVBoxLayout(rowsHost);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(0);
    m_scroll->setWidget(rowsHost);
    tableLayout->addWidget(m_scroll, 1);
    root->addWidget(tableCard, 1);

    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(2, 0, 2, 0);
    footer->setSpacing(10);
    auto* hint = controls::label(
        tr("Клик по строке выбирает окно, Ctrl/Shift — несколько. "
           "Действия применяются к выбранным или ко всем."),
        QStringLiteral("pageSub"));
    hint->setWordWrap(true);
    footer->addWidget(hint, 1);
    m_status = controls::label(QString(), QStringLiteral("pageSub"));
    m_status->hide();
    footer->addWidget(m_status);
    root->addLayout(footer);

    updatePollHint();
}

void InstancesView::updatePollHint()
{
    const int seconds =
        qMax(1, m_ctx.settings().get(QStringLiteral("monitor.intervalMs")).toInt()
                    / 1000);
    if (m_pollHint)
        m_pollHint->setText(tr("Интервал: %1 с").arg(seconds));
}

void InstancesView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    updatePollHint();
}

void InstancesView::setStatus(const QString& text, const QString& tone)
{
    if (!m_status)
        return;
    m_status->setText(text);
    m_status->setProperty("class",
                          tone == QLatin1String("error") ? QStringLiteral("errorText")
                          : tone == QLatin1String("success") ? QStringLiteral("successText")
                                                             : QStringLiteral("pageSub"));
    controls::repolish(m_status);
    m_status->setVisible(!text.isEmpty());
}

void InstancesView::reload()
{
    const auto instances = m_ctx.registry().instances();
    double totalCpu = 0.0;
    qint64 totalRam = 0;

    QList<qint64> pids;
    pids.reserve(instances.size());
    for (const auto& inst : instances) {
        pids.append(inst.pid);
        totalCpu += inst.cpuPercent;
        totalRam += inst.workingSetMb;
    }

    if (!m_rowsBuilt || pids != m_order) {
        // Order or contents changed — rebuild the strings completely.
        m_rows.clear();
        while (QLayoutItem* item = m_rowsLayout->takeAt(0)) {
            if (item->widget())
                item->widget()->deleteLater();
            delete item;
        }
        m_order = pids;
        for (auto it = m_selectedPids.begin(); it != m_selectedPids.end();) {
            if (!m_order.contains(*it))
                it = m_selectedPids.erase(it);
            else
                ++it;
        }
        if (m_order.isEmpty()) {
            m_selectedPids.clear();
            m_selectionAnchor = 0;
        }
        for (qint64 pid : pids) {
            auto* row = new InstanceRow(m_scroll->widget());
            row->action = [this](qint64 rowPid, int action) {
                handleRowAction(rowPid, action);
            };
            row->selectRequest = [this](qint64 rowPid,
                                        Qt::KeyboardModifiers modifiers) {
                handleRowSelection(rowPid, modifiers);
            };
            m_rows.insert(pid, row);
            m_rowsLayout->addWidget(row);
        }
        if (pids.isEmpty()) {
            auto* empty = controls::label(
                tr("Клиенты не запущены. Запустите аккаунты — они появятся здесь."),
                QStringLiteral("pageSub"));
            empty->setAlignment(Qt::AlignCenter);
            empty->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            m_rowsLayout->addWidget(empty, 1);
        } else {
            m_rowsLayout->addStretch(1);
        }
        m_rowsBuilt = true;
    }

    for (const auto& inst : instances) {
        auto* row = static_cast<InstanceRow*>(m_rows.value(inst.pid, nullptr));
        if (!row)
            continue;
        ex::model::Account account;
        if (inst.accountId) {
            if (const auto stored = m_ctx.store().accountById(inst.accountId))
                account = *stored;
        }
        if (account.id == 0) {
            account.username = inst.accountUsername;
            account.displayName = inst.accountUsername.isEmpty()
                ? tr("PID %1").arg(inst.pid) : inst.accountUsername;
        }
        const auto presence = m_ctx.accounts().presenceFor(inst.accountId).type;
        const QString fallback = account.username.isEmpty()
            ? account.aliasOrUsername() : account.username;
        const QPixmap avatar =
            m_avatars.request(QUrl(account.avatarUrl), fallback);
        row->setInstance(inst, account, presence, avatar);
        row->setWindowHidden(m_hiddenWindows.contains(inst.pid));
        row->setSelected(m_selectedPids.contains(inst.pid));
    }

    m_clientsValue->setText(QString::number(instances.size()));
    m_cpuValue->setText(QStringLiteral("%1%").arg(totalCpu, 0, 'f', 1));
    m_ramValue->setText(tr("%1 ГБ").arg(totalRam / 1024.0, 0, 'f', 1));
    m_tableCount->setText(QString::number(instances.size()));
    m_subtitle->setText(tr("%1 %2 · интервал опроса %3 с")
                            .arg(instances.size())
                            .arg(clientsWord(instances.size()))
                            .arg(qMax(1, m_ctx.settings().get(
                                             QStringLiteral("monitor.intervalMs"))
                                             .toInt() / 1000)));
    updatePollHint();

    m_cpuHistory.append(totalCpu);
    while (m_cpuHistory.size() > 40)
        m_cpuHistory.removeFirst();
    static_cast<Sparkline*>(m_spark)->setValues(m_cpuHistory);
    updateBulkUi();
}

void InstancesView::handleRowAction(qint64 pid, int action)
{
    if (pid <= 0)
        return;
    QString name = tr("PID %1").arg(pid);
    if (const auto accountId = m_ctx.registry().accountIdForPid(pid)) {
        if (const auto account = m_ctx.store().accountById(*accountId))
            name = account->aliasOrUsername();
    }

    switch (static_cast<RowAction>(action)) {
    case RowAction::Focus:
        ex::platform::WindowMgr::focus(pid);
        setStatus(tr("Фокус: %1").arg(name));
        break;
    case RowAction::Show:
    case RowAction::Hide:
    case RowAction::Toggle: {
        const bool hide = static_cast<RowAction>(action) == RowAction::Hide
            || (static_cast<RowAction>(action) == RowAction::Toggle
                && !m_hiddenWindows.contains(pid));
        if (hide) {
            ex::platform::WindowMgr::hide(pid);
            m_hiddenWindows.insert(pid);
        } else {
            ex::platform::WindowMgr::showNoActivate(pid);
            m_hiddenWindows.remove(pid);
        }
        if (auto* row = static_cast<InstanceRow*>(m_rows.value(pid, nullptr)))
            row->setWindowHidden(hide);
        setStatus(tr("Окно %1 %2").arg(name, hide ? tr("скрыто")
                                                  : tr("показано")));
        break;
    }
    case RowAction::Trim:
        ex::platform::RamLimiter::trimWorkingSet(pid);
        setStatus(tr("Рабочий набор %1 сокращён").arg(name),
                  QStringLiteral("success"));
        break;
    case RowAction::Kill:
        if (const auto accountId = m_ctx.registry().accountIdForPid(pid)) {
            m_ctx.launcher().closeInstance(*accountId);
            setStatus(tr("Закрытие клиента %1…").arg(name),
                      QStringLiteral("busy"));
        } else {
            setStatus(tr("Инстанс не атрибуцирован — закрытие запрещено"),
                      QStringLiteral("error"));
        }
        break;
    }
}

void InstancesView::handleRowSelection(qint64 pid,
                                       Qt::KeyboardModifiers modifiers)
{
    const int index = m_order.indexOf(pid);
    if (index < 0)
        return;

    if ((modifiers & Qt::ShiftModifier) && m_selectionAnchor != 0
        && m_order.contains(m_selectionAnchor)) {
        const int anchor = m_order.indexOf(m_selectionAnchor);
        const int first = qMin(anchor, index);
        const int last = qMax(anchor, index);
        m_selectedPids.clear();
        for (int i = first; i <= last; ++i)
            m_selectedPids.insert(m_order.at(i));
    } else if (modifiers & Qt::ControlModifier) {
        if (m_selectedPids.contains(pid))
            m_selectedPids.remove(pid);
        else
            m_selectedPids.insert(pid);
        m_selectionAnchor = pid;
    } else {
        m_selectedPids.clear();
        m_selectedPids.insert(pid);
        m_selectionAnchor = pid;
    }

    m_scopeAll = false;
    for (auto it = m_rows.constBegin(); it != m_rows.constEnd(); ++it) {
        if (auto* row = static_cast<InstanceRow*>(it.value()))
            row->setSelected(m_selectedPids.contains(it.key()));
    }
    updateBulkUi();
}

QList<qint64> InstancesView::actionPids() const
{
    if (m_scopeAll)
        return m_order;
    return selectedPids();
}

QList<qint64> InstancesView::selectedPids() const
{
    QList<qint64> targets;
    for (qint64 pid : m_order) {
        if (m_selectedPids.contains(pid))
            targets.append(pid);
    }
    return targets;
}

void InstancesView::showClientOptions()
{
    if (m_order.isEmpty()) {
        setStatus(tr("Нет запущенных клиентов"), QStringLiteral("error"));
        return;
    }

    const int windowWidth = m_ctx.settings()
        .get(QStringLiteral("launcher.windowWidth"), 800).toInt();
    const int windowHeight = m_ctx.settings()
        .get(QStringLiteral("launcher.windowHeight"), 600).toInt();
    const int cpuPercent = m_ctx.settings()
        .get(QStringLiteral("client.cpuPercent"), 0).toInt();
    const qint64 ramMb = m_ctx.settings()
        .get(QStringLiteral("client.ramMb"), 0).toLongLong();

    ClientOptionsDialog dialog(selectedPids(), m_order, m_scopeAll,
                               windowWidth, windowHeight, cpuPercent, ramMb,
                               this);
    dialog.exec();

    m_ctx.settings().set(QStringLiteral("launcher.windowWidth"),
                         dialog.windowSize().width());
    m_ctx.settings().set(QStringLiteral("launcher.windowHeight"),
                         dialog.windowSize().height());
    m_ctx.settings().set(QStringLiteral("client.cpuPercent"),
                         dialog.cpuPercent());
    m_ctx.settings().set(QStringLiteral("client.ramMb"),
                         dialog.ramMb());
}

void InstancesView::updateBulkUi()
{
    if (!m_selectionInfo || !m_scopeSelected || !m_scopeAllBtn)
        return;

    const bool hasClients = !m_order.isEmpty();
    const bool hasTargets =
        hasClients && (m_scopeAll || !m_selectedPids.isEmpty());
    for (QPushButton* button : m_bulkButtons)
        button->setEnabled(hasTargets);
    if (m_optionsButton)
        m_optionsButton->setEnabled(hasClients);

    if (!hasClients) {
        m_selectionInfo->setText(tr("Нет клиентов"));
    } else if (m_scopeAll) {
        m_selectionInfo->setText(tr("Все клиенты · %1").arg(m_order.size()));
    } else if (m_selectedPids.isEmpty()) {
        m_selectionInfo->setText(tr("Клик по строке — выбрать"));
    } else {
        m_selectionInfo->setText(tr("Выбрано: %1").arg(m_selectedPids.size()));
    }

    const QSignalBlocker blockSelected(m_scopeSelected);
    const QSignalBlocker blockAll(m_scopeAllBtn);
    m_scopeSelected->setChecked(!m_scopeAll);
    m_scopeAllBtn->setChecked(m_scopeAll);
}

void InstancesView::performBulkAction(int action)
{
    const QList<qint64> targets = actionPids();
    if (targets.isEmpty()) {
        setStatus(m_scopeAll ? tr("Нет клиентов")
                             : tr("Сначала выберите клиентов"),
                  QStringLiteral("error"));
        return;
    }

    switch (static_cast<RowAction>(action)) {
    case RowAction::Focus:
        for (qint64 pid : targets)
            ex::platform::WindowMgr::focus(pid);
        setStatus(tr("Фокус: %1 %2").arg(targets.size())
                      .arg(clientsWord(targets.size())),
                  QStringLiteral("success"));
        break;
    case RowAction::Show:
    case RowAction::Hide: {
        const bool hide = static_cast<RowAction>(action) == RowAction::Hide;
        for (qint64 pid : targets) {
            if (hide) {
                ex::platform::WindowMgr::hide(pid);
                m_hiddenWindows.insert(pid);
            } else {
                ex::platform::WindowMgr::showNoActivate(pid);
                m_hiddenWindows.remove(pid);
            }
            if (auto* row = static_cast<InstanceRow*>(m_rows.value(pid, nullptr)))
                row->setWindowHidden(hide);
        }
        setStatus(hide
                      ? tr("Скрыто окон: %1").arg(targets.size())
                      : tr("Показано окон: %1").arg(targets.size()),
                  QStringLiteral("success"));
        break;
    }
    case RowAction::Trim:
        for (qint64 pid : targets)
            ex::platform::RamLimiter::trimWorkingSet(pid);
        setStatus(tr("Тримминг ОЗУ выполнен для %1 окон").arg(targets.size()),
                  QStringLiteral("success"));
        break;
    case RowAction::Kill:
        if (m_scopeAll) {
            m_ctx.launcher().closeAll();
            setStatus(tr("Закрытие всех клиентов…"), QStringLiteral("busy"));
        } else {
            int closed = 0;
            int skipped = 0;
            for (qint64 pid : targets) {
                if (const auto accountId = m_ctx.registry().accountIdForPid(pid)) {
                    m_ctx.launcher().closeInstance(*accountId);
                    ++closed;
                } else {
                    ++skipped;
                }
            }
            if (closed == 0) {
                setStatus(tr("Инстансы не атрибуцированы — закрытие запрещено"),
                          QStringLiteral("error"));
            } else {
                QString message = tr("Закрытие клиентов: %1").arg(closed);
                if (skipped > 0)
                    message += tr(" · пропущено чужих: %1").arg(skipped);
                setStatus(message, skipped > 0 ? QStringLiteral("busy")
                                               : QStringLiteral("success"));
            }
        }
        break;
    default:
        break;
    }

    updateBulkUi();
}

} // namespace ex::ui

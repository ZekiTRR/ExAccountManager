#include "ui/settings_view.h"

#include "platform/windows/roblox_install.h"
#include "ui/app_context.h"
#include "ui/material/controls.h"
#include "ui/material/icons.h"
#include "ui/material/stepper.h"
#include "ui/material/switch.h"
#include "ui/theme.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QStringList>
#include <QVBoxLayout>

namespace ex::ui {
namespace {

// QComboBox paints the CSS triangle as a black square; draw the chevron
// theme icon instead of the native arrow.
class ThemedComboBox : public QComboBox {
public:
    using QComboBox::QComboBox;

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QComboBox::paintEvent(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const auto& c = theme::currentPalette();
        icons::paint(&p, QStringLiteral("chevron-down"),
                     QRectF(width() - 20.0, (height() - 14.0) / 2.0, 14, 14),
                     c.iconDim);
    }
};

QWidget* settingRow(const QString& title, const QString& description,
                    QWidget* control, QWidget* parent)
{
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 10, 0, 10);
    layout->setSpacing(16);

    auto* copy = new QVBoxLayout;
    copy->setContentsMargins(0, 0, 0, 0);
    copy->setSpacing(2);
    copy->addWidget(controls::label(title, QStringLiteral("setLabel")));
    auto* desc = controls::label(description, QStringLiteral("setDesc"));
    desc->setWordWrap(true);
    copy->addWidget(desc);
    layout->addLayout(copy, 1);
    layout->addWidget(control, 0, Qt::AlignVCenter);
    return row;
}

void addDivider(QVBoxLayout* layout)
{
    layout->addWidget(controls::separator());
}

QFrame* sectionCard(const QString& title, const QString& subtitle,
                    QWidget* parent)
{
    auto* card = controls::card(parent);
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 16, 18, 12);
    layout->setSpacing(0);
    layout->addWidget(controls::label(title, QStringLiteral("sectionTitle")));
    layout->addSpacing(2);
    layout->addWidget(controls::label(subtitle, QStringLiteral("sectionSub")));
    layout->addSpacing(8);
    return card;
}

QWidget* noteBox(const QString& iconName, const QString& text, QWidget* parent)
{
    auto* box = new QFrame(parent);
    box->setProperty("class", QStringLiteral("innerBox"));
    auto* layout = new QHBoxLayout(box);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(7);
    auto* icon = new icons::IconLabel(iconName, 13, box);
    icon->setTone(QStringLiteral("muted"));
    layout->addWidget(icon, 0, Qt::AlignTop);
    auto* label = controls::label(text, QStringLiteral("pageSub"));
    label->setWordWrap(true);
    layout->addWidget(label, 1);
    return box;
}

} // namespace

class ThemeOption : public QAbstractButton {
public:
    ThemeOption(bool dark, const QString& text, QWidget* parent = nullptr)
        : QAbstractButton(parent)
        , m_dark(dark)
    {
        setCheckable(true);
        setAutoExclusive(true);
        setCursor(Qt::PointingHandCursor);
        setMinimumHeight(126);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setText(text);
        setAccessibleName(text);
        setToolTip(text);
    }

    QSize sizeHint() const override { return {300, 126}; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const auto& c = theme::currentPalette();
        const QRectF outer = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const bool active = isChecked();

        QColor border = c.outlineVariant;
        if (active)
            border = c.accent;
        else if (underMouse())
            border = c.outline;
        p.setPen(QPen(border, active ? 1.6 : 1.0));
        p.setBrush(c.fieldBg);
        p.drawRoundedRect(outer, 12, 12);

        const QRectF preview = outer.adjusted(6, 6, -6, -39);
        drawPreview(p, preview);

        const QRectF labelRect(preview.left() + 3,
                               preview.bottom() + 10, preview.width() - 6, 22);
        icons::paint(&p, m_dark ? QStringLiteral("moon") : QStringLiteral("sun"),
                     QRectF(labelRect.left(), labelRect.center().y() - 7, 14, 14),
                     active ? c.accent : c.onSurfaceVariant);

        QFont f = font();
        f.setPixelSize(12);
        f.setWeight(active ? QFont::Medium : QFont::Normal);
        p.setFont(f);
        p.setPen(active ? c.onSurface : c.onSurfaceVariant);
        p.drawText(labelRect.adjusted(20, 0, -22, 0),
                   Qt::AlignVCenter | Qt::AlignLeft, text());

        const QPointF radioCenter(labelRect.right() - 6,
                                  labelRect.center().y());
        p.setPen(QPen(active ? c.accent : c.outline, 1.3));
        p.setBrush(active ? c.accent : Qt::NoBrush);
        p.drawEllipse(radioCenter, 7, 7);
        if (active) {
            p.setPen(Qt::NoPen);
            p.setBrush(c.onPrimary);
            p.drawEllipse(radioCenter, 2.5, 2.5);
        }

        if (hasFocus()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(c.accent, 1.5, Qt::DashLine));
            p.drawRoundedRect(outer.adjusted(-2, -2, 2, 2), 13, 13);
        }
    }

    void enterEvent(QEnterEvent* event) override
    {
        QAbstractButton::enterEvent(event);
        update();
    }

    void leaveEvent(QEvent* event) override
    {
        QAbstractButton::leaveEvent(event);
        update();
    }

private:
    void drawPreview(QPainter& p, const QRectF& rect)
    {
        const QColor bg = m_dark ? QColor(0x0B, 0x0D, 0x12)
                                 : QColor(0xF3, 0xF2, 0xF8);
        const QColor rail = m_dark ? QColor(0x0E, 0x11, 0x17)
                                   : QColor(0xF8, 0xF7, 0xFC);
        const QColor surface = m_dark ? QColor(0x15, 0x1A, 0x23)
                                      : QColor(0xFF, 0xFF, 0xFF);
        const QColor line = m_dark ? QColor(0x4A, 0x4F, 0x5F)
                                   : QColor(0xD6, 0xD2, 0xE2);
        const QColor accent = m_dark ? QColor(0x8D, 0x7B, 0xFF)
                                     : QColor(0x5B, 0x4B, 0xE0);
        const QColor selected = m_dark ? QColor(0x25, 0x20, 0x40)
                                       : QColor(0xEF, 0xEB, 0xFF);

        QPainterPath clip;
        clip.addRoundedRect(rect, 8, 8);
        p.save();
        p.setClipPath(clip);
        p.fillRect(rect, bg);

        p.fillRect(QRectF(rect.left(), rect.top(), 27, rect.height()), rail);
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawRoundedRect(QRectF(rect.left() + 9, rect.top() + 8, 9, 9), 3, 3);

        p.setBrush(line);
        for (int i = 0; i < 3; ++i) {
            p.drawRoundedRect(QRectF(rect.left() + 8,
                                     rect.top() + 26 + i * 9, 11, 3), 1.5, 1.5);
        }
        p.setBrush(m_dark ? QColor(0x3A, 0x3F, 0x4D)
                          : QColor(0xE4, 0xE0, 0xEE));
        p.drawRoundedRect(QRectF(rect.left() + 8,
                                 rect.top() + 61, 11, 3), 1.5, 1.5);

        const qreal contentX = rect.left() + 36;
        const qreal contentW = rect.width() - 47;
        p.setBrush(line);
        p.drawRoundedRect(QRectF(contentX, rect.top() + 9,
                                 contentW * 0.42, 5), 2.5, 2.5);

        p.setBrush(surface);
        p.setPen(QPen(m_dark ? QColor(0x2A, 0x2F, 0x3D)
                             : QColor(0xEA, 0xE7, 0xF1), 1));
        p.drawRoundedRect(QRectF(contentX, rect.top() + 23,
                                 contentW, 18), 5, 5);
        p.setBrush(selected);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(contentX + 4, rect.top() + 27,
                                 contentW - 8, 10), 3, 3);
        p.setPen(QPen(accent, 1));
        p.setBrush(surface);
        p.drawRoundedRect(QRectF(contentX, rect.top() + 45,
                                 contentW, 15), 5, 5);
        p.setBrush(accent);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(contentX + contentW - 30,
                                 rect.top() + 48, 26, 9), 4, 4);
        p.restore();
    }

    bool m_dark = false;
};

SettingsView::SettingsView(AppContext& ctx, QWidget* parent)
    : QWidget(parent)
    , m_ctx(ctx)
{
    buildUi();

    const auto setting = [this](const QString& key) {
        return m_ctx.settings().get(key);
    };
    m_startMinimized->setChecked(setting(QStringLiteral("general.startMinimized")).toBool());
    m_monitorInterval->setValue(
        qMax(1, setting(QStringLiteral("monitor.intervalMs")).toInt() / 1000));
    m_presenceEnabled->setChecked(setting(QStringLiteral("presence.enabled")).toBool());
    m_launchConcurrency->setValue(
        setting(QStringLiteral("launcher.concurrency")).toInt());
    m_launchDelay->setValue(
        setting(QStringLiteral("launcher.delaySeconds")).toInt());
    m_robloxPath = setting(QStringLiteral("launcher.robloxPath")).toString();
    m_multiRoblox->setChecked(m_ctx.multiRoblox().enabled());
    updateMultiRobloxStatus();
    setThemeName(theme::isDark() ? QStringLiteral("dark")
                                 : QStringLiteral("light"));

    connect(m_startMinimized, &Switch::toggled, this, [this]() { markDirty(); });
    connect(m_presenceEnabled, &Switch::toggled, this, [this]() { markDirty(); });
    // Applied at once: holding the mutex has to change before the next launch.
    connect(m_multiRoblox, &Switch::toggled, this, [this](bool on) {
        m_ctx.multiRoblox().setEnabled(on);
        markDirty();
        updateMultiRobloxStatus();
    });
    connect(&m_ctx.multiRoblox(), &ex::launcher::MultiRoblox::stateChanged, this,
            [this]() { updateMultiRobloxStatus(); });
    connect(m_monitorInterval, &Stepper::valueChanged, this,
            [this](int) { markDirty(); });
    connect(m_launchConcurrency, &Stepper::valueChanged, this,
            [this](int) { markDirty(); });
    connect(m_launchDelay, &Stepper::valueChanged, this,
            [this](int) { markDirty(); });

    connect(m_darkTheme, &QAbstractButton::clicked, this, [this](bool) {
        if (!m_darkTheme->isChecked())
            return;
        m_themeName = QStringLiteral("dark");
        m_savedHint->setText(tr("Тема применена"));
        emit themeChangeRequested(m_themeName);
    });
    connect(m_lightTheme, &QAbstractButton::clicked, this, [this](bool) {
        if (!m_lightTheme->isChecked())
            return;
        m_themeName = QStringLiteral("light");
        m_savedHint->setText(tr("Тема применена"));
        emit themeChangeRequested(m_themeName);
    });

    rebuildRobloxClients();
}

void SettingsView::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(26, 22, 26, 24);
    root->setSpacing(18);

    auto* header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(16);
    auto* copy = new QVBoxLayout;
    copy->setContentsMargins(0, 0, 0, 0);
    copy->setSpacing(5);
    copy->addWidget(controls::label(tr("Настройки"),
                                    QStringLiteral("pageTitle")));
    copy->addWidget(controls::label(tr("Внешний вид, мониторинг и запуск"),
                                    QStringLiteral("pageSub")));
    header->addLayout(copy, 1);

    auto* saveBtn = controls::button(tr("Сохранить"),
                                     QStringLiteral("filled"), this);
    saveBtn->setFixedHeight(40);
    saveBtn->setMinimumWidth(122);
    icons::setGlyph(saveBtn, QStringLiteral("save"),
                    QStringLiteral("onAccent"));
    connect(saveBtn, &QPushButton::clicked, this, &SettingsView::save);
    header->addWidget(saveBtn, 0, Qt::AlignVCenter);
    root->addLayout(header);

    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(22);

    auto* index = new QWidget(this);
    index->setFixedWidth(172);
    auto* indexLayout = new QVBoxLayout(index);
    indexLayout->setContentsMargins(0, 0, 0, 0);
    indexLayout->setSpacing(2);
    auto* group = new QButtonGroup(index);
    group->setExclusive(true);

    m_scroll = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setMaximumWidth(780);
    auto* content = new QWidget(m_scroll);
    content->setObjectName(QStringLiteral("settingsStack"));
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 6, 14);
    contentLayout->setSpacing(14);

    // ---------------- Appearance
    auto* appearance = sectionCard(
        tr("Внешний вид"), tr("Тема приложения и поведение при старте."),
        content);
    auto* appearanceLayout = static_cast<QVBoxLayout*>(appearance->layout());

    auto* themes = new QWidget(appearance);
    auto* themesLayout = new QHBoxLayout(themes);
    themesLayout->setContentsMargins(0, 2, 0, 4);
    themesLayout->setSpacing(10);
    m_darkTheme = new ThemeOption(true, tr("Тёмная"), themes);
    m_lightTheme = new ThemeOption(false, tr("Светлая"), themes);
    themesLayout->addWidget(m_darkTheme, 1);
    themesLayout->addWidget(m_lightTheme, 1);
    appearanceLayout->addWidget(themes);
    addDivider(appearanceLayout);
    m_startMinimized = new Switch(appearance);
    appearanceLayout->addWidget(settingRow(
        tr("Запускать свёрнутым в трей"),
        tr("Окно не появится при старте — приложение будет ждать в трее."),
        m_startMinimized, appearance));
    contentLayout->addWidget(appearance);

    // ---------------- Monitoring
    auto* monitoring = sectionCard(
        tr("Мониторинг"), tr("Данные о клиентах и присутствии в Roblox."),
        content);
    auto* monitoringLayout = static_cast<QVBoxLayout*>(monitoring->layout());
    m_monitorInterval = new Stepper(1, 60, 5, tr("сек"), monitoring);
    monitoringLayout->addWidget(settingRow(
        tr("Интервал опроса клиентов"),
        tr("Как часто обновляются CPU и RAM запущенных инстансов."),
        m_monitorInterval, monitoring));
    addDivider(monitoringLayout);
    m_presenceEnabled = new Switch(monitoring);
    monitoringLayout->addWidget(settingRow(
        tr("Presence"),
        tr("Показывать, кто онлайн, в игре или в Studio."),
        m_presenceEnabled, monitoring));
    monitoringLayout->addSpacing(6);
    monitoringLayout->addWidget(noteBox(
        QStringLiteral("info"),
        tr("Presence не влияет на запуск аккаунтов — только на отображаемый статус."),
        monitoring));
    monitoringLayout->addSpacing(4);
    contentLayout->addWidget(monitoring);

    // ---------------- Launch
    auto* launch = sectionCard(
        tr("Запуск"), tr("Параметры пакетного запуска клиентов."), content);
    auto* launchLayout = static_cast<QVBoxLayout*>(launch->layout());
    m_launchConcurrency = new Stepper(1, 20, 2, QString(), launch);
    launchLayout->addWidget(settingRow(
        tr("Одновременных запусков"),
        tr("Сколько клиентов открывать параллельно."),
        m_launchConcurrency, launch));
    addDivider(launchLayout);
    m_launchDelay = new Stepper(0, 60, 3, tr("сек"), launch);
    launchLayout->addWidget(settingRow(
        tr("Пауза между запусками"),
        tr("Помогает Roblox спокойнее принимать очередь."),
        m_launchDelay, launch));
    launchLayout->addSpacing(6);
    launchLayout->addWidget(noteBox(
        QStringLiteral("shield"),
        tr("Слишком частые запуски Roblox может ограничить. "
           "Пауза 2–5 секунд работает стабильнее всего."),
        launch));
    addDivider(launchLayout);

    // Multi Roblox (RAM's EnableMultiRbx): applied immediately, not on Save.
    m_multiRoblox = new Switch(launch);
    launchLayout->addWidget(settingRow(
        tr("Multi Roblox"),
        tr("Держать ROBLOX_singletonMutex за себя, чтобы несколько клиентов "
           "работали одновременно."),
        m_multiRoblox, launch));
    m_multiRobloxStatus = controls::label(QString(), QStringLiteral("pageSub"));
    m_multiRobloxStatus->setWordWrap(true);
    launchLayout->addWidget(m_multiRobloxStatus);
    launchLayout->addSpacing(6);
    launchLayout->addWidget(noteBox(
        QStringLiteral("alert"),
        tr("Способ заметен анти-читу Roblox (так же делают RAM и RM), поэтому "
           "он выключен по умолчанию. Включается сразу, кнопка «Сохранить» "
           "нужна только чтобы запомнить выбор после перезапуска."),
        launch));
    launchLayout->addSpacing(4);
    contentLayout->addWidget(launch);

    // ---------------- Roblox client
    auto* roblox = sectionCard(
        tr("Roblox"),
        tr("Какой клиент запускать и что зарегистрировано в Windows."),
        content);
    auto* robloxLayout = static_cast<QVBoxLayout*>(roblox->layout());

    robloxLayout->addWidget(controls::label(tr("Установка Roblox"),
                                            QStringLiteral("setLabel")));
    auto* robloxDesc = controls::label(
        tr("Выбранный клиент запускается напрямую. «Как в Windows» — через "
           "зарегистрированный обработчик roblox-player."),
        QStringLiteral("setDesc"));
    robloxDesc->setWordWrap(true);
    robloxLayout->addWidget(robloxDesc);
    robloxLayout->addSpacing(6);

    auto* robloxRow = new QHBoxLayout;
    robloxRow->setSpacing(8);
    m_robloxClient = new ThemedComboBox(roblox);
    m_robloxClient->setMinimumWidth(260);
    m_robloxClient->setMinimumHeight(36);
    m_robloxClient->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_robloxClient->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_robloxClient->setMinimumContentsLength(18);
    m_robloxClient->setToolTip(
        tr("Обнаруженные версии Roblox и ручной путь"));
    auto* browse = controls::button(tr("Обзор…"), QStringLiteral("soft"), roblox);
    browse->setFixedHeight(36);
    browse->setToolTip(tr("Выбрать папку RobloxPlayerBeta.exe или Versions"));
    auto* refresh = controls::iconButton(
        QStringLiteral("refresh"), tr("Обновить список установок"), roblox);
    refresh->setFixedSize(36, 36);
    robloxRow->addWidget(m_robloxClient, 1);
    robloxRow->addWidget(browse, 0);
    robloxRow->addWidget(refresh, 0);
    robloxLayout->addLayout(robloxRow);

    m_robloxStatus = controls::label(QString(), QStringLiteral("pageSub"));
    m_robloxStatus->setWordWrap(true);
    m_robloxStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    robloxLayout->addWidget(m_robloxStatus);
    robloxLayout->addSpacing(6);
    robloxLayout->addWidget(noteBox(
        QStringLiteral("info"),
        tr("Выбор действует только для запусков из менеджера — ассоциация "
           "roblox-player в Windows не меняется."),
        roblox));
    robloxLayout->addSpacing(4);
    contentLayout->addWidget(roblox);

    connect(m_robloxClient, &QComboBox::currentIndexChanged, this,
            [this](int) { onRobloxClientChanged(); });
    connect(browse, &QPushButton::clicked, this,
            &SettingsView::browseRobloxFolder);
    connect(refresh, &QPushButton::clicked, this,
            &SettingsView::rebuildRobloxClients);

    m_savedHint = controls::label(
        tr("Изменения сохраняются кнопкой «Сохранить»."),
        QStringLiteral("pageSub"));
    contentLayout->addWidget(m_savedHint);
    contentLayout->addStretch(1);
    m_scroll->setWidget(content);

    const auto addIndex = [this, indexLayout, group](const QString& text,
                                                      QWidget* target) {
        auto* button = new QPushButton(text, this);
        button->setProperty("class", QStringLiteral("indexBtn"));
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(32);
        group->addButton(button);
        indexLayout->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, target]() {
            m_scroll->ensureWidgetVisible(target, 0, 12);
        });
        return button;
    };
    auto* appearanceIndex = addIndex(tr("Внешний вид"), appearance);
    addIndex(tr("Мониторинг"), monitoring);
    addIndex(tr("Запуск"), launch);
    addIndex(tr("Roblox"), roblox);
    appearanceIndex->setChecked(true);
    indexLayout->addStretch(1);

    m_scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    body->addWidget(index, 0, Qt::AlignTop);
    body->addWidget(m_scroll, 1);
    root->addLayout(body, 1);
}

void SettingsView::setThemeName(const QString& name)
{
    m_themeName = name == QLatin1String("light") ? QStringLiteral("light")
                                                 : QStringLiteral("dark");
    if (!m_darkTheme || !m_lightTheme)
        return;
    const QSignalBlocker blockDark(m_darkTheme);
    const QSignalBlocker blockLight(m_lightTheme);
    m_darkTheme->setChecked(m_themeName == QLatin1String("dark"));
    m_lightTheme->setChecked(m_themeName == QLatin1String("light"));
    m_darkTheme->update();
    m_lightTheme->update();
}

void SettingsView::markDirty()
{
    if (m_savedHint)
        m_savedHint->setText(tr("Есть несохранённые изменения."));
}

void SettingsView::rebuildRobloxClients()
{
    if (!m_robloxClient)
        return;

    QString selected = m_robloxPath.trimmed();
    const QString resolved = ex::platform::resolveRobloxPlayerPath(selected);
    if (!resolved.isEmpty())
        selected = resolved;

    const QString registered = ex::platform::registeredRobloxPlayer();
    const QList<ex::platform::RobloxInstallation> installs =
        ex::platform::robloxInstallations();
    const auto pathKey = [](const QString& path) {
        return path.isEmpty() ? QString()
                              : QFileInfo(path).absoluteFilePath().toLower();
    };
    const QString registeredKey = pathKey(registered);
    const QString registeredDir =
        registered.isEmpty() ? QString()
                             : QFileInfo(registered).absolutePath().toLower();

    const QSignalBlocker blocker(m_robloxClient);
    m_robloxClient->clear();
    m_robloxClient->addItem(tr("Как в Windows (обработчик roblox-player)"),
                            QString());

    QSet<QString> known;
    for (const auto& install : installs) {
        known.insert(pathKey(install.exePath));
        QString label = install.folderName;
        if (install.modified.isValid()) {
            label += QStringLiteral("  ·  %1").arg(
                install.modified.toString(QStringLiteral("dd.MM.yyyy HH:mm")));
        }
        if (!registeredKey.isEmpty()
            && (pathKey(install.exePath) == registeredKey
                || (!registeredDir.isEmpty()
                    && QFileInfo(install.exePath).absolutePath().toLower()
                           == registeredDir))) {
            label += tr("  · зарегистрирован");
        }
        m_robloxClient->addItem(label, install.exePath);
        m_robloxClient->setItemData(
            m_robloxClient->count() - 1,
            QDir::toNativeSeparators(install.exePath), Qt::ToolTipRole);
    }

    if (!selected.isEmpty() && !known.contains(pathKey(selected))) {
        const bool valid =
            !ex::platform::resolveRobloxPlayerPath(selected).isEmpty();
        const QString label =
            valid ? tr("Выбранный путь — %1")
                        .arg(QDir::toNativeSeparators(selected))
                  : tr("Недоступный путь — %1")
                        .arg(QDir::toNativeSeparators(selected));
        m_robloxClient->addItem(label, selected);
        m_robloxClient->setItemData(
            m_robloxClient->count() - 1,
            QDir::toNativeSeparators(selected), Qt::ToolTipRole);
    }

    int index = 0;
    for (int i = 0; i < m_robloxClient->count(); ++i) {
        if (pathKey(m_robloxClient->itemData(i).toString())
            == pathKey(selected)) {
            index = i;
            break;
        }
    }
    m_robloxClient->setCurrentIndex(index);
    m_robloxPath = m_robloxClient->itemData(index).toString();
    updateRobloxStatus();
}

void SettingsView::updateRobloxStatus()
{
    if (!m_robloxStatus)
        return;

    const QString registered = ex::platform::registeredRobloxPlayer();
    const QString selected = m_robloxPath.trimmed();
    const QString resolved = ex::platform::resolveRobloxPlayerPath(selected);

    QStringList lines;
    bool warning = false;
    if (registered.isEmpty()) {
        lines << tr("Обработчик roblox-player в Windows не зарегистрирован.");
        if (selected.isEmpty())
            warning = true;
    } else {
        lines << tr("Сейчас Windows открывает: %1")
                     .arg(QDir::toNativeSeparators(registered));
    }

    if (!selected.isEmpty() && resolved.isEmpty()) {
        lines << tr("Выбранный клиент не найден — будет использован "
                    "обработчик Windows.");
        warning = true;
    } else if (!resolved.isEmpty()) {
        lines << tr("ExAccountManager запустит: %1")
                     .arg(QDir::toNativeSeparators(resolved));
    } else {
        lines << tr("ExAccountManager использует обработчик Windows.");
    }

    m_robloxStatus->setText(lines.join(QLatin1Char('\n')));
    m_robloxStatus->setToolTip(QDir::toNativeSeparators(
        resolved.isEmpty() ? registered : resolved));
    m_robloxStatus->setProperty(
        "class", warning ? QStringLiteral("warningText")
                         : QStringLiteral("pageSub"));
    controls::repolish(m_robloxStatus);
}

void SettingsView::updateMultiRobloxStatus()
{
    if (!m_multiRobloxStatus)
        return;

    QString text;
    bool warning = false;
    switch (m_ctx.multiRoblox().state()) {
    case ex::launcher::MultiRoblox::State::Off:
        text = tr("Выключено: клиенты делят один мьютекс Roblox между собой.");
        break;
    case ex::launcher::MultiRoblox::State::Holding:
        text = tr("Мьютекс ROBLOX_singletonMutex у менеджера — можно держать "
                  "несколько клиентов одновременно.");
        break;
    case ex::launcher::MultiRoblox::State::Waiting:
        text = tr("Мьютекс занят — его держит запущенный Roblox или другой "
                  "менеджер (например, RAM). Заберём, как только владелец "
                  "освободит.");
        warning = true;
        break;
    case ex::launcher::MultiRoblox::State::Error:
        text = tr("Не удалось захватить мьютекс — мультиинстанс недоступен.");
        warning = true;
        break;
    }

    m_multiRobloxStatus->setText(text);
    m_multiRobloxStatus->setProperty(
        "class", warning ? QStringLiteral("warningText")
                         : QStringLiteral("pageSub"));
    controls::repolish(m_multiRobloxStatus);
}

void SettingsView::browseRobloxFolder()
{
    QString startDir;
    const QString resolved = ex::platform::resolveRobloxPlayerPath(m_robloxPath);
    if (!resolved.isEmpty()) {
        startDir = QFileInfo(resolved).absolutePath();
    } else if (const QString registered =
                   ex::platform::registeredRobloxPlayer();
               !registered.isEmpty()) {
        startDir = QFileInfo(registered).absolutePath();
    } else {
        const auto installs = ex::platform::robloxInstallations();
        if (!installs.isEmpty())
            startDir = QFileInfo(installs.first().exePath).absolutePath();
    }

    const QString folder = QFileDialog::getExistingDirectory(
        this, tr("Папка RobloxPlayerBeta.exe или Versions"), startDir,
        QFileDialog::ShowDirsOnly);
    if (folder.isEmpty())
        return;

    const QString exe = ex::platform::findRobloxPlayerInFolder(folder);
    if (exe.isEmpty()) {
        if (m_robloxStatus) {
            m_robloxStatus->setText(
                tr("В этой папке нет RobloxPlayerBeta.exe. Выберите папку "
                   "установки или папку Versions."));
            m_robloxStatus->setProperty("class", QStringLiteral("warningText"));
            controls::repolish(m_robloxStatus);
        }
        return;
    }

    m_robloxPath = exe;
    rebuildRobloxClients();
    markDirty();
}

void SettingsView::onRobloxClientChanged()
{
    if (!m_robloxClient)
        return;
    m_robloxPath = m_robloxClient->currentData().toString();
    updateRobloxStatus();
    markDirty();
}

void SettingsView::save()
{
    auto& settings = m_ctx.settings();
    settings.set(QStringLiteral("general.theme"), m_themeName);
    settings.set(QStringLiteral("general.startMinimized"),
                 m_startMinimized->isChecked());
    settings.set(QStringLiteral("monitor.intervalMs"),
                 m_monitorInterval->value() * 1000);
    settings.set(QStringLiteral("presence.enabled"),
                 m_presenceEnabled->isChecked());
    settings.set(QStringLiteral("launcher.concurrency"),
                 m_launchConcurrency->value());
    settings.set(QStringLiteral("launcher.delaySeconds"),
                 m_launchDelay->value());
    settings.set(QStringLiteral("multiinstance.enabled"),
                 m_multiRoblox->isChecked());
    settings.set(QStringLiteral("launcher.robloxPath"), m_robloxPath);

    m_ctx.monitor().setIntervalMs(m_monitorInterval->value() * 1000);
    m_savedHint->setText(tr("Сохранено только что."));
}

} // namespace ex::ui

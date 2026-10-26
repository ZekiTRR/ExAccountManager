#include "ui/instances/client_options_dialog.h"

#include "platform/windows/ram_limits.h"
#include "platform/windows/window_mgr.h"
#include "ui/material/controls.h"
#include "ui/material/icons.h"

#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>

namespace ex::ui {
namespace {

QWidget* captionBlock(const QString& title, const QString& subtitle,
                      QWidget* parent)
{
    auto* block = new QWidget(parent);
    auto* layout = new QVBoxLayout(block);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(controls::label(title, QStringLiteral("setLabel")));
    auto* sub = controls::label(subtitle, QStringLiteral("setDesc"));
    sub->setWordWrap(true);
    layout->addWidget(sub);
    return block;
}

QFrame* section(const QString& title, const QString& subtitle, QWidget* parent)
{
    auto* card = controls::card(parent);
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(8);
    layout->addWidget(controls::label(title, QStringLiteral("sectionTitle")));
    auto* sub = controls::label(subtitle, QStringLiteral("sectionSub"));
    sub->setWordWrap(true);
    layout->addWidget(sub);
    return card;
}

} // namespace

ClientOptionsDialog::ClientOptionsDialog(const QList<qint64>& selectedPids,
                                         const QList<qint64>& allPids,
                                         bool preferAll, int windowWidth,
                                         int windowHeight, int cpuPercent,
                                         qint64 ramMb, QWidget* parent)
    : QDialog(parent)
    , m_selected(selectedPids)
    , m_all(allPids)
{
    setWindowTitle(tr("Параметры клиентов"));
    setModal(true);
    resize(540, 600);
    setMinimumSize(500, 560);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 16);
    root->setSpacing(12);

    auto* header = new QHBoxLayout;
    header->setSpacing(11);
    auto* iconTile = new QFrame(this);
    iconTile->setProperty("class", QStringLiteral("iconTile"));
    iconTile->setFixedSize(34, 34);
    auto* iconLayout = new QVBoxLayout(iconTile);
    iconLayout->setContentsMargins(0, 0, 0, 0);
    auto* icon = new icons::IconLabel(QStringLiteral("settings"), 17, iconTile);
    icon->setTone(QStringLiteral("accent"));
    iconLayout->addWidget(icon);
    header->addWidget(iconTile, 0, Qt::AlignTop);
    auto* copy = new QVBoxLayout;
    copy->setContentsMargins(0, 0, 0, 0);
    copy->setSpacing(2);
    copy->addWidget(controls::label(tr("Параметры клиентов"),
                                    QStringLiteral("titleMedium")));
    copy->addWidget(controls::label(
        tr("Размер окна Roblox и лимиты ресурсов для выбранных или всех окон."),
        QStringLiteral("pageSub")));
    header->addLayout(copy, 1);
    root->addLayout(header);

    // ---- область применения
    auto* scopeRow = new QHBoxLayout;
    scopeRow->setContentsMargins(0, 0, 0, 0);
    scopeRow->setSpacing(10);
    scopeRow->addWidget(controls::label(tr("Применить к"),
                                        QStringLiteral("fieldLabel")));
    scopeRow->addStretch(1);

    auto* scope = new QFrame(this);
    scope->setProperty("class", QStringLiteral("miniSeg"));
    auto* scopeLayout = new QHBoxLayout(scope);
    scopeLayout->setContentsMargins(3, 3, 3, 3);
    scopeLayout->setSpacing(0);
    auto* group = new QButtonGroup(scope);
    group->setExclusive(true);
    m_scopeSelected = new QPushButton(
        tr("Выбранные (%1)").arg(m_selected.size()), scope);
    m_scopeSelected->setProperty("class", QStringLiteral("miniSegItem"));
    m_scopeSelected->setCheckable(true);
    m_scopeSelected->setCursor(Qt::PointingHandCursor);
    m_scopeSelected->setFocusPolicy(Qt::NoFocus);
    m_scopeSelected->setFixedHeight(26);
    m_scopeAll = new QPushButton(
        tr("Все окна (%1)").arg(m_all.size()), scope);
    m_scopeAll->setProperty("class", QStringLiteral("miniSegItem"));
    m_scopeAll->setCheckable(true);
    m_scopeAll->setCursor(Qt::PointingHandCursor);
    m_scopeAll->setFocusPolicy(Qt::NoFocus);
    m_scopeAll->setFixedHeight(26);
    group->addButton(m_scopeSelected);
    group->addButton(m_scopeAll);
    scopeLayout->addWidget(m_scopeSelected);
    scopeLayout->addWidget(m_scopeAll);
    scopeRow->addWidget(scope);
    root->addLayout(scopeRow);

    // ---- окно
    auto* windowCard = section(
        tr("Окно Roblox"),
        tr("Размер применяется с сохранением текущей позиции окна; сетка "
           "расставляет окна с этим размером вдоль рабочего стола."),
        this);
    auto* windowLayout = static_cast<QVBoxLayout*>(windowCard->layout());

    auto* sizeRow = new QHBoxLayout;
    sizeRow->setContentsMargins(0, 4, 0, 0);
    sizeRow->setSpacing(8);
    auto* widthCol = new QVBoxLayout;
    widthCol->setSpacing(4);
    widthCol->addWidget(controls::label(tr("Ширина, px"),
                                        QStringLiteral("fieldLabel")));
    m_width = new controls::NumberSpinBox(windowCard);
    m_width->setRange(100, 7680);
    m_width->setSingleStep(10);
    m_width->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_width->setAlignment(Qt::AlignRight);
    m_width->setFixedWidth(98);
    widthCol->addWidget(m_width);
    auto* heightCol = new QVBoxLayout;
    heightCol->setSpacing(4);
    heightCol->addWidget(controls::label(tr("Высота, px"),
                                         QStringLiteral("fieldLabel")));
    m_height = new controls::NumberSpinBox(windowCard);
    m_height->setRange(100, 4320);
    m_height->setSingleStep(10);
    m_height->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_height->setAlignment(Qt::AlignRight);
    m_height->setFixedWidth(98);
    heightCol->addWidget(m_height);
    sizeRow->addLayout(widthCol);
    sizeRow->addWidget(controls::label(QStringLiteral("×"),
                                       QStringLiteral("pageSub")),
                       0, Qt::AlignBottom);
    sizeRow->addLayout(heightCol);
    sizeRow->addStretch(1);
    windowLayout->addLayout(sizeRow);

    auto* windowActions = new QHBoxLayout;
    windowActions->setSpacing(8);
    auto* applySize = controls::button(tr("Применить размер"),
                                       QStringLiteral("soft"), windowCard);
    applySize->setFixedHeight(32);
    auto* arrange = controls::button(tr("Разложить сеткой"),
                                     QStringLiteral("soft"), windowCard);
    arrange->setFixedHeight(32);
    icons::setGlyph(arrange, QStringLiteral("grid"));
    m_actionButtons.append(applySize);
    m_actionButtons.append(arrange);
    windowActions->addWidget(applySize);
    windowActions->addWidget(arrange);
    windowActions->addStretch(1);
    windowLayout->addLayout(windowActions);
    windowLayout->addWidget(controls::label(
        tr("Для окна 100×100 сетка уложит клиентов компактно вдоль экрана."),
        QStringLiteral("fieldHint")));
    root->addWidget(windowCard);

    // ---- лимиты
    auto* limitsCard = section(
        tr("Ресурсы"),
        tr("ОЗУ — жёсткий лимит рабочего набора, CPU — Job Object "
           "(при недоступности мягкий режим). 0 — без ограничения."),
        this);
    auto* limitsLayout = static_cast<QVBoxLayout*>(limitsCard->layout());

    auto* cpuRow = new QHBoxLayout;
    cpuRow->setSpacing(12);
    cpuRow->addWidget(captionBlock(
        tr("CPU"), tr("Предел загрузки процессора для клиента, %."),
        limitsCard), 1);
    m_cpu = new controls::NumberSpinBox(limitsCard);
    m_cpu->setRange(0, 100);
    m_cpu->setSingleStep(5);
    m_cpu->setSpecialValueText(tr("без лимита"));
    m_cpu->setSuffix(QStringLiteral(" %"));
    m_cpu->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_cpu->setAlignment(Qt::AlignRight);
    m_cpu->setFixedWidth(118);
    cpuRow->addWidget(m_cpu, 0, Qt::AlignVCenter);
    limitsLayout->addLayout(cpuRow);

    auto* ramRow = new QHBoxLayout;
    ramRow->setSpacing(12);
    ramRow->addWidget(captionBlock(
        tr("ОЗУ"), tr("Максимальный рабочий набор (RAM), МБ."),
        limitsCard), 1);
    m_ram = new controls::NumberSpinBox(limitsCard);
    m_ram->setRange(0, 32768);
    m_ram->setSingleStep(256);
    m_ram->setSpecialValueText(tr("без лимита"));
    m_ram->setSuffix(QStringLiteral(" МБ"));
    m_ram->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_ram->setAlignment(Qt::AlignRight);
    m_ram->setFixedWidth(132);
    ramRow->addWidget(m_ram, 0, Qt::AlignVCenter);
    limitsLayout->addLayout(ramRow);

    auto* warning = controls::label(
        tr("Слишком низкий лимит ОЗУ вызовет подкачку и фризы. Если клиент "
           "уже в чужом Job Object, CPU ограничивается мягко — приоритетом "
           "и EcoQoS."),
        QStringLiteral("fieldHint"));
    warning->setWordWrap(true);
    limitsLayout->addWidget(warning);

    auto* limitActions = new QHBoxLayout;
    limitActions->setSpacing(8);
    auto* applyLimitsBtn = controls::button(tr("Применить лимиты"),
                                            QStringLiteral("soft"), limitsCard);
    applyLimitsBtn->setFixedHeight(32);
    auto* clearLimitsBtn = controls::button(tr("Снять лимиты"),
                                            QStringLiteral("text"), limitsCard);
    clearLimitsBtn->setFixedHeight(32);
    m_actionButtons.append(applyLimitsBtn);
    m_actionButtons.append(clearLimitsBtn);
    limitActions->addWidget(applyLimitsBtn);
    limitActions->addWidget(clearLimitsBtn);
    limitActions->addStretch(1);
    limitsLayout->addLayout(limitActions);
    root->addWidget(limitsCard);

    m_status = controls::label(QString(), QStringLiteral("pageSub"));
    m_status->setWordWrap(true);
    m_status->setMinimumHeight(34);
    root->addWidget(m_status);
    root->addStretch(1);

    auto* footer = new QHBoxLayout;
    footer->addStretch(1);
    auto* close = controls::button(tr("Закрыть"),
                                   QStringLiteral("filled"), this);
    close->setFixedHeight(36);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    footer->addWidget(close);
    root->addLayout(footer);

    m_width->setValue(qBound(100, windowWidth, 7680));
    m_height->setValue(qBound(100, windowHeight, 4320));
    m_cpu->setValue(qBound(0, cpuPercent, 100));
    m_ram->setValue(static_cast<int>(qBound<qint64>(qint64(0), ramMb, qint64(32768))));

    const bool useSelected = !preferAll && !m_selected.isEmpty();
    m_scopeSelected->setChecked(useSelected);
    m_scopeAll->setChecked(!useSelected);
    m_width->setFocus();
    connect(m_scopeSelected, &QPushButton::clicked, this,
            [this]() { updateActionState(); });
    connect(m_scopeAll, &QPushButton::clicked, this,
            [this]() { updateActionState(); });
    connect(applySize, &QPushButton::clicked, this,
            [this]() { applyWindowSize(); });
    connect(arrange, &QPushButton::clicked, this,
            [this]() { arrangeGrid(); });
    connect(applyLimitsBtn, &QPushButton::clicked, this,
            [this]() { applyLimits(); });
    connect(clearLimitsBtn, &QPushButton::clicked, this,
            [this]() { clearLimits(); });

    updateActionState();
}

QSize ClientOptionsDialog::windowSize() const
{
    return {m_width->value(), m_height->value()};
}

int ClientOptionsDialog::cpuPercent() const
{
    return m_cpu->value();
}

qint64 ClientOptionsDialog::ramMb() const
{
    return m_ram->value();
}

QList<qint64> ClientOptionsDialog::targets() const
{
    return m_scopeAll->isChecked() ? m_all : m_selected;
}

void ClientOptionsDialog::updateActionState()
{
    const bool available = !targets().isEmpty();
    for (QPushButton* button : m_actionButtons)
        button->setEnabled(available);
    if (!available)
        setStatus(tr("Нет выбранных окон. Переключитесь на «Все окна»."),
                  QStringLiteral("error"));
}

void ClientOptionsDialog::setStatus(const QString& text, const QString& tone)
{
    if (!m_status)
        return;
    m_status->setText(text);
    m_status->setProperty(
        "class",
        tone == QLatin1String("error") ? QStringLiteral("errorText")
        : tone == QLatin1String("success") ? QStringLiteral("successText")
        : tone == QLatin1String("warning") ? QStringLiteral("warningText")
                                           : QStringLiteral("pageSub"));
    controls::repolish(m_status);
}

void ClientOptionsDialog::applyWindowSize()
{
    const auto pids = targets();
    if (pids.isEmpty())
        return;
    int ok = 0;
    for (qint64 pid : pids) {
        if (ex::platform::WindowMgr::resizeWindow(pid, m_width->value(),
                                                  m_height->value())) {
            ++ok;
        }
    }
    if (ok == 0) {
        setStatus(tr("Не удалось изменить размер окон."),
                  QStringLiteral("error"));
    } else {
        setStatus(tr("Размер %1×%2 применён: %3 из %4")
                      .arg(m_width->value())
                      .arg(m_height->value())
                      .arg(ok)
                      .arg(pids.size()),
                  ok == pids.size() ? QStringLiteral("success")
                                    : QStringLiteral("warning"));
    }
}

void ClientOptionsDialog::arrangeGrid()
{
    const auto pids = targets();
    if (pids.isEmpty())
        return;
    if (ex::platform::WindowMgr::arrangeGridSized(
            pids, m_width->value(), m_height->value())) {
        setStatus(tr("Сетка %1×%2 применена к %3 окнам")
                      .arg(m_width->value())
                      .arg(m_height->value())
                      .arg(pids.size()),
                  QStringLiteral("success"));
    } else {
        setStatus(tr("Не удалось разложить окна."),
                  QStringLiteral("error"));
    }
}

void ClientOptionsDialog::applyLimits()
{
    const auto pids = targets();
    if (pids.isEmpty())
        return;

    const int cpu = m_cpu->value();
    const qint64 ram = m_ram->value();
    if (cpu == 0 && ram == 0) {
        clearLimits();
        return;
    }

    int ramOk = 0;
    int cpuHard = 0;
    int cpuSoft = 0;
    for (qint64 pid : pids) {
        const auto report = ex::platform::RamLimiter::applyLimits(pid, cpu, ram);
        if (ram > 0 && report.memory)
            ++ramOk;
        if (report.cpuHard)
            ++cpuHard;
        if (report.cpuSoft)
            ++cpuSoft;
    }

    const int total = static_cast<int>(pids.size());
    const int cpuAny = cpuHard + cpuSoft;
    QStringList parts;
    if (ram > 0)
        parts << tr("ОЗУ: %1 из %2").arg(ramOk).arg(total);
    if (cpu > 0) {
        if (cpuHard == total)
            parts << tr("CPU: %1 из %2").arg(cpuHard).arg(total);
        else if (cpuAny > 0)
            parts << tr("CPU: %1 из %2, мягкий режим").arg(cpuAny).arg(total);
        else
            parts << tr("CPU: не применён");
    }

    const bool any = (ram > 0 && ramOk > 0) || cpuHard > 0 || cpuSoft > 0;
    if (!any) {
        setStatus(tr("Не удалось применить лимиты. Проверьте, что клиенты "
                     "запущены, и повторите."),
                  QStringLiteral("error"));
    } else {
        const bool full = (ram == 0 || ramOk == total)
            && (cpu == 0 || cpuAny == total);
        setStatus(tr("Применено · %1").arg(parts.join(QStringLiteral(" · "))),
                  full && cpuSoft == 0 ? QStringLiteral("success")
                                       : QStringLiteral("warning"));
    }
}

void ClientOptionsDialog::clearLimits()
{
    const auto pids = targets();
    if (pids.isEmpty())
        return;
    int ok = 0;
    for (qint64 pid : pids) {
        if (ex::platform::RamLimiter::clearLimits(pid))
            ++ok;
    }
    setStatus(tr("Лимиты сняты: %1 из %2").arg(ok).arg(pids.size()),
              ok == pids.size() ? QStringLiteral("success")
                                : QStringLiteral("warning"));
}

} // namespace ex::ui

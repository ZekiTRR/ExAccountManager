#include "ui/analytics_view.h"

#include "core/models/account.h"
#include "ui/accounts/avatar_cache.h"
#include "ui/app_context.h"
#include "ui/material/controls.h"
#include "ui/material/icons.h"
#include "ui/material/segmented.h"
#include "ui/material/stat_card.h"
#include "ui/theme.h"

#include <QDate>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace ex::ui {
namespace {

struct TimeEntry {
    ex::model::Account account;
    qint64 seconds = 0;
    int launches = 0;
};

QString launchWord(int count)
{
    const int mod10 = count % 10;
    const int mod100 = count % 100;
    if (mod10 == 1 && mod100 != 11)
        return QObject::tr("запуск");
    if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14))
        return QObject::tr("запуска");
    return QObject::tr("запусков");
}

QString monthGenitive(int month)
{
    static const char* names[] = {
        "января", "февраля", "марта", "апреля", "мая", "июня",
        "июля", "августа", "сентября", "октября", "ноября", "декабря"
    };
    return QString::fromUtf8(names[qBound(1, month, 12) - 1]);
}

QString shortDate(const QDate& date)
{
    return QStringLiteral("%1 %2").arg(date.day()).arg(monthGenitive(date.month()));
}

QString formatDuration(qint64 seconds)
{
    if (seconds <= 0)
        return QObject::tr("0 мин");
    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    if (hours >= 48) {
        const qint64 days = hours / 24;
        return QObject::tr("%1 д %2 ч").arg(days).arg(hours % 24);
    }
    if (hours > 0) {
        return QObject::tr("%1 ч %2 м")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'));
    }
    return QObject::tr("%1 мин").arg(qMax<qint64>(1, minutes));
}

QString rangeLabel(int period)
{
    const QDate today = QDate::currentDate();
    if (period == 0)
        return QObject::tr("сегодня, %1").arg(shortDate(today));
    const QDate from = today.addDays(period == 1 ? -6 : -29);
    if (from.month() == today.month() && from.year() == today.year()) {
        return QObject::tr("%1–%2 %3")
            .arg(from.day()).arg(today.day())
            .arg(monthGenitive(today.month()));
    }
    return QObject::tr("%1 — %2")
        .arg(shortDate(from), shortDate(today));
}

QString periodFoot(int period)
{
    if (period == 0)
        return QObject::tr("за сегодня");
    if (period == 1)
        return QObject::tr("за 7 дней");
    return QObject::tr("за 30 дней");
}

// ------------------------------------------------------------------ time rows

class TimeRowsWidget : public QWidget {
public:
    explicit TimeRowsWidget(AvatarCache& avatars, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_avatars(&avatars)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMinimumHeight(kHeaderHeight + 110);
    }

    void setEntries(const QList<TimeEntry>& entries)
    {
        m_entries = entries;
        const int height = entries.isEmpty()
            ? kHeaderHeight + 110
            : kHeaderHeight + static_cast<int>(entries.size()) * kRowHeight + 2;
        setFixedHeight(height);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const auto& c = theme::currentPalette();

        int maxSeconds = 1;
        qint64 totalSeconds = 0;
        for (const auto& entry : m_entries) {
            maxSeconds = qMax(maxSeconds, static_cast<int>(entry.seconds));
            totalSeconds += entry.seconds;
        }

        const int nameW = qBound(120, int(width() * 0.33), 200);
        const int launchesW = 44;
        const int timeW = 60;
        const int shareW = 38;
        const int gap = 10;
        const int launchesX = width() - launchesW;
        const int timeX = launchesX - gap - timeW;
        const int shareX = timeX - gap - shareW;
        const int barLeft = nameW + 8;
        const int barRight = qMax(barLeft + 30, shareX - gap);

        QFont headFont = font();
        headFont.setPixelSize(11);
        headFont.setWeight(QFont::Normal);
        p.setFont(headFont);
        p.setPen(c.outline);
        p.drawText(QRect(0, 0, nameW, kHeaderHeight),
                   Qt::AlignVCenter | Qt::AlignLeft, tr("Аккаунт"));
        p.drawText(QRect(barLeft, 0, barRight - barLeft, kHeaderHeight),
                   Qt::AlignVCenter | Qt::AlignLeft, tr("Доля времени"));
        p.drawText(QRect(timeX, 0, timeW, kHeaderHeight),
                   Qt::AlignVCenter | Qt::AlignRight, tr("Время"));
        p.drawText(QRect(launchesX, 0, launchesW, kHeaderHeight),
                   Qt::AlignVCenter | Qt::AlignRight, tr("Запуски"));
        p.setPen(QPen(c.outlineVariant, 1));
        p.drawLine(0, kHeaderHeight - 1, width(), kHeaderHeight - 1);

        if (m_entries.isEmpty()) {
            p.setFont(font());
            p.setPen(c.outline);
            p.drawText(QRect(0, kHeaderHeight, width(), 100),
                       Qt::AlignCenter, tr("Пока нет игровых сессий"));
            return;
        }

        QFont nameFont = font();
        nameFont.setPixelSize(12);
        nameFont.setWeight(QFont::Normal);
        QFont metaFont = font();
        metaFont.setPixelSize(11);
        metaFont.setWeight(QFont::Normal);
        QFont valueFont = font();
        valueFont.setPixelSize(11);
        valueFont.setWeight(QFont::Normal);

        for (int i = 0; i < m_entries.size(); ++i) {
            const auto& entry = m_entries.at(i);
            const int top = kHeaderHeight + i * kRowHeight;
            const int centerY = top + kRowHeight / 2;

            const QRect avatarRect(0, centerY - 13, 27, 27);
            const QString fallback = entry.account.username.isEmpty()
                ? entry.account.aliasOrUsername() : entry.account.username;
            const QPixmap avatar =
                m_avatars->request(QUrl(entry.account.avatarUrl), fallback);
            p.drawPixmap(avatarRect, avatar, avatar.rect());

            p.setFont(nameFont);
            p.setPen(c.onSurface);
            p.drawText(QRect(35, top, nameW - 36, kRowHeight),
                       Qt::AlignVCenter | Qt::AlignLeft,
                       p.fontMetrics().elidedText(entry.account.aliasOrUsername(),
                                                  Qt::ElideRight, nameW - 38));

            const QRectF track(barLeft, centerY - 2.5, barRight - barLeft, 5);
            p.setPen(Qt::NoPen);
            p.setBrush(c.surfaceContainerHighest);
            p.drawRoundedRect(track, 2.5, 2.5);
            if (entry.seconds > 0) {
                const qreal fraction =
                    static_cast<qreal>(entry.seconds) / maxSeconds;
                QRectF fill = track;
                fill.setWidth(qMax<qreal>(3, track.width() * fraction));
                QLinearGradient g(track.left(), 0, track.right(), 0);
                g.setColorAt(0.0, c.primary);
                g.setColorAt(1.0, c.accent);
                p.setBrush(g);
                p.drawRoundedRect(fill, 2.5, 2.5);
            }

            const double share = totalSeconds > 0
                ? 100.0 * entry.seconds / totalSeconds : 0.0;
            p.setFont(valueFont);
            p.setPen(c.outline);
            p.drawText(QRect(shareX, top, shareW, kRowHeight),
                       Qt::AlignVCenter | Qt::AlignRight,
                       share >= 1.0 ? QStringLiteral("%1%").arg(qRound(share))
                                    : (share > 0.0 ? QStringLiteral("<1%")
                                                   : QStringLiteral("0%")));

            p.setFont(metaFont);
            p.setPen(c.onSurfaceVariant);
            p.drawText(QRect(timeX, top, timeW, kRowHeight),
                       Qt::AlignVCenter | Qt::AlignRight,
                       formatDuration(entry.seconds));

            const QRect pill(launchesX + launchesW - 26, centerY - 10, 26, 20);
            p.setPen(QPen(c.outlineVariant, 1));
            p.setBrush(c.surfaceContainerHigh);
            p.drawRoundedRect(pill, 6, 6);
            p.setPen(c.onSurfaceVariant);
            p.drawText(pill, Qt::AlignCenter, QString::number(entry.launches));
        }
    }

private:
    static constexpr int kHeaderHeight = 30;
    static constexpr int kRowHeight = 43;
    AvatarCache* m_avatars = nullptr;
    QList<TimeEntry> m_entries;
};

// ------------------------------------------------------------------ chart

class LaunchChart : public QWidget {
public:
    explicit LaunchChart(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(190);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setData(const QStringList& axis, const QStringList& points,
                 const QList<int>& values)
    {
        m_axis = axis;
        m_points = points;
        m_values = values;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const auto& c = theme::currentPalette();

        const QRectF chart(0, 8, width(), height() - 32);
        QFont axisFont = font();
        axisFont.setPixelSize(11);
        axisFont.setWeight(QFont::Normal);

        const int count = m_values.size();
        if (count == 0)
            return;
        const int maxValue = *std::max_element(m_values.cbegin(), m_values.cend());
        if (maxValue > 0) {
            p.setPen(QPen(c.outlineVariant, 1));
            for (int i = 0; i < 3; ++i) {
                const qreal y = chart.top() + chart.height() * i / 2.0;
                p.drawLine(QPointF(chart.left(), y), QPointF(chart.right(), y));
            }
        }
        if (maxValue == 0) {
            p.setFont(font());
            p.setPen(c.outline);
            p.drawText(chart, Qt::AlignCenter, tr("Нет запусков за период"));
        } else {
            const qreal slot = chart.width() / count;
            const qreal barW = qMin<qreal>(26, slot * 0.48);
            for (int i = 0; i < count; ++i) {
                const int value = m_values.at(i);
                if (value <= 0)
                    continue;
                const qreal h = qMax<qreal>(5, chart.height() * value / maxValue);
                const QRectF bar(i * slot + (slot - barW) / 2,
                                 chart.bottom() - h, barW, h);
                QLinearGradient g(0, bar.top(), 0, bar.bottom());
                g.setColorAt(0.0, c.accent);
                g.setColorAt(1.0, c.primary);
                p.setPen(Qt::NoPen);
                p.setBrush(g);
                p.drawRoundedRect(bar, 6, 6);
            }
        }

        p.setFont(axisFont);
        p.setPen(c.outline);
        const qreal slot = width() / qMax(1, count);
        for (int i = 0; i < count && i < m_axis.size(); ++i) {
            const QRectF labelRect(i * slot, chart.bottom() + 6, slot, 18);
            p.drawText(labelRect, Qt::AlignHCenter | Qt::AlignTop,
                       m_axis.at(i));
        }
    }

private:
    QStringList m_axis;
    QStringList m_points;
    QList<int> m_values;
};

} // namespace

AnalyticsView::AnalyticsView(AppContext& ctx, QWidget* parent)
    : QWidget(parent)
    , m_ctx(ctx)
{
    buildUi();
    connect(&ctx, &AppContext::accountsChanged, this, &AnalyticsView::reload);
    connect(&ctx.accounts(), &ex::roblox::AccountService::presenceUpdated,
            this, &AnalyticsView::reload);
    connect(&m_avatars, &AvatarCache::avatarReady, this, [this]() {
        if (m_timeRows)
            m_timeRows->update();
    });

    auto* timer = new QTimer(this);
    timer->setInterval(30000);
    connect(timer, &QTimer::timeout, this, [this]() {
        if (isVisible())
            reload();
    });
    timer->start();

    reload();
}

void AnalyticsView::buildUi()
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
    copy->addWidget(controls::label(tr("Аналитика"),
                                    QStringLiteral("pageTitle")));
    m_subtitle = controls::label(QString(), QStringLiteral("pageSub"));
    copy->addWidget(m_subtitle);
    header->addLayout(copy, 1);

    m_periodSeg = new Segmented(this);
    m_periodSeg->addItem(tr("Сегодня"), 0);
    m_periodSeg->addItem(tr("7 дней"), 1);
    m_periodSeg->addItem(tr("30 дней"), 2);
    m_periodSeg->setFixedHeight(44);
    connect(m_periodSeg, &Segmented::currentIndexChanged, this,
            [this](int) { reload(); });
    header->addWidget(m_periodSeg, 0, Qt::AlignVCenter);
    root->addLayout(header);

    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("pageRoot"));
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(14);

    auto* stats = new QHBoxLayout;
    stats->setContentsMargins(0, 0, 0, 0);
    stats->setSpacing(12);
    m_timeCard = new StatCard(QStringLiteral("clock"), tr("Фарм-время"),
                              QString(), periodFoot(0));
    m_launchCard = new StatCard(QStringLiteral("play-filled"), tr("Запусков"),
                                QString(), periodFoot(0),
                                QStringLiteral("accent"));
    m_avgCard = new StatCard(QStringLiteral("hourglass"), tr("Средняя сессия"),
                             QString(), tr("на одну сессию"));
    m_activeCard = new StatCard(QStringLiteral("accounts"),
                                tr("Активных аккаунтов"), QString(),
                                tr("— сейчас в игре"));
    stats->addWidget(m_timeCard, 1);
    stats->addWidget(m_launchCard, 1);
    stats->addWidget(m_avgCard, 1);
    stats->addWidget(m_activeCard, 1);
    contentLayout->addLayout(stats);

    auto* grid = new QHBoxLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(14);

    auto* timePanel = controls::card(content);
    auto* timeLayout = new QVBoxLayout(timePanel);
    timeLayout->setContentsMargins(16, 15, 16, 10);
    timeLayout->setSpacing(8);
    auto* timeHead = new QHBoxLayout;
    timeHead->setContentsMargins(0, 0, 0, 0);
    timeHead->setSpacing(8);
    timeHead->addWidget(controls::label(tr("Время по аккаунтам"),
                                        QStringLiteral("titleSmall")));
    m_timePanelSub = controls::label(periodFoot(0),
                                     QStringLiteral("pageSub"));
    timeHead->addWidget(m_timePanelSub);
    timeHead->addStretch(1);
    m_leader = controls::label(QString(), QStringLiteral("chip"));
    timeHead->addWidget(m_leader);
    timeLayout->addLayout(timeHead);
    m_timeRows = new TimeRowsWidget(m_avatars, timePanel);
    timeLayout->addWidget(m_timeRows);
    grid->addWidget(timePanel, 1, Qt::AlignTop);

    auto* chartPanel = controls::card(content);
    chartPanel->setFixedWidth(306);
    auto* chartLayout = new QVBoxLayout(chartPanel);
    chartLayout->setContentsMargins(16, 15, 16, 12);
    chartLayout->setSpacing(8);
    auto* chartHead = new QHBoxLayout;
    chartHead->setContentsMargins(0, 0, 0, 0);
    chartHead->setSpacing(7);
    chartHead->addWidget(controls::label(tr("Активность"),
                                         QStringLiteral("titleSmall")));
    m_chartSub = controls::label(QString(), QStringLiteral("pageSub"));
    chartHead->addWidget(m_chartSub);
    chartHead->addStretch(1);
    chartLayout->addLayout(chartHead);
    m_chart = new LaunchChart(chartPanel);
    chartLayout->addWidget(m_chart);
    auto* chartFoot = new QHBoxLayout;
    chartFoot->setContentsMargins(0, 0, 0, 0);
    chartFoot->setSpacing(6);
    auto* trend = new icons::IconLabel(QStringLiteral("trend"), 13, chartPanel);
    trend->setTone(QStringLiteral("accent"));
    chartFoot->addWidget(trend);
    m_peak = controls::label(QString(), QStringLiteral("pageSub"));
    chartFoot->addWidget(m_peak, 1);
    chartLayout->addLayout(chartFoot);
    grid->addWidget(chartPanel, 0, Qt::AlignTop);

    contentLayout->addLayout(grid);
    contentLayout->addStretch(1);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);
}

void AnalyticsView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    reload();
}

void AnalyticsView::reload()
{
    const int period = m_periodSeg ? m_periodSeg->currentIndex() : 0;
    const QDate today = QDate::currentDate();
    const int backDays = period == 0 ? 0 : (period == 1 ? 6 : 29);
    const QDate fromDate = today.addDays(-backDays);
    const QDateTime from(fromDate, QTime(0, 0));

    const auto accounts = m_ctx.store().accounts();
    QList<TimeEntry> entries;
    entries.reserve(accounts.size());
    qint64 totalSeconds = 0;
    int totalLaunches = 0;
    int inGame = 0;

    for (const auto& account : accounts) {
        TimeEntry entry;
        entry.account = account;
        entry.seconds = m_ctx.store().sessionSecondsFor(account.id, from, {});
        entry.launches = m_ctx.store().sessionCountFor(account.id, from, {});
        totalSeconds += entry.seconds;
        totalLaunches += entry.launches;
        entries.append(entry);
        if (m_ctx.accounts().presenceFor(account.id).type
            == ex::model::PresenceType::InGame) {
            ++inGame;
        }
    }

    std::sort(entries.begin(), entries.end(),
              [](const TimeEntry& a, const TimeEntry& b) {
        if (a.seconds != b.seconds)
            return a.seconds > b.seconds;
        if (a.launches != b.launches)
            return a.launches > b.launches;
        return a.account.aliasOrUsername() < b.account.aliasOrUsername();
    });

    QList<TimeEntry> active;
    for (const auto& entry : entries) {
        if (entry.seconds > 0 || entry.launches > 0)
            active.append(entry);
    }

    m_subtitle->setText(tr("%1 · время в игре и запуски").arg(rangeLabel(period)));
    m_timePanelSub->setText(periodFoot(period));
    m_timeCard->setValue(formatDuration(totalSeconds));
    m_timeCard->setFoot(periodFoot(period));
    m_launchCard->setValue(QString::number(totalLaunches));
    m_launchCard->setFoot(periodFoot(period));
    m_avgCard->setValue(formatDuration(
        totalLaunches > 0 ? totalSeconds / totalLaunches : 0));
    m_avgCard->setFoot(tr("на одну сессию"));
    m_activeCard->setValue(QString::number(active.size()));
    m_activeCard->setFoot(tr("%1 сейчас в игре").arg(inGame));

    const bool hasTime = !active.isEmpty() && active.first().seconds > 0;
    m_leader->setVisible(hasTime);
    if (hasTime) {
        m_leader->setText(tr("Лидер · %1")
                              .arg(active.first().account.aliasOrUsername()));
    }
    static_cast<TimeRowsWidget*>(m_timeRows)->setEntries(active);

    // ---- активность по бакетам
    QStringList axis;
    QStringList points;
    QList<int> values;
    if (period == 0) {
        axis = {"00", "03", "06", "09", "12", "15", "18", "21"};
        for (int hour = 0; hour < 24; hour += 3)
            points.append(QStringLiteral("%1:00").arg(hour, 2, 10, QLatin1Char('0')));
        values = QList<int>(8, 0);
    } else if (period == 1) {
        static const char* days[] = {"Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"};
        for (int i = 0; i < 7; ++i) {
            const QDate date = fromDate.addDays(i);
            axis.append(QString::fromUtf8(days[date.dayOfWeek() - 1]));
            points.append(shortDate(date));
        }
        values = QList<int>(7, 0);
    } else {
        for (int i = 0; i < 6; ++i) {
            const QDate start = fromDate.addDays(i * 5);
            const QDate end = start.addDays(4);
            axis.append(QStringLiteral("%1–%2").arg(start.day()).arg(end.day()));
            points.append(QStringLiteral("%1–%2 %3")
                              .arg(start.day())
                              .arg(end.day())
                              .arg(monthGenitive(start.month())));
        }
        values = QList<int>(6, 0);
    }

    const auto starts = m_ctx.store().sessionStarts(from, {});
    for (const QDateTime& utc : starts) {
        const QDateTime local = utc.toLocalTime();
        const QDate date = local.date();
        if (date < fromDate || date > today)
            continue;
        if (period == 0) {
            if (date == today) {
                const int bin = local.time().hour() / 3;
                values[qBound(0, bin, 7)] += 1;
            }
        } else if (period == 1) {
            const int bin = fromDate.daysTo(date);
            if (bin >= 0 && bin < values.size())
                values[bin] += 1;
        } else {
            const int bin = fromDate.daysTo(date) / 5;
            values[qBound(0, bin, 5)] += 1;
        }
    }

    m_chartSub->setText(period == 0 ? tr("по часам")
                                    : period == 1 ? tr("по дням")
                                                  : tr("по 5 дней"));
    static_cast<LaunchChart*>(m_chart)->setData(axis, points, values);

    int maxValue = 0;
    int maxIndex = -1;
    for (int i = 0; i < values.size(); ++i) {
        if (values.at(i) > maxValue) {
            maxValue = values.at(i);
            maxIndex = i;
        }
    }
    if (maxValue <= 0 || maxIndex < 0) {
        m_peak->setText(tr("Пока нет запусков"));
    } else {
        m_peak->setText(tr("Пик: %1 — %2 %3")
                            .arg(points.at(maxIndex))
                            .arg(maxValue)
                            .arg(launchWord(maxValue)));
    }
}

} // namespace ex::ui

#pragma once

#include "ui/accounts/avatar_cache.h"

#include <QWidget>

class QLabel;
class QShowEvent;
class QWidget;

namespace ex::ui {

class AppContext;
class Segmented;
class StatCard;

// Аналитика: период (сегодня/7/30 дней), карточки метрик, разбивка по
// аккаунтам и график активности.
class AnalyticsView : public QWidget {
    Q_OBJECT
public:
    explicit AnalyticsView(AppContext& ctx, QWidget* parent = nullptr);
    void reload();

protected:
    void showEvent(QShowEvent* event) override;

private:
    void buildUi();
    AppContext& m_ctx;
    AvatarCache m_avatars;
    Segmented* m_periodSeg = nullptr;
    QLabel* m_subtitle = nullptr;
    QLabel* m_timePanelSub = nullptr;
    QLabel* m_leader = nullptr;
    QLabel* m_chartSub = nullptr;
    QLabel* m_peak = nullptr;
    StatCard* m_timeCard = nullptr;
    StatCard* m_launchCard = nullptr;
    StatCard* m_avgCard = nullptr;
    StatCard* m_activeCard = nullptr;
    QWidget* m_timeRows = nullptr;
    QWidget* m_chart = nullptr;
};

} // namespace ex::ui

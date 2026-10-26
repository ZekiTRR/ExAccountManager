#pragma once

#include "ui/accounts/avatar_cache.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QWidget>

class QLabel;
class QPushButton;
class QScrollArea;
class QShowEvent;
class QVBoxLayout;
class QWidget;

namespace ex::ui {

class AppContext;

// Инстансы: live-метрики, таблица клиентов с действиями в строке и
// массовыми действиями по выбранным/всем окнам.
class InstancesView : public QWidget {
    Q_OBJECT
public:
    explicit InstancesView(AppContext& ctx, QWidget* parent = nullptr);
    void reload();

protected:
    void showEvent(QShowEvent* event) override;

private:
    void buildUi();
    void updatePollHint();
    void handleRowAction(qint64 pid, int action);
    void handleRowSelection(qint64 pid, Qt::KeyboardModifiers modifiers);
    void performBulkAction(int action);
    void showClientOptions();
    [[nodiscard]] QList<qint64> actionPids() const;
    [[nodiscard]] QList<qint64> selectedPids() const;
    void updateBulkUi();
    void setStatus(const QString& text, const QString& tone = QString());

    AppContext& m_ctx;
    AvatarCache m_avatars;
    QLabel* m_subtitle = nullptr;
    QLabel* m_clientsValue = nullptr;
    QLabel* m_cpuValue = nullptr;
    QLabel* m_ramValue = nullptr;
    QLabel* m_tableCount = nullptr;
    QLabel* m_pollHint = nullptr;
    QLabel* m_status = nullptr;
    QWidget* m_spark = nullptr;
    QScrollArea* m_scroll = nullptr;
    QVBoxLayout* m_rowsLayout = nullptr;
    QHash<qint64, QWidget*> m_rows;
    QList<qint64> m_order;
    QSet<qint64> m_hiddenWindows;
    QList<double> m_cpuHistory;
    QSet<qint64> m_selectedPids;
    qint64 m_selectionAnchor = 0;
    bool m_scopeAll = false;
    QLabel* m_selectionInfo = nullptr;
    QPushButton* m_scopeSelected = nullptr;
    QPushButton* m_scopeAllBtn = nullptr;
    QPushButton* m_optionsButton = nullptr;
    QList<QPushButton*> m_bulkButtons;
    bool m_rowsBuilt = false;
};

} // namespace ex::ui

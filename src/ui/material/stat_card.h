#pragma once

#include <QFrame>

class QLabel;

namespace ex::ui {
namespace icons { class IconLabel; }

// Компактная метрика аналитики: подпись, значение, контекст и иконка.
class StatCard : public QFrame {
public:
    explicit StatCard(const QString& iconName, const QString& label,
                      const QString& value = QString(),
                      const QString& foot = QString(),
                      const QString& tone = QStringLiteral("muted"),
                      QWidget* parent = nullptr);

    void setValue(const QString& value);
    void setFoot(const QString& foot);
    void setLabel(const QString& label);

private:
    QLabel* m_label = nullptr;
    QLabel* m_value = nullptr;
    QLabel* m_foot = nullptr;
};

} // namespace ex::ui

#pragma once

#include <QFrame>

class QLabel;
class QPushButton;

namespace ex::ui {

// Compact "- value +" number control with a unit caption.
class Stepper : public QFrame {
    Q_OBJECT
public:
    explicit Stepper(int minimum, int maximum, int value,
                     const QString& suffix = QString(), QWidget* parent = nullptr);

    [[nodiscard]] int value() const { return m_value; }
    void setValue(int value);
    void setRange(int minimum, int maximum);
    void setSuffix(const QString& suffix);

signals:
    void valueChanged(int value);

private:
    void step(int delta);
    void sync();

    int m_min = 0;
    int m_max = 60;
    int m_value = 0;
    QPushButton* m_minus = nullptr;
    QPushButton* m_plus = nullptr;
    QLabel* m_valueLabel = nullptr;
    QLabel* m_suffix = nullptr;
};

} // namespace ex::ui

#pragma once

#include <QSpinBox>
#include <QString>
#include <QWidget>

class QFocusEvent;
class QFrame;
class QLabel;
class QLayout;
class QPushButton;
class QResizeEvent;
class QShowEvent;
class QWidget;

namespace ex::ui::controls {

// Маленькая цветная точка статуса; цвет следует tone и текущей теме.
class StatusDot : public QWidget {
public:
    explicit StatusDot(int size = 6, QWidget* parent = nullptr);
    void setTone(const QString& tone);
    [[nodiscard]] QSize sizeHint() const override { return {m_size, m_size}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int m_size = 6;
    QString m_tone;
};

// Спинбокс без стрелок. Держит внутренний QLineEdit шириной с поле:
// QStyleSheetStyle иначе сжимает его до 1 px, и значение не видно.
class NumberSpinBox : public QSpinBox {
public:
    explicit NumberSpinBox(QWidget* parent = nullptr);

protected:
    void focusInEvent(QFocusEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void syncEditWidth();
};

QLabel* label(const QString& text, const QString& styleClass = QString(),
              QWidget* parent = nullptr);
QPushButton* button(const QString& text, const QString& variant = QString(),
                    QWidget* parent = nullptr);
QPushButton* iconButton(const QString& icon, const QString& tooltip,
                        QWidget* parent = nullptr,
                        const QString& tone = QStringLiteral("auto"));
QFrame* card(QWidget* parent = nullptr);
QFrame* separator(QWidget* parent = nullptr);

// Установить динамическое свойство tone и переполировать (для status-меток).
void setTone(QWidget* widget, const QString& tone);
void repolish(QWidget* widget);

// Удалить и уничтожить все элементы layout (не сам layout).
void clearLayout(QLayout* layout);

} // namespace ex::ui::controls

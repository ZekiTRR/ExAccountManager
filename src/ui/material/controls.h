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

// Small coloured status dot; colour follows tone and the current theme.
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

// Spinbox without arrows. Its inner QLineEdit spans the whole field:
// otherwise QStyleSheetStyle shrinks it to 1 px and the value is invisible.
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

// Set the dynamic tone property and repolish (for status labels).
void setTone(QWidget* widget, const QString& tone);
void repolish(QWidget* widget);

// Remove and destroy every item in the layout (not the layout itself).
void clearLayout(QLayout* layout);

} // namespace ex::ui::controls

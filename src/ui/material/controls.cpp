#include "ui/material/controls.h"

#include "ui/material/icons.h"
#include "ui/theme.h"

#include <QFocusEvent>
#include <QFrame>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStyle>
#include <QTimer>

namespace ex::ui::controls {

NumberSpinBox::NumberSpinBox(QWidget* parent)
    : QSpinBox(parent)
{
    setButtonSymbols(QAbstractSpinBox::NoButtons);
}

void NumberSpinBox::focusInEvent(QFocusEvent* event)
{
    QSpinBox::focusInEvent(event);
    QTimer::singleShot(0, this, [this]() {
        if (QLineEdit* edit = findChild<QLineEdit*>())
            edit->deselect();
    });
}

void NumberSpinBox::resizeEvent(QResizeEvent* event)
{
    QSpinBox::resizeEvent(event);
    syncEditWidth();
}

void NumberSpinBox::showEvent(QShowEvent* event)
{
    QSpinBox::showEvent(event);
    syncEditWidth();
}

void NumberSpinBox::syncEditWidth()
{
    if (QLineEdit* edit = findChild<QLineEdit*>())
        edit->setMinimumWidth(qMax(1, width() - 12));
}

StatusDot::StatusDot(int size, QWidget* parent)
    : QWidget(parent)
    , m_size(size)
{
    setFixedSize(size, size);
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void StatusDot::setTone(const QString& tone)
{
    m_tone = tone;
    update();
}

void StatusDot::paintEvent(QPaintEvent*)
{
    const auto& c = theme::currentPalette();
    QColor color = c.outline;
    if (m_tone == QLatin1String("success"))
        color = c.success;
    else if (m_tone == QLatin1String("busy"))
        color = c.warning;
    else if (m_tone == QLatin1String("error"))
        color = c.error;
    else if (m_tone == QLatin1String("accent"))
        color = c.accent;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(rect().adjusted(1, 1, -1, -1));
}

QLabel* label(const QString& text, const QString& styleClass, QWidget* parent)
{
    auto* out = new QLabel(text, parent);
    if (!styleClass.isEmpty())
        out->setProperty("class", styleClass);
    return out;
}

QPushButton* button(const QString& text, const QString& variant, QWidget* parent)
{
    auto* out = new QPushButton(text, parent);
    out->setCursor(Qt::PointingHandCursor);
    if (!variant.isEmpty())
        out->setProperty("variant", variant);
    return out;
}

QPushButton* iconButton(const QString& icon, const QString& tooltip,
                        QWidget* parent, const QString& tone)
{
    auto* out = new QPushButton(parent);
    out->setProperty("class", QStringLiteral("iconBtn"));
    out->setCursor(Qt::PointingHandCursor);
    out->setToolTip(tooltip);
    out->setAccessibleName(tooltip);
    icons::setGlyph(out, icon, tone);
    out->setFixedSize(34, 34);
    return out;
}

QFrame* card(QWidget* parent)
{
    auto* out = new QFrame(parent);
    out->setProperty("class", QStringLiteral("card"));
    return out;
}

QFrame* separator(QWidget* parent)
{
    auto* out = new QFrame(parent);
    out->setProperty("class", QStringLiteral("separator"));
    out->setFixedHeight(1);
    return out;
}

void setTone(QWidget* widget, const QString& tone)
{
    if (!widget)
        return;
    widget->setProperty("tone", tone);
    repolish(widget);
}

void repolish(QWidget* widget)
{
    if (!widget)
        return;
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

void clearLayout(QLayout* layout)
{
    if (!layout)
        return;
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QLayout* child = item->layout())
            clearLayout(child);
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
}

} // namespace ex::ui::controls

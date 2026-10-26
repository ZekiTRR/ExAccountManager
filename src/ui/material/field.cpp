#include "ui/material/field.h"

#include "ui/material/icons.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QStyle>

namespace ex::ui {

Field::Field(const QString& iconName, QWidget* parent)
    : QFrame(parent)
{
    setProperty("class", QStringLiteral("field"));
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover);
    setFixedHeight(40);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 0, 7, 0);
    layout->setSpacing(8);

    m_icon = new icons::IconLabel(iconName, 16, this);
    m_icon->setVisible(!iconName.isEmpty());
    layout->addWidget(m_icon, 0, Qt::AlignVCenter);

    m_edit = new QLineEdit(this);
    m_edit->setFrame(false);
    m_edit->setAttribute(Qt::WA_MacShowFocusRect, false);
    m_edit->installEventFilter(this);
    layout->addWidget(m_edit, 1);

    m_trailing = new QPushButton(this);
    m_trailing->setProperty("class", QStringLiteral("fieldAction"));
    m_trailing->setCursor(Qt::PointingHandCursor);
    m_trailing->setIconSize(QSize(14, 14));
    m_trailing->hide();
    connect(m_trailing, &QPushButton::clicked, this, &Field::trailingClicked);
    layout->addWidget(m_trailing, 0, Qt::AlignVCenter);
}

QString Field::text() const
{
    return m_edit->text();
}

void Field::setText(const QString& text)
{
    m_edit->setText(text);
}

void Field::setPlaceholder(const QString& text)
{
    m_edit->setPlaceholderText(text);
}

void Field::setIcon(const QString& name)
{
    m_icon->setIcon(name);
    m_icon->setVisible(!name.isEmpty());
}

void Field::setInvalid(bool invalid)
{
    setProperty("invalid", invalid);
    repolish();
}

void Field::setTrailingIcon(const QString& name, const QString& tooltip)
{
    icons::setGlyph(m_trailing, name, QStringLiteral("muted"));
    m_trailing->setToolTip(tooltip);
    m_trailing->setVisible(!name.isEmpty());
    m_trailing->setFixedSize(24, 24);
}

bool Field::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_edit) {
        if (event->type() == QEvent::FocusIn) {
            setProperty("focused", true);
            repolish();
        } else if (event->type() == QEvent::FocusOut) {
            setProperty("focused", false);
            repolish();
        }
    }
    return QFrame::eventFilter(watched, event);
}

void Field::mousePressEvent(QMouseEvent* event)
{
    m_edit->setFocus(Qt::MouseFocusReason);
    QFrame::mousePressEvent(event);
}

void Field::repolish()
{
    style()->unpolish(this);
    style()->polish(this);
    update();
}

} // namespace ex::ui

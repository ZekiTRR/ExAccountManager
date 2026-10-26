#include "ui/material/stepper.h"

#include "ui/material/icons.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace ex::ui {

Stepper::Stepper(int minimum, int maximum, int value,
                 const QString& suffix, QWidget* parent)
    : QFrame(parent)
    , m_min(minimum)
    , m_max(maximum)
    , m_value(value)
{
    setProperty("class", QStringLiteral("stepper"));
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedHeight(32);

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(2, 2, 2, 2);
    row->setSpacing(0);

    m_minus = new QPushButton(this);
    m_minus->setProperty("class", QStringLiteral("stepBtn"));
    m_minus->setCursor(Qt::PointingHandCursor);
    icons::setGlyph(m_minus, QStringLiteral("minus"), QStringLiteral("muted"));
    m_minus->setFixedSize(28, 28);

    m_valueLabel = new QLabel(this);
    m_valueLabel->setProperty("class", QStringLiteral("stepperValue"));
    m_valueLabel->setAlignment(Qt::AlignCenter);
    m_valueLabel->setMinimumWidth(24);

    m_suffix = new QLabel(suffix, this);
    m_suffix->setProperty("class", QStringLiteral("tertiary"));
    if (suffix.isEmpty())
        m_suffix->hide();

    m_plus = new QPushButton(this);
    m_plus->setProperty("class", QStringLiteral("stepBtn"));
    m_plus->setCursor(Qt::PointingHandCursor);
    icons::setGlyph(m_plus, QStringLiteral("plus"), QStringLiteral("muted"));
    m_plus->setFixedSize(28, 28);

    row->addWidget(m_minus);
    row->addWidget(m_valueLabel);
    row->addWidget(m_suffix);
    row->addSpacing(4);
    row->addWidget(m_plus);

    connect(m_minus, &QPushButton::clicked, this, [this]() { step(-1); });
    connect(m_plus, &QPushButton::clicked, this, [this]() { step(1); });
    sync();
}

void Stepper::setValue(int value)
{
    const int next = qBound(m_min, value, m_max);
    if (next == m_value)
        return;
    m_value = next;
    sync();
    emit valueChanged(m_value);
}

void Stepper::setRange(int minimum, int maximum)
{
    m_min = minimum;
    m_max = qMax(minimum, maximum);
    setValue(qBound(m_min, m_value, m_max));
}

void Stepper::setSuffix(const QString& suffix)
{
    m_suffix->setText(suffix);
    m_suffix->setVisible(!suffix.isEmpty());
}

void Stepper::step(int delta)
{
    setValue(m_value + delta);
}

void Stepper::sync()
{
    m_valueLabel->setText(QString::number(m_value));
    m_minus->setEnabled(m_value > m_min);
    m_plus->setEnabled(m_value < m_max);
}

} // namespace ex::ui

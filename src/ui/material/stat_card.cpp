#include "ui/material/stat_card.h"

#include "ui/material/controls.h"
#include "ui/material/icons.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace ex::ui {

StatCard::StatCard(const QString& iconName, const QString& label,
                   const QString& value, const QString& foot,
                   const QString& tone, QWidget* parent)
    : QFrame(parent)
{
    setProperty("class", QStringLiteral("card"));
    setMinimumHeight(94);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(0);

    auto* top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(6);
    m_label = controls::label(label, QStringLiteral("statLabel"));
    top->addWidget(m_label);
    top->addStretch(1);
    auto* icon = new icons::IconLabel(iconName, 15, this);
    icon->setTone(tone);
    top->addWidget(icon);
    root->addLayout(top);
    root->addSpacing(7);

    m_value = controls::label(value, QStringLiteral("statValue"));
    root->addWidget(m_value);
    root->addSpacing(1);

    m_foot = controls::label(foot, QStringLiteral("statFoot"));
    root->addWidget(m_foot);
}

void StatCard::setValue(const QString& value)
{
    m_value->setText(value);
}

void StatCard::setFoot(const QString& foot)
{
    m_foot->setText(foot);
}

void StatCard::setLabel(const QString& label)
{
    m_label->setText(label);
}

} // namespace ex::ui

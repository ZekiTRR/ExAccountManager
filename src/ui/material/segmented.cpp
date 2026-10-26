#include "ui/material/segmented.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QPushButton>

namespace ex::ui {

Segmented::Segmented(QWidget* parent)
    : QFrame(parent)
{
    setProperty("class", QStringLiteral("seg"));

    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(5, 5, 5, 5);
    row->setSpacing(2);
}

void Segmented::addItem(const QString& text, const QVariant& itemData)
{
    auto* btn = new QPushButton(text);
    btn->setCheckable(true);
    btn->setProperty("class", QStringLiteral("segItem"));
    btn->setProperty("segData", itemData);
    btn->setCursor(Qt::PointingHandCursor);
    m_group->addButton(btn, static_cast<int>(m_buttons.size()));
    m_buttons.append(btn);

    auto* row = static_cast<QHBoxLayout*>(layout());
    row->addWidget(btn);

    connect(btn, &QPushButton::toggled, this, [this, btn](bool on) {
        if (on)
            emit currentIndexChanged(m_buttons.indexOf(btn));
    });
    if (m_buttons.size() == 1)
        btn->setChecked(true);
}

int Segmented::count() const
{
    return static_cast<int>(m_buttons.size());
}

int Segmented::currentIndex() const
{
    for (int i = 0; i < m_buttons.size(); ++i) {
        if (m_buttons.at(i)->isChecked())
            return i;
    }
    return -1;
}

QVariant Segmented::currentData() const
{
    const int idx = currentIndex();
    return idx >= 0 ? m_buttons.at(idx)->property("segData") : QVariant();
}

QString Segmented::currentText() const
{
    const int idx = currentIndex();
    return idx >= 0 ? m_buttons.at(idx)->text() : QString();
}

void Segmented::setCurrentIndex(int index)
{
    if (index >= 0 && index < m_buttons.size())
        m_buttons.at(index)->setChecked(true);
}

void Segmented::setCurrentData(const QVariant& itemData)
{
    for (QPushButton* btn : m_buttons) {
        if (btn->property("segData") == itemData) {
            btn->setChecked(true);
            return;
        }
    }
}

} // namespace ex::ui

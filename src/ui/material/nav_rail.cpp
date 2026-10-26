#include "ui/material/nav_rail.h"

#include "ui/material/controls.h"
#include "ui/material/icons.h"
#include "ui/theme.h"

#include <QButtonGroup>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace ex::ui {
namespace {

constexpr int kRailWidth = 232;
constexpr int kButtonHeight = 42;
constexpr int kButtonRadius = 12;

class BrandMark : public QWidget {
public:
    explicit BrandMark(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(36, 36);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QLinearGradient g(0, 0, width(), height());
        g.setColorAt(0.0, QColor(0x9A, 0x83, 0xFF));
        g.setColorAt(0.56, QColor(0x6A, 0x51, 0xEF));
        g.setColorAt(1.0, QColor(0x35, 0xC8, 0xA5));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawRoundedRect(rect(), 11, 11);
        QFont f(QStringLiteral("Segoe UI Variable Display"));
        f.setPixelSize(15);
        f.setWeight(QFont::Medium);
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Ex"));
    }
};

// Пункт навигации рисует себя сам: глиф, подпись и pill активного состояния.
class NavButton : public QPushButton {
public:
    explicit NavButton(const NavRail::Destination& dest, QWidget* parent = nullptr)
        : QPushButton(parent)
        , m_icon(dest.icon)
        , m_label(dest.label)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setFixedHeight(kButtonHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setToolTip(dest.label);
        setAccessibleName(dest.label);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const auto& c = theme::currentPalette();

        const QRectF pill = QRectF(rect()).adjusted(0.5, 1, -0.5, -1);
        if (isChecked()) {
            p.setPen(Qt::NoPen);
            p.setBrush(c.secondaryContainer);
            p.drawRoundedRect(pill, kButtonRadius, kButtonRadius);
        } else if (underMouse() || isDown()) {
            QColor overlay = c.onSurface;
            overlay.setAlpha(isDown() ? 20 : 12);
            p.setPen(Qt::NoPen);
            p.setBrush(overlay);
            p.drawRoundedRect(pill, kButtonRadius, kButtonRadius);
        }

        const QColor iconColor = isChecked() ? c.accent : c.icon;
        const QColor textColor = isChecked() ? c.onSurface : c.onSurfaceVariant;

        const QRectF iconRect(pill.left() + 11, pill.center().y() - 10, 20, 20);
        icons::paint(&p, m_icon, iconRect, iconColor);

        QFont labelFont = font();
        labelFont.setPixelSize(13);
        labelFont.setWeight(QFont::Medium);
        p.setFont(labelFont);
        p.setPen(textColor);

        const int badge = property("badgeCount").toInt();
        int badgeWidth = 0;
        if (badge > 0) {
            const QString badgeText = QString::number(badge);
            const QFont badgeFont = [&]() {
                QFont f = font();
                f.setPixelSize(11);
                f.setWeight(QFont::Medium);
                return f;
            }();
            const QFontMetrics fm(badgeFont);
            badgeWidth = qMax(20, fm.horizontalAdvance(badgeText) + 12);
            const QRectF badgeRect(pill.right() - 9 - badgeWidth,
                                   pill.center().y() - 10, badgeWidth, 20);
            QColor badgeBg = c.success;
            badgeBg.setAlpha(30);
            p.setPen(Qt::NoPen);
            p.setBrush(badgeBg);
            p.drawRoundedRect(badgeRect, 10, 10);
            p.setFont(badgeFont);
            p.setPen(c.success);
            p.drawText(badgeRect, Qt::AlignCenter, badgeText);
        }

        const QRectF textRect(iconRect.right() + 11, pill.top(),
                              pill.width() - 11 - 20 - 11 - 10 - badgeWidth,
                              pill.height());
        p.setFont(labelFont);
        p.setPen(textColor);
        p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                   p.fontMetrics().elidedText(m_label, Qt::ElideRight,
                                              qRound(textRect.width())));
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        QPushButton::mousePressEvent(event);
        update();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        QPushButton::mouseReleaseEvent(event);
        update();
    }

private:
    QString m_icon;
    QString m_label;
};

} // namespace

NavRail::NavRail(const QList<Destination>& destinations, QWidget* parent)
    : QFrame(parent)
{
    setFixedWidth(kRailWidth);
    setObjectName(QStringLiteral("navRail"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 20, 12, 14);
    root->setSpacing(2);

    auto* brand = new QHBoxLayout;
    brand->setContentsMargins(7, 0, 4, 20);
    brand->setSpacing(10);
    brand->addWidget(new BrandMark(this), 0, Qt::AlignVCenter);

    auto* brandCopy = new QVBoxLayout;
    brandCopy->setContentsMargins(0, 0, 0, 0);
    brandCopy->setSpacing(1);
    brandCopy->addWidget(controls::label(QStringLiteral("ExAccount"),
                                         QStringLiteral("brandName")));
    auto* sub = controls::label(QStringLiteral("MANAGER"),
                                QStringLiteral("brandSub"));
    QFont subFont = sub->font();
    subFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.8);
    sub->setFont(subFont);
    brandCopy->addWidget(sub);
    brand->addLayout(brandCopy, 1);
    root->addLayout(brand);

    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);
    for (const Destination& dest : destinations) {
        auto* button = new NavButton(dest, this);
        m_group->addButton(button, static_cast<int>(m_buttons.size()));
        m_buttons.append(button);
        root->addWidget(button);
        if (dest.icon == QLatin1String("instances"))
            m_instanceButton = button;
        connect(button, &QPushButton::toggled, this, [this, button](bool on) {
            if (on)
                emit currentChanged(m_buttons.indexOf(button));
        });
    }
    if (!m_buttons.isEmpty())
        m_buttons.first()->setChecked(true);

    root->addStretch(1);
    buildFooter();
    updateThemeButtons();
}

void NavRail::setCurrentIndex(int index)
{
    if (index >= 0 && index < m_buttons.size())
        m_buttons.at(index)->setChecked(true);
}

int NavRail::currentIndex() const
{
    for (int i = 0; i < m_buttons.size(); ++i) {
        if (m_buttons.at(i)->isChecked())
            return i;
    }
    return -1;
}

void NavRail::setThemeName(const QString& name)
{
    m_theme = name == QLatin1String("light") ? QStringLiteral("light")
                                             : QStringLiteral("dark");
    updateThemeButtons();
}

void NavRail::setInstanceCount(int count)
{
    if (!m_instanceButton)
        return;
    m_instanceButton->setProperty("badgeCount", count);
    m_instanceButton->update();
}

void NavRail::buildFooter()
{
    auto* root = static_cast<QVBoxLayout*>(layout());

    auto* line = controls::separator(this);
    root->addSpacing(10);
    root->addWidget(line);
    root->addSpacing(10);

    auto* frame = new QFrame(this);
    frame->setProperty("class", QStringLiteral("themeSwitch"));
    auto* row = new QHBoxLayout(frame);
    row->setContentsMargins(3, 3, 3, 3);
    row->setSpacing(2);

    const auto makeThemeButton = [this, frame](const QString& name,
                                                const QString& icon) {
        auto* button = new QPushButton(frame);
        button->setProperty("class", QStringLiteral("themeOption"));
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(28);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setToolTip(name);
        button->setAccessibleName(name);
        icons::setGlyph(button, icon, QStringLiteral("muted"));
        return button;
    };

    m_darkButton = makeThemeButton(tr("Тёмная тема"), QStringLiteral("moon"));
    m_lightButton = makeThemeButton(tr("Светлая тема"), QStringLiteral("sun"));

    auto* group = new QButtonGroup(frame);
    group->setExclusive(true);
    group->addButton(m_darkButton);
    group->addButton(m_lightButton);

    row->addWidget(m_darkButton);
    row->addWidget(m_lightButton);
    root->addWidget(frame);

    connect(m_darkButton, &QPushButton::clicked, this, [this]() {
        emit themeChangeRequested(QStringLiteral("dark"));
    });
    connect(m_lightButton, &QPushButton::clicked, this, [this]() {
        emit themeChangeRequested(QStringLiteral("light"));
    });
}

void NavRail::updateThemeButtons()
{
    if (!m_darkButton || !m_lightButton)
        return;

    const QSignalBlocker blockDark(m_darkButton);
    const QSignalBlocker blockLight(m_lightButton);
    const bool dark = m_theme == QLatin1String("dark");
    m_darkButton->setChecked(dark);
    m_lightButton->setChecked(!dark);
    icons::setGlyph(m_darkButton, QStringLiteral("moon"),
                    dark ? QStringLiteral("accent") : QStringLiteral("auto"));
    icons::setGlyph(m_lightButton, QStringLiteral("sun"),
                    dark ? QStringLiteral("auto") : QStringLiteral("accent"));
}

void NavRail::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const auto& c = theme::currentPalette();
    p.fillRect(rect(), c.surfaceContainerLow);
    p.setPen(QPen(c.outlineVariant, 1));
    p.drawLine(width() - 1, 0, width() - 1, height());
}

} // namespace ex::ui

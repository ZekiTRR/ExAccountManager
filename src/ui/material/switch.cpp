#include "ui/material/switch.h"

#include "ui/theme.h"

#include <QPainter>
#include <QVariantAnimation>

namespace ex::ui {
namespace {

constexpr int kTrackWidth = 40;
constexpr int kTrackHeight = 24;
constexpr int kThumbOn = 18;
constexpr int kThumbOff = 15;

} // namespace

Switch::Switch(QWidget* parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // M3 spring: the thumb springs over quickly on toggle.
    m_thumbAnim = new QVariantAnimation(this);
    m_thumbAnim->setDuration(150);
    m_thumbAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_thumbAnim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& v) {
                m_thumbPos = v.toReal();
                update();
            });
    connect(this, &QAbstractButton::toggled, this, [this](bool on) {
        m_thumbAnim->stop();
        m_thumbAnim->setStartValue(m_thumbPos);
        m_thumbAnim->setEndValue(on ? 1.0 : 0.0);
        m_thumbAnim->start();
    });
}

QSize Switch::sizeHint() const
{
    return {kTrackWidth, kTrackHeight};
}

void Switch::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const auto& c = theme::currentPalette();

    const QRectF track(0, 0, kTrackWidth, kTrackHeight);
    const qreal radius = kTrackHeight / 2.0;

    if (isChecked()) {
        p.setPen(Qt::NoPen);
        p.setBrush(c.primary);
        p.drawRoundedRect(track, radius, radius);
    } else {
        p.setPen(QPen(c.outlineVariant, 1));
        p.setBrush(c.fieldBg);
        p.drawRoundedRect(track.adjusted(0.5, 0.5, -0.5, -0.5),
                          radius, radius);
    }

    const int grow = isDown() ? 1 : 0;
    const qreal d = kThumbOff + (kThumbOn - kThumbOff) * m_thumbPos + grow;
    const qreal x = 4 + (kTrackWidth - 8 - kThumbOn) * m_thumbPos;
    const QPointF center(x + d / 2, track.center().y());
    p.setPen(Qt::NoPen);
    p.setBrush(isChecked() ? c.onPrimary : c.onSurfaceVariant);
    p.drawEllipse(center, d / 2, d / 2);
}

void Switch::resizeEvent(QResizeEvent* event)
{
    QAbstractButton::resizeEvent(event);
    update();
}

} // namespace ex::ui

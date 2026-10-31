#include "ui/material/icons.h"

#include "ui/theme.h"

#include <QApplication>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPushButton>
#include <QSvgRenderer>
#include <QWidget>
#include <QtMath>

#include <algorithm>

namespace ex::ui::icons {
namespace {

// Old Material glyph names stay as aliases: dialogs and context menus
// menus keep working after the move to our own SVG set.
const QHash<QString, QString>& aliases()
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("person"), QStringLiteral("accounts")},
        {QStringLiteral("group"), QStringLiteral("accounts")},
        {QStringLiteral("groups"), QStringLiteral("accounts")},
        {QStringLiteral("grid_view"), QStringLiteral("instances")},
        {QStringLiteral("monitoring"), QStringLiteral("analytics")},
        {QStringLiteral("insights"), QStringLiteral("analytics")},
        {QStringLiteral("tune"), QStringLiteral("settings")},
        {QStringLiteral("tuning"), QStringLiteral("settings")},
        {QStringLiteral("person_add"), QStringLiteral("user-plus")},
        {QStringLiteral("add"), QStringLiteral("plus")},
        {QStringLiteral("delete"), QStringLiteral("trash")},
        {QStringLiteral("stop"), QStringLiteral("power")},
        {QStringLiteral("visibility"), QStringLiteral("eye")},
        {QStringLiteral("visibility_off"), QStringLiteral("eye-off")},
        {QStringLiteral("warning"), QStringLiteral("alert")},
        {QStringLiteral("error"), QStringLiteral("alert")},
        {QStringLiteral("memory"), QStringLiteral("ram")},
        {QStringLiteral("speed"), QStringLiteral("trend")},
        {QStringLiteral("schedule"), QStringLiteral("clock")},
        {QStringLiteral("center_focus_strong"), QStringLiteral("focus")},
        {QStringLiteral("expand_more"), QStringLiteral("chevron-down")},
        {QStringLiteral("chevron_right"), QStringLiteral("chevron-right")},
        {QStringLiteral("dark_mode"), QStringLiteral("moon")},
        {QStringLiteral("light_mode"), QStringLiteral("sun")},
        {QStringLiteral("check_circle"), QStringLiteral("check")},
        {QStringLiteral("hourglass_empty"), QStringLiteral("hourglass")},
        {QStringLiteral("play_arrow"), QStringLiteral("play-filled")},
        {QStringLiteral("rocket_launch"), QStringLiteral("instances")},
        {QStringLiteral("more_vert"), QStringLiteral("more")},
        {QStringLiteral("drive_file_move"), QStringLiteral("folder")},
        {QStringLiteral("history"), QStringLiteral("clock")},
        {QStringLiteral("public"), QStringLiteral("link")},
        {QStringLiteral("sports_esports"), QStringLiteral("gamepad")},
        {QStringLiteral("language"), QStringLiteral("link")},
        {QStringLiteral("cloud_off"), QStringLiteral("alert")},
        {QStringLiteral("pending"), QStringLiteral("hourglass")},
        {QStringLiteral("edit"), QStringLiteral("save")},
        {QStringLiteral("login"), QStringLiteral("user-plus")},
        {QStringLiteral("password"), QStringLiteral("lock")},
    };
    return map;
}

QString assetName(const QString& name)
{
    auto it = aliases().constFind(name);
    return it == aliases().constEnd() ? name : it.value();
}

QHash<QString, QPixmap>& cache()
{
    static QHash<QString, QPixmap> value;
    return value;
}

QColor withAlpha(const QColor& color, int alpha)
{
    QColor copy = color;
    copy.setAlpha(alpha);
    return copy;
}

QIcon iconFor(const QString& name, int px, const QColor& normal,
              const QColor& active, const QColor& disabled)
{
    const QPixmap n = pixmap(name, px, normal);
    if (n.isNull())
        return {};
    QIcon icon;
    icon.addPixmap(n, QIcon::Normal, QIcon::Off);
    icon.addPixmap(pixmap(name, px, active), QIcon::Active, QIcon::Off);
    icon.addPixmap(pixmap(name, px, disabled), QIcon::Disabled, QIcon::Off);
    return icon;
}

QColor buttonNormalColor(const QPushButton* button)
{
    const auto& c = theme::currentPalette();
    const QString tone = button->property("iconTone").toString();
    if (!tone.isEmpty() && tone != QLatin1String("auto"))
        return toneColor(tone);
    if (button->property("variant").toString() == QLatin1String("filled")
        || button->property("primary").toBool() || button->isDefault()) {
        return c.onPrimary;
    }
    if (button->objectName() == QLatin1String("danger")
        || button->property("variant").toString() == QLatin1String("danger")) {
        return c.error;
    }
    return c.icon;
}

QColor buttonActiveColor(const QPushButton* button)
{
    const auto& c = theme::currentPalette();
    const QString tone = button->property("iconTone").toString();
    if (tone == QLatin1String("onAccent"))
        return c.onPrimary;
    if (tone == QLatin1String("error"))
        return c.error;
    if (button->property("variant").toString() == QLatin1String("filled")
        || button->property("primary").toBool() || button->isDefault()) {
        return c.onPrimary;
    }
    return c.primary;
}

void applyButtonIcon(QPushButton* button)
{
    const QString name = button->property("iconGlyph").toString();
    if (name.isEmpty()) {
        button->setIcon(QIcon());
        return;
    }
    const QColor normal = buttonNormalColor(button);
    const QColor active = buttonActiveColor(button);
    const QColor disabled = withAlpha(normal, 110);
    const int px = button->property("class").toString() == QLatin1String("iconBtn")
        ? 17 : 16;
    button->setIcon(iconFor(name, px, normal, active, disabled));
    button->setIconSize(QSize(px, px));
}

} // namespace

void init()
{
    // SVGs load lazily from qrc; no separate init is needed.
}

QColor toneColor(const QString& tone)
{
    const auto& c = theme::currentPalette();
    if (tone == QLatin1String("onAccent"))
        return c.onPrimary;
    if (tone == QLatin1String("accent"))
        return c.primary;
    if (tone == QLatin1String("success"))
        return c.success;
    if (tone == QLatin1String("warning"))
        return c.warning;
    if (tone == QLatin1String("error"))
        return c.error;
    if (tone == QLatin1String("muted"))
        return c.iconDim;
    return c.icon;
}

QPixmap pixmap(const QString& name, int px, const QColor& color, qreal dpr)
{
    if (px <= 0 || !color.isValid())
        return {};
    if (dpr <= 0.0)
        dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    const QString asset = assetName(name);
    const QString key = QStringLiteral("%1|%2|%3|%4")
        .arg(asset).arg(px).arg(color.rgba()).arg(qRound(dpr * 100.0));
    auto it = cache().constFind(key);
    if (it != cache().constEnd())
        return it.value();

    QSvgRenderer renderer(QStringLiteral(":/icons/%1.svg").arg(asset));
    if (!renderer.isValid())
        return {};

    const int side = qCeil(px * dpr);
    QImage image(side, side, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        renderer.render(&p, QRectF(0, 0, side, side));
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(image.rect(), color);
    }
    QPixmap pm = QPixmap::fromImage(image);
    pm.setDevicePixelRatio(dpr);
    cache().insert(key, pm);
    return pm;
}

void paint(QPainter* painter, const QString& name, const QRectF& rect,
           const QColor& color)
{
    if (!painter || rect.isEmpty())
        return;
    const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    const int side = qRound(std::min(rect.width(), rect.height()));
    const QPixmap pm = pixmap(name, side, color, dpr);
    if (pm.isNull())
        return;
    const qreal w = pm.width() / pm.devicePixelRatio();
    const qreal h = pm.height() / pm.devicePixelRatio();
    const QRectF target(rect.center().x() - w / 2.0,
                        rect.center().y() - h / 2.0, w, h);
    painter->drawPixmap(target, pm, QRectF(0, 0, pm.width(), pm.height()));
}

QIcon makeIcon(const QString& name, int px)
{
    const auto& c = theme::currentPalette();
    return iconFor(name, px, c.icon, c.primary, withAlpha(c.iconDim, 110));
}

void setGlyph(QPushButton* button, const QString& name, const QString& tone)
{
    if (!button)
        return;
    button->setProperty("iconGlyph", name);
    button->setProperty("iconTone",
                        tone.isEmpty() ? QStringLiteral("auto") : tone);
    applyButtonIcon(button);
}

void refreshAllWidgets()
{
    const auto widgets = qApp->allWidgets();
    for (QWidget* w : widgets) {
        if (auto* button = qobject_cast<QPushButton*>(w)) {
            if (button->property("iconGlyph").isValid())
                applyButtonIcon(button);
        }
        if (auto* label = qobject_cast<IconLabel*>(w))
            label->update();
        w->update();
    }
}

QIcon appIcon()
{
    // The brand icon ignores the theme.
    const QColor top(0x9A, 0x83, 0xFF);
    const QColor middle(0x6A, 0x51, 0xEF);
    const QColor bottom(0x35, 0xC8, 0xA5);

    QIcon icon;
    for (int px : {16, 24, 32, 48, 64, 128, 256}) {
        QPixmap pm(px, px);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);

        QLinearGradient g(0, 0, px, px);
        g.setColorAt(0.0, top);
        g.setColorAt(0.58, middle);
        g.setColorAt(1.0, bottom);
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawRoundedRect(QRectF(0, 0, px, px), px * 0.28, px * 0.28);

        QFont f(QStringLiteral("Segoe UI Variable Display"));
        f.setPixelSize(qRound(px * (px < 24 ? 0.50 : 0.40)));
        f.setWeight(QFont::Medium);
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(QRectF(0, 0, px, px), Qt::AlignCenter,
                   px < 24 ? QStringLiteral("E") : QStringLiteral("Ex"));
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

IconLabel::IconLabel(const QString& name, int pixelSize, QWidget* parent)
    : QWidget(parent)
    , m_name(name)
    , m_pixelSize(pixelSize)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedSize(m_pixelSize, m_pixelSize);
}

void IconLabel::setIcon(const QString& name)
{
    m_name = name;
    update();
}

void IconLabel::setIconSize(int pixelSize)
{
    m_pixelSize = qMax(1, pixelSize);
    setFixedSize(m_pixelSize, m_pixelSize);
    update();
}

void IconLabel::setTone(const QString& tone)
{
    m_tone = tone;
    update();
}

QColor IconLabel::effectiveColor() const
{
    const auto& c = theme::currentPalette();
    if (!isEnabled())
        return c.iconDim;
    if (!m_tone.isEmpty())
        return toneColor(m_tone);
    const QString cls = property("class").toString();
    if (cls == QLatin1String("iconPrimary"))
        return c.primary;
    if (cls == QLatin1String("iconSuccess"))
        return c.success;
    if (cls == QLatin1String("iconWarning"))
        return c.warning;
    if (cls == QLatin1String("iconError"))
        return c.error;
    return c.icon;
}

void IconLabel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    paint(&p, m_name, rect(), effectiveColor());
}

} // namespace ex::ui::icons

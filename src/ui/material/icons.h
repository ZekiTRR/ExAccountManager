#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QWidget>

class QPainter;
class QPushButton;

namespace ex::ui::icons {

// Initialise icon resources. Calling it again is safe.
void init();

// Button icon: name is the SVG in :/icons, px the logical size.
QIcon makeIcon(const QString& name, int px = 16);

// Attach an icon to a button. tone: auto / onAccent / accent / muted / error.
// The icon is retinted on a theme change via refreshAllWidgets().
void setGlyph(QPushButton* button, const QString& name,
              const QString& tone = QStringLiteral("auto"));

// Repaint icons and labels after a theme change.
void refreshAllWidgets();

// App icon: gradient squircle with the Ex monogram.
QIcon appIcon();

// Pixmap of a given colour (cached; honours devicePixelRatio).
QPixmap pixmap(const QString& name, int px, const QColor& color, qreal dpr = 0.0);

// Draw the icon inside a rect, keeping its aspect ratio.
void paint(QPainter* painter, const QString& name, const QRectF& rect,
           const QColor& color);

// Semantic colour for a tone in the current palette.
QColor toneColor(const QString& tone);

// Light icon label; colour comes from property("class") or setTone().
class IconLabel : public QWidget {
    Q_OBJECT
public:
    explicit IconLabel(const QString& name = QString(), int pixelSize = 24,
                       QWidget* parent = nullptr);

    void setIcon(const QString& name);
    void setIconSize(int pixelSize);
    void setTone(const QString& tone);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QColor effectiveColor() const;

    QString m_name;
    int m_pixelSize = 24;
    QString m_tone;
};

} // namespace ex::ui::icons

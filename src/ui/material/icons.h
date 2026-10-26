#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QWidget>

class QPainter;
class QPushButton;

namespace ex::ui::icons {

// Инициализация ресурсов иконок. Повторный вызов безопасен.
void init();

// Иконка для кнопок: name — имя SVG в :/icons, px — логический размер.
QIcon makeIcon(const QString& name, int px = 16);

// Повесить иконку на кнопку. tone: auto / onAccent / accent / muted / error.
// Иконка перекрашивается при смене темы через refreshAllWidgets().
void setGlyph(QPushButton* button, const QString& name,
              const QString& tone = QStringLiteral("auto"));

// Перерисовать иконки и метки после смены темы.
void refreshAllWidgets();

// Иконка приложения: градиентный squircle с монограммой Ex.
QIcon appIcon();

// Пиксмапа заданного цвета (кэшируется; учитывает devicePixelRatio).
QPixmap pixmap(const QString& name, int px, const QColor& color, qreal dpr = 0.0);

// Нарисовать иконку в прямоугольнике, сохраняя пропорции.
void paint(QPainter* painter, const QString& name, const QRectF& rect,
           const QColor& color);

// Семантический цвет тона из текущей палитры.
QColor toneColor(const QString& tone);

// Лёгкая метка-иконка; цвет берётся из property("class") или setTone().
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

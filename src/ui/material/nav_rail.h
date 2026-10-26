#pragma once

#include <QFrame>

class QButtonGroup;
class QPushButton;

namespace ex::ui {

// Левая навигационная панель: бренд, пункты разделов, переключатель темы.
// Версия и статус хранилища в футере намеренно отсутствуют.
class NavRail : public QFrame {
    Q_OBJECT
public:
    struct Destination {
        QString icon;
        QString label;
    };

    explicit NavRail(const QList<Destination>& destinations,
                     QWidget* parent = nullptr);

    void setCurrentIndex(int index);
    [[nodiscard]] int currentIndex() const;

    void setThemeName(const QString& name);
    void setInstanceCount(int count);

signals:
    void currentChanged(int index);
    void themeChangeRequested(const QString& name);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void buildFooter();
    void updateThemeButtons();

    QList<QPushButton*> m_buttons;
    QButtonGroup* m_group = nullptr;
    QPushButton* m_instanceButton = nullptr;
    QPushButton* m_darkButton = nullptr;
    QPushButton* m_lightButton = nullptr;
    QString m_theme = QStringLiteral("dark");
};

} // namespace ex::ui

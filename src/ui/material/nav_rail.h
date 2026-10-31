#pragma once

#include <QFrame>

class QButtonGroup;
class QPushButton;

namespace ex::ui {

// Left nav panel: brand, section items, theme switch.
// Version and vault status are deliberately absent from the footer.
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

#pragma once

#include <QFrame>

class QLineEdit;
class QPushButton;

namespace ex::ui {
namespace icons { class IconLabel; }

// Input field with a leading SVG icon and an optional trailing button.
// Focus/invalid states show on the outline through dynamic properties.
class Field : public QFrame {
    Q_OBJECT
public:
    explicit Field(const QString& iconName = QString(), QWidget* parent = nullptr);

    [[nodiscard]] QLineEdit* lineEdit() const { return m_edit; }
    [[nodiscard]] QString text() const;
    void setText(const QString& text);
    void setPlaceholder(const QString& text);
    void setIcon(const QString& name);
    void setInvalid(bool invalid);
    void setTrailingIcon(const QString& name, const QString& tooltip = QString());
    [[nodiscard]] QPushButton* trailingButton() const { return m_trailing; }

signals:
    void trailingClicked();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void repolish();

    icons::IconLabel* m_icon = nullptr;
    QLineEdit* m_edit = nullptr;
    QPushButton* m_trailing = nullptr;
};

} // namespace ex::ui

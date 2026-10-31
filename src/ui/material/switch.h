#pragma once

#include <QAbstractButton>

class QVariantAnimation;

namespace ex::ui {

// Material 3 switch: 52x32 track, thumb with a flight animation.
// QCheckBox API: checked()/setChecked()/toggled(bool).
class Switch : public QAbstractButton {
    Q_OBJECT
public:
    explicit Switch(QWidget* parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    qreal m_thumbPos = 0.0; // 0 = off (left), 1 = on (right)
    QVariantAnimation* m_thumbAnim = nullptr;
};

} // namespace ex::ui

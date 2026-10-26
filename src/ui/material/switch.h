#pragma once

#include <QAbstractButton>

class QVariantAnimation;

namespace ex::ui {

// Switch в стиле Material 3: трек 52x32, бегунок с анимацией перелёта.
// API как у QCheckBox: checked()/setChecked()/toggled(bool).
class Switch : public QAbstractButton {
    Q_OBJECT
public:
    explicit Switch(QWidget* parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    qreal m_thumbPos = 0.0; // 0 = выкл (слева), 1 = вкл (справа)
    QVariantAnimation* m_thumbAnim = nullptr;
};

} // namespace ex::ui

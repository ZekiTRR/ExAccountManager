#pragma once

#include <QFrame>

class QButtonGroup;
class QPushButton;

namespace ex::ui {

// M3 segmented control (mutually exclusive pills in one frame).
// QComboBox stand-in for short sets: currentIndex()/currentData().
class Segmented : public QFrame {
    Q_OBJECT
public:
    explicit Segmented(QWidget* parent = nullptr);

    void addItem(const QString& text, const QVariant& itemData = {});
    [[nodiscard]] int count() const;
    [[nodiscard]] int currentIndex() const;
    [[nodiscard]] QVariant currentData() const;
    [[nodiscard]] QString currentText() const;
    void setCurrentIndex(int index);
    void setCurrentData(const QVariant& itemData);

signals:
    void currentIndexChanged(int index);

private:
    QList<QPushButton*> m_buttons;
    QButtonGroup* m_group = nullptr;
};

} // namespace ex::ui

#pragma once

#include <QFrame>

class QButtonGroup;
class QPushButton;

namespace ex::ui {

// Сегмент-контрол M3 (mutually exclusive pills в общей рамке).
// Аналог QComboBox для коротких наборов: currentIndex()/currentData().
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

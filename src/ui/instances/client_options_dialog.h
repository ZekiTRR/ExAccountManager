#pragma once

#include <QDialog>
#include <QList>
#include <QSize>

class QLabel;
class QPushButton;
class QSpinBox;

namespace ex::ui {

// Roblox window size and CPU/RAM limits for selected or all clients.
// Actions apply at once; the dialog stays open for the next operation.
class ClientOptionsDialog : public QDialog {
public:
    ClientOptionsDialog(const QList<qint64>& selectedPids,
                        const QList<qint64>& allPids, bool preferAll,
                        int windowWidth, int windowHeight,
                        int cpuPercent, qint64 ramMb,
                        QWidget* parent = nullptr);

    [[nodiscard]] QSize windowSize() const;
    [[nodiscard]] int cpuPercent() const;
    [[nodiscard]] qint64 ramMb() const;

private:
    [[nodiscard]] QList<qint64> targets() const;
    void updateActionState();
    void applyWindowSize();
    void arrangeGrid();
    void applyLimits();
    void clearLimits();
    void setStatus(const QString& text, const QString& tone = QString());

    QList<qint64> m_selected;
    QList<qint64> m_all;
    QPushButton* m_scopeSelected = nullptr;
    QPushButton* m_scopeAll = nullptr;
    QList<QPushButton*> m_actionButtons;
    QSpinBox* m_width = nullptr;
    QSpinBox* m_height = nullptr;
    QSpinBox* m_cpu = nullptr;
    QSpinBox* m_ram = nullptr;
    QLabel* m_status = nullptr;
};

} // namespace ex::ui

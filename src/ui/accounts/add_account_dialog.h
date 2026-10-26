#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;

namespace ex::ui {

class Field;

// Диалог добавления аккаунтов: cookie / массовый импорт / логин-пароль.
// Чистая форма: сервисы вызывает AccountsView после exec().
class AddAccountDialog : public QDialog {
    Q_OBJECT
public:
    enum class Mode { Cookie = 0, Bulk = 1, LoginPass = 2 };

    explicit AddAccountDialog(QWidget* parent = nullptr);

    void setMode(Mode mode);
    [[nodiscard]] Mode mode() const;
    [[nodiscard]] QString cookieText() const;
    [[nodiscard]] QString bulkText() const;
    [[nodiscard]] QString username() const;
    [[nodiscard]] QString password() const;

private:
    void buildUi();
    void setTab(int index);
    void tryAccept();
    void showError(const QString& text);
    void clearError();

    QList<QPushButton*> m_tabs;
    QStackedWidget* m_pages = nullptr;
    QPlainTextEdit* m_cookieEdit = nullptr;
    QPlainTextEdit* m_bulkEdit = nullptr;
    QLineEdit* m_userEdit = nullptr;
    QLineEdit* m_passEdit = nullptr;
    QLabel* m_error = nullptr;
};

} // namespace ex::ui

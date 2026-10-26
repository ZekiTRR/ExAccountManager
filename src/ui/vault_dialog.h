#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

namespace ex::ui {

class AppContext;

// Разблокировка/восстановление хранилища секретов при старте.
//  - password-required: запрос мастер-пароля;
//  - повреждённый/потерянный device-ключ: предложение пересоздать хранилище
//    (старые cookie нерасшифровываемы — это честно сказано в диалоге).
class VaultDialog : public QDialog {
    Q_OBJECT
public:
    explicit VaultDialog(AppContext& ctx, QWidget* parent = nullptr);

    // Показывает диалог, если vault не разблокирован.
    // Возвращает true, если в итоге vault разблокирован.
    static bool ensureUnlocked(AppContext& ctx, QWidget* parent);

private:
    void buildUi();
    void tryUnlock();
    void tryReset();

    AppContext& m_ctx;
    QLabel* m_status = nullptr;
    QLineEdit* m_password = nullptr;
    QPushButton* m_unlockBtn = nullptr;
    QPushButton* m_resetBtn = nullptr;
};

} // namespace ex::ui

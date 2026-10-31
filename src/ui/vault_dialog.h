#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

namespace ex::ui {

class AppContext;

// Unlock/repair the secret vault at start-up.
//  - password-required: ask for the master password;
//  - damaged/lost device key: offer to recreate the vault
//    (old cookies cannot be decrypted — the dialog says so plainly).
class VaultDialog : public QDialog {
    Q_OBJECT
public:
    explicit VaultDialog(AppContext& ctx, QWidget* parent = nullptr);

    // Shows the dialog when the vault is locked.
    // Returns true once the vault ends up unlocked.
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

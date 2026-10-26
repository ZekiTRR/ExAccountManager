#pragma once

#include <QDialog>

class QTableWidget;
class QLabel;
class QPushButton;

namespace ex::roblox {
class CdpLoginService;
struct CdpLoginRequest;
}

namespace ex::ui {

class AppContext;

// Диалог входа через браузер (CDP): таблица сессий (логин, статус),
// прогресс, отмена. По перехвату cookie — добавляет аккаунт через
// AccountService и сохраняет пароль в vault.
class CdpLoginDialog : public QDialog {
    Q_OBJECT
public:
    explicit CdpLoginDialog(AppContext& ctx, QWidget* parent = nullptr);

    void start(const QList<ex::roblox::CdpLoginRequest>& requests, int concurrency = 2);

protected:
    void reject() override;

private:
    void onLoginSucceeded(const QString& username, const QString& cookie, const QString& password,
                          qint64 userId, const QString& realUsername,
                          const QString& realDisplayName);
    void onSessionStatus(const QString& username, int status, const QString& detail);
    void markRowError(const QString& username, const QString& error);
    void updateCloseState();
    void updateSummary();

    AppContext& m_ctx;
    ex::roblox::CdpLoginService* m_service = nullptr; // owned
    QTableWidget* m_table = nullptr;
    QLabel* m_summary = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    int m_done = 0;
    int m_ok = 0;
    int m_total = 0;
    bool m_finished = false;
};

} // namespace ex::ui

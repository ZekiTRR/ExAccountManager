#include "ui/accounts/cdp_login_dialog.h"

#include "core/roblox/cdp_login.h"
#include "core/util/log.h"
#include "ui/app_context.h"
#include "ui/theme.h"

#include <QHeaderView>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>

namespace ex::ui {

using ex::roblox::CdpLoginSession;
using ex::roblox::CdpLoginService;

namespace {
QString statusText(int status)
{
    switch (static_cast<CdpLoginSession::Status>(status)) {
    case CdpLoginSession::Status::StartingBrowser: return QStringLiteral("Запуск браузера...");
    case CdpLoginSession::Status::Connecting: return QStringLiteral("Подключение...");
    case CdpLoginSession::Status::FillingForm: return QStringLiteral("Заполнение формы...");
    case CdpLoginSession::Status::WaitingForUser: return QStringLiteral("Ожидание входа (капча/2FA)");
    case CdpLoginSession::Status::CookieCaptured: return QStringLiteral("Cookie получен!");
    case CdpLoginSession::Status::Failed: return QStringLiteral("Ошибка");
    case CdpLoginSession::Status::Cancelled: return QStringLiteral("Отменено");
    }
    return QStringLiteral("...");
}
} // namespace

CdpLoginDialog::CdpLoginDialog(AppContext& ctx, QWidget* parent)
    : QDialog(parent)
    , m_ctx(ctx)
{
    setWindowTitle(tr("Вход через браузер"));
    setModal(true);
    resize(560, 400);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 22, 24, 20);
    root->setSpacing(12);

    auto* title = new QLabel(tr("Вход через браузер"));
    title->setProperty("class", QStringLiteral("titleLarge"));
    root->addWidget(title);

    auto* hint = new QLabel(tr(
        "Открыты окна браузера с автозаполненным логином.\n"
        "При необходимости пройдите капчу/2FA и нажмите «Log In».\n"
        "Cookie .ROBLOSECURITY будет перехвачен автоматически."));
    hint->setProperty("class", QStringLiteral("dim"));
    root->addWidget(hint);

    m_table = new QTableWidget;
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({tr("Логин"), tr("Статус")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->hide();
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    root->addWidget(m_table, 1);

    m_summary = new QLabel;
    m_summary->setProperty("class", QStringLiteral("dim"));
    root->addWidget(m_summary);

    m_cancelBtn = new QPushButton(tr("Отменить оставшиеся"));
    m_cancelBtn->setProperty("variant", QStringLiteral("text"));
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        if (m_service)
            m_service->cancelAll();
    });
    root->addWidget(m_cancelBtn, 0, Qt::AlignRight);

    m_service = new CdpLoginService(this);
    connect(m_service, &CdpLoginService::sessionStatus,
            this, &CdpLoginDialog::onSessionStatus);
    connect(m_service, &CdpLoginService::loginSucceeded,
            this, &CdpLoginDialog::onLoginSucceeded);
    connect(m_service, &CdpLoginService::loginFailed, this,
            [this](const QString& username, const QString& error) {
        ++m_done;
        markRowError(username, error);
        updateSummary();
        updateCloseState();
    });
    connect(m_service, &CdpLoginService::allFinished, this, [this]() {
        // If nothing could be started (no browser) — close right away
        if (m_total == 0) {
            m_finished = true;
            accept();
        }
    });
}

void CdpLoginDialog::start(const QList<ex::roblox::CdpLoginRequest>& requests, int concurrency)
{
    m_total = requests.size();
    m_done = 0;
    m_ok = 0;
    m_table->setRowCount(requests.size());
    int row = 0;
    for (const auto& r : requests) {
        auto* nameItem = new QTableWidgetItem(r.username);
        m_table->setItem(row, 0, nameItem);
        m_table->setItem(row, 1, new QTableWidgetItem(tr("В очереди")));
        ++row;
    }
    updateSummary();
    m_service->loginBulk(requests, concurrency);
}

void CdpLoginDialog::onSessionStatus(const QString& username, int status, const QString& detail)
{
    for (int i = 0; i < m_table->rowCount(); ++i) {
        if (m_table->item(i, 0)->text() == username) {
            QString text = statusText(status);
            if (!detail.isEmpty() && status == static_cast<int>(CdpLoginSession::Status::Failed))
                text += QStringLiteral(" (%1)").arg(detail);
            m_table->item(i, 1)->setText(text);
            if (status == static_cast<int>(CdpLoginSession::Status::Failed))
                m_table->item(i, 1)->setForeground(theme::currentPalette().error);
            if (status == static_cast<int>(CdpLoginSession::Status::CookieCaptured))
                m_table->item(i, 1)->setForeground(theme::currentPalette().success);
            break;
        }
    }
}

void CdpLoginDialog::onLoginSucceeded(const QString& username, const QString& cookie,
                                      const QString& password, qint64 userId,
                                      const QString& realUsername,
                                      const QString& realDisplayName)
{
    ++m_ok;
    ++m_done;
    updateSummary();

    // Account data already came from the browser page — save it directly
    // without another HTTP validation (the service stores the password in the vault).
    // QPointer: the dialog may close before the save finishes.
    QPointer<CdpLoginDialog> self(this);
    m_ctx.accounts().addAccountValidated(cookie, userId, realUsername, realDisplayName,
                                         password,
        [self, username](bool ok, qint64 accountId, const QString& err) {
        if (!self)
            return;
        if (!ok) {
            EXLOG_WARN("cdp") << "login captured but save failed:" << err;
            self->markRowError(username, err);
        }
        Q_UNUSED(accountId);
    });
    updateCloseState();
}

void CdpLoginDialog::updateCloseState()
{
    if (m_total <= 0 || m_done < m_total || m_finished)
        return;
    m_finished = true;
    m_cancelBtn->setEnabled(true);
    m_cancelBtn->setText(tr("Закрыть"));
    m_cancelBtn->setProperty("variant", QStringLiteral("filled"));
    m_cancelBtn->style()->unpolish(m_cancelBtn);
    m_cancelBtn->style()->polish(m_cancelBtn);
    disconnect(m_cancelBtn, nullptr, nullptr, nullptr);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void CdpLoginDialog::markRowError(const QString& username, const QString& error)
{
    for (int i = 0; i < m_table->rowCount(); ++i) {
        if (m_table->item(i, 0)->text() == username) {
            m_table->item(i, 1)->setText(tr("Ошибка (%1)").arg(error));
            m_table->item(i, 1)->setForeground(theme::currentPalette().error);
            break;
        }
    }
}

void CdpLoginDialog::updateSummary()
{
    m_summary->setText(tr("Готово: %1 из %2 | Успешно: %3")
                           .arg(m_done).arg(m_total).arg(m_ok));
}

void CdpLoginDialog::reject()
{
    // Closing the dialog cancels the remaining sessions
    if (m_service)
        m_service->cancelAll();
    QDialog::reject();
}

} // namespace ex::ui

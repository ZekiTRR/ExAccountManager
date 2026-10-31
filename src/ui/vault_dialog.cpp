#include "ui/vault_dialog.h"

#include "ui/app_context.h"
#include "ui/material/icons.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace ex::ui {

VaultDialog::VaultDialog(AppContext& ctx, QWidget* parent)
    : QDialog(parent)
    , m_ctx(ctx)
{
    setWindowTitle(tr("Хранилище секретов"));
    setModal(true);
    setMinimumWidth(480);
    buildUi();
}

void VaultDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 22, 24, 20);
    root->setSpacing(12);

    // Header: lock glyph in a tint circle + name.
    auto* header = new QHBoxLayout;
    header->setSpacing(12);
    auto* lockIcon = new icons::IconLabel(QStringLiteral("lock"), 26);
    lockIcon->setProperty("class", QStringLiteral("iconPrimary"));
    auto* title = new QLabel(tr("Хранилище секретов"));
    title->setProperty("class", QStringLiteral("titleLarge"));
    header->addWidget(lockIcon, 0, Qt::AlignTop);
    header->addWidget(title, 1);
    root->addLayout(header);

    auto* intro = new QLabel(tr(
        "Cookie, пароли и токены аккаунтов хранятся в зашифрованном виде.\n"
        "Для работы с сохранёнными сессиями хранилище нужно разблокировать."));
    intro->setWordWrap(true);
    intro->setProperty("class", QStringLiteral("dim"));
    root->addWidget(intro);

    if (m_ctx.vaultNeedsPassword()) {
        m_password = new QLineEdit;
        m_password->setEchoMode(QLineEdit::Password);
        m_password->setPlaceholderText(tr("Мастер-пароль"));
        auto* toggle = new QPushButton;
        toggle->setProperty("class", QStringLiteral("iconBtn"));
        toggle->setCursor(Qt::PointingHandCursor);
        icons::setGlyph(toggle, QStringLiteral("visibility"));
        connect(toggle, &QPushButton::clicked, this, [this, toggle]() {
            const bool hidden =
                m_password->echoMode() == QLineEdit::Password;
            m_password->setEchoMode(hidden ? QLineEdit::Normal
                                           : QLineEdit::Password);
            icons::setGlyph(toggle, hidden
                                         ? QStringLiteral("visibility_off")
                                         : QStringLiteral("visibility"));
        });
        auto* passRow = new QHBoxLayout;
        passRow->setSpacing(4);
        passRow->addWidget(m_password, 1);
        passRow->addWidget(toggle, 0, Qt::AlignVCenter);
        root->addLayout(passRow);

        auto* buttons = new QHBoxLayout;
        m_unlockBtn = new QPushButton(tr("Разблокировать"));
        m_unlockBtn->setProperty("variant", QStringLiteral("filled"));
        auto* skip = new QPushButton(tr("Продолжить без секретов"));
        skip->setProperty("variant", QStringLiteral("text"));
        buttons->addStretch();
        buttons->addWidget(skip);
        buttons->addWidget(m_unlockBtn);
        root->addLayout(buttons);

        connect(m_unlockBtn, &QPushButton::clicked, this, &VaultDialog::tryUnlock);
        connect(m_password, &QLineEdit::returnPressed, this, &VaultDialog::tryUnlock);
        connect(skip, &QPushButton::clicked, this, &QDialog::reject);
    } else {
        auto* warning = new QLabel(tr(
            "Хранилище не удалось открыть (%1).\n\n"
            "Пересоздание удалит сохранённые cookie и пароли — аккаунты "
            "останутся в списке, но сессии придётся добавить заново.")
            .arg(m_ctx.vaultError()));
        warning->setWordWrap(true);
        warning->setProperty("class", QStringLiteral("warningText"));
        root->addWidget(warning);

        auto* buttons = new QHBoxLayout;
        m_resetBtn = new QPushButton(tr("Пересоздать хранилище"));
        m_resetBtn->setObjectName(QStringLiteral("danger"));
        auto* skip = new QPushButton(tr("Продолжить без секретов"));
        skip->setProperty("variant", QStringLiteral("text"));
        buttons->addStretch();
        buttons->addWidget(skip);
        buttons->addWidget(m_resetBtn);
        root->addLayout(buttons);

        connect(m_resetBtn, &QPushButton::clicked, this, &VaultDialog::tryReset);
        connect(skip, &QPushButton::clicked, this, &QDialog::reject);
    }

    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setProperty("class", QStringLiteral("errorText"));
    root->addWidget(m_status);
}

void VaultDialog::tryUnlock()
{
    if (!m_password || m_password->text().isEmpty()) {
        m_status->setText(tr("Введите мастер-пароль."));
        return;
    }
    bool ok = false;
    m_ctx.unlockWithPassword(m_password->text(), ok);
    if (ok) {
        accept();
        return;
    }
    m_status->setText(tr("Не удалось разблокировать: %1").arg(m_ctx.vaultError()));
}

void VaultDialog::tryReset()
{
    const auto answer = QMessageBox::warning(
        this, tr("Пересоздание хранилища"),
        tr("Все сохранённые cookie и пароли будут безвозвратно удалены.\n"
           "Аккаунты останутся в списке, но потребуют повторного входа.\n\n"
           "Продолжить?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    if (m_ctx.resetVault()) {
        accept();
        return;
    }
    m_status->setText(tr("Не удалось пересоздать хранилище: %1").arg(m_ctx.vaultError()));
}

bool VaultDialog::ensureUnlocked(AppContext& ctx, QWidget* parent)
{
    if (ctx.vaultUnlocked())
        return true;
    VaultDialog dialog(ctx, parent);
    dialog.exec();
    return ctx.vaultUnlocked();
}

} // namespace ex::ui

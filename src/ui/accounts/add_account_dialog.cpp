#include "ui/accounts/add_account_dialog.h"

#include "core/roblox/bulk_import.h"
#include "ui/material/controls.h"
#include "ui/material/field.h"
#include "ui/material/icons.h"

#include <QButtonGroup>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace ex::ui {
namespace {

constexpr int kDialogWidth = 600;
constexpr int kDialogHeight = 540;

void readFileIntoBulkEdit(QWidget* parent, QPlainTextEdit* bulkEdit)
{
    const QString path = QFileDialog::getOpenFileName(
        parent, QObject::tr("Импорт аккаунтов"), QString(),
        QObject::tr("Текстовые файлы (*.txt *.csv *.tsv);;Все файлы (*)"));
    if (path.isEmpty())
        return;
    const auto entries = ex::roblox::BulkImportParser::parseFile(path);
    QStringList lines;
    for (const auto& entry : entries) {
        lines.append(entry.cookie.isEmpty()
                         ? QStringLiteral("%1:%2").arg(entry.username,
                                                       entry.password)
                         : (entry.username.isEmpty()
                                ? entry.cookie
                                : QStringLiteral("%1:%2:%3")
                                      .arg(entry.username, entry.password,
                                           entry.cookie)));
    }
    bulkEdit->setPlainText(lines.join(QLatin1Char('\n')));
}

QWidget* noteRow(const QString& iconName, const QString& text, QWidget* parent)
{
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(7);
    auto* icon = new icons::IconLabel(iconName, 13, row);
    icon->setTone(QStringLiteral("muted"));
    layout->addWidget(icon, 0, Qt::AlignTop);
    auto* label = controls::label(text, QStringLiteral("pageSub"));
    label->setWordWrap(true);
    layout->addWidget(label, 1);
    return row;
}

QLabel* areaLabel(const QString& text, QWidget* parent)
{
    auto* label = controls::label(text, QStringLiteral("fieldLabel"));
    label->setParent(parent);
    return label;
}

} // namespace

AddAccountDialog::AddAccountDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Добавление аккаунтов"));
    setModal(true);
    resize(kDialogWidth, kDialogHeight);
    setMinimumSize(560, 500);
    buildUi();
}

void AddAccountDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(22, 18, 22, 18);
    root->setSpacing(12);

    auto* header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(11);
    auto* iconTile = new QFrame(this);
    iconTile->setProperty("class", QStringLiteral("iconTile"));
    iconTile->setFixedSize(34, 34);
    auto* iconLayout = new QVBoxLayout(iconTile);
    iconLayout->setContentsMargins(0, 0, 0, 0);
    auto* icon = new icons::IconLabel(QStringLiteral("user-plus"), 17, iconTile);
    icon->setTone(QStringLiteral("accent"));
    iconLayout->addWidget(icon, 0, Qt::AlignCenter);
    header->addWidget(iconTile, 0, Qt::AlignTop);

    auto* copy = new QVBoxLayout;
    copy->setContentsMargins(0, 0, 0, 0);
    copy->setSpacing(2);
    copy->addWidget(controls::label(tr("Добавить аккаунты"),
                                    QStringLiteral("titleMedium")));
    copy->addWidget(controls::label(
        tr("Cookie, массовый импорт или вход по логину и паролю."),
        QStringLiteral("pageSub")));
    header->addLayout(copy, 1);
    root->addLayout(header);
    root->addSpacing(2);

    auto* tabsRow = new QHBoxLayout;
    tabsRow->setContentsMargins(0, 0, 0, 0);
    tabsRow->setSpacing(2);
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    const QStringList tabNames = {tr("Cookie"), tr("Массовый импорт"),
                                  tr("Логин и пароль")};
    for (int i = 0; i < tabNames.size(); ++i) {
        auto* tab = new QPushButton(tabNames.at(i), this);
        tab->setProperty("class", QStringLiteral("dialogTab"));
        tab->setCheckable(true);
        tab->setCursor(Qt::PointingHandCursor);
        tab->setFixedHeight(36);
        group->addButton(tab, i);
        m_tabs.append(tab);
        tabsRow->addWidget(tab);
        connect(tab, &QPushButton::clicked, this, [this, i]() { setTab(i); });
    }
    tabsRow->addStretch(1);
    root->addLayout(tabsRow);
    root->addWidget(controls::separator(this));

    m_pages = new QStackedWidget(this);
    root->addWidget(m_pages, 1);

    QFont mono(QStringLiteral("Cascadia Mono"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPixelSize(11);

    // ---- cookie
    auto* cookiePage = new QWidget(m_pages);
    auto* cookieLayout = new QVBoxLayout(cookiePage);
    cookieLayout->setContentsMargins(0, 8, 0, 0);
    cookieLayout->setSpacing(7);
    auto* cookieLabel = controls::label(
        tr("Вставьте <span style=\"font-family:'Cascadia Mono','Consolas',monospace\">"
           ".ROBLOSECURITY</span>"),
        QStringLiteral("fieldLabel"));
    cookieLabel->setTextFormat(Qt::RichText);
    cookieLayout->addWidget(cookieLabel);
    m_cookieEdit = new QPlainTextEdit(cookiePage);
    m_cookieEdit->setPlaceholderText(
        QStringLiteral("_|WARNING:-DO-NOT-SHARE-THIS...|<токен>"));
    m_cookieEdit->setMinimumHeight(170);
    m_cookieEdit->setFont(mono);
    cookieLayout->addWidget(m_cookieEdit, 1);
    cookieLayout->addWidget(noteRow(
        QStringLiteral("shield"),
        tr("Cookie шифруется AES-256-GCM и остаётся на этом устройстве."),
        cookiePage));
    m_pages->addWidget(cookiePage);

    // ---- bulk
    auto* bulkPage = new QWidget(m_pages);
    auto* bulkLayout = new QVBoxLayout(bulkPage);
    bulkLayout->setContentsMargins(0, 8, 0, 0);
    bulkLayout->setSpacing(7);
    bulkLayout->addWidget(areaLabel(tr("Список аккаунтов"), bulkPage));
    m_bulkEdit = new QPlainTextEdit(bulkPage);
    m_bulkEdit->setPlaceholderText(
        QStringLiteral("user:password\nuser:password:cookie\n"
                       "_|WARNING:-DO-NOT-SHARE-THIS...|..."));
    m_bulkEdit->setMinimumHeight(150);
    m_bulkEdit->setFont(mono);
    bulkLayout->addWidget(m_bulkEdit, 1);
    auto* fileRow = new QHBoxLayout;
    fileRow->setContentsMargins(0, 0, 0, 0);
    fileRow->setSpacing(8);
    auto* fileBtn = controls::button(tr("Из файла"), QString(), bulkPage);
    icons::setGlyph(fileBtn, QStringLiteral("file"));
    connect(fileBtn, &QPushButton::clicked, this, [this]() {
        readFileIntoBulkEdit(this, m_bulkEdit);
        clearError();
    });
    fileRow->addWidget(fileBtn);
    auto* bulkHint = controls::label(
        tr("user:pass · user:pass:cookie · cookie. Комментарии # и //."),
        QStringLiteral("pageSub"));
    bulkHint->setWordWrap(true);
    fileRow->addWidget(bulkHint, 1);
    bulkLayout->addLayout(fileRow);
    m_pages->addWidget(bulkPage);

    // ---- login
    auto* loginPage = new QWidget(m_pages);
    auto* loginLayout = new QVBoxLayout(loginPage);
    loginLayout->setContentsMargins(0, 10, 0, 0);
    loginLayout->setSpacing(8);
    loginLayout->addWidget(areaLabel(tr("Логин Roblox"), loginPage));
    auto* userField = new Field(QStringLiteral("accounts"), loginPage);
    userField->setPlaceholder(tr("username"));
    m_userEdit = userField->lineEdit();
    m_userEdit->setAccessibleName(tr("Логин Roblox"));
    loginLayout->addWidget(userField);
    loginLayout->addSpacing(2);
    loginLayout->addWidget(areaLabel(tr("Пароль"), loginPage));
    auto* passField = new Field(QStringLiteral("lock"), loginPage);
    passField->setPlaceholder(QStringLiteral("••••••••"));
    m_passEdit = passField->lineEdit();
    m_passEdit->setEchoMode(QLineEdit::Password);
    m_passEdit->setAccessibleName(tr("Пароль"));
    loginLayout->addWidget(passField);
    loginLayout->addSpacing(6);
    loginLayout->addWidget(noteRow(
        QStringLiteral("info"),
        tr("Откроется отдельный браузер с автозаполнением. "
           "После входа cookie сохранится автоматически."),
        loginPage));
    loginLayout->addStretch(1);
    m_pages->addWidget(loginPage);

    m_error = controls::label(QString(), QStringLiteral("fieldError"));
    m_error->setWordWrap(true);
    m_error->hide();
    root->addWidget(m_error);

    root->addWidget(controls::separator(this));
    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(0, 0, 0, 0);
    footer->setSpacing(8);
    footer->addWidget(controls::label(tr("Секреты не покидают устройство."),
                                      QStringLiteral("pageSub")), 1);
    auto* cancel = controls::button(tr("Отмена"), QStringLiteral("text"), this);
    auto* add = controls::button(tr("Добавить"), QStringLiteral("filled"), this);
    add->setMinimumWidth(102);
    add->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(add, &QPushButton::clicked, this, [this]() { tryAccept(); });
    footer->addWidget(cancel);
    footer->addWidget(add);
    root->addLayout(footer);

    connect(m_cookieEdit, &QPlainTextEdit::textChanged, this, &AddAccountDialog::clearError);
    connect(m_bulkEdit, &QPlainTextEdit::textChanged, this, &AddAccountDialog::clearError);
    connect(m_userEdit, &QLineEdit::textChanged, this, &AddAccountDialog::clearError);
    connect(m_passEdit, &QLineEdit::textChanged, this, &AddAccountDialog::clearError);

    setTab(0);
}

void AddAccountDialog::setMode(Mode mode)
{
    setTab(static_cast<int>(mode));
}

void AddAccountDialog::setTab(int index)
{
    const int bounded = qBound(0, index, m_tabs.size() - 1);
    m_pages->setCurrentIndex(bounded);
    for (int i = 0; i < m_tabs.size(); ++i)
        m_tabs.at(i)->setChecked(i == bounded);
    clearError();
}

void AddAccountDialog::tryAccept()
{
    if (mode() == Mode::Cookie && m_cookieEdit->toPlainText().trimmed().isEmpty()) {
        showError(tr("Вставьте cookie полностью, вместе с предупреждением."));
        m_cookieEdit->setFocus(Qt::OtherFocusReason);
        return;
    }
    if (mode() == Mode::Bulk && m_bulkEdit->toPlainText().trimmed().isEmpty()) {
        showError(tr("Добавьте хотя бы одну строку или загрузите файл."));
        m_bulkEdit->setFocus(Qt::OtherFocusReason);
        return;
    }
    if (mode() == Mode::LoginPass
        && (m_userEdit->text().trimmed().isEmpty() || m_passEdit->text().isEmpty())) {
        showError(tr("Введите логин и пароль."));
        (m_userEdit->text().trimmed().isEmpty() ? m_userEdit : m_passEdit)
            ->setFocus(Qt::OtherFocusReason);
        return;
    }
    accept();
}

void AddAccountDialog::showError(const QString& text)
{
    m_error->setText(text);
    m_error->show();
}

void AddAccountDialog::clearError()
{
    if (m_error) {
        m_error->clear();
        m_error->hide();
    }
}

AddAccountDialog::Mode AddAccountDialog::mode() const
{
    return static_cast<Mode>(m_pages->currentIndex());
}

QString AddAccountDialog::cookieText() const
{
    return m_cookieEdit->toPlainText().trimmed();
}

QString AddAccountDialog::bulkText() const
{
    return m_bulkEdit->toPlainText();
}

QString AddAccountDialog::username() const
{
    return m_userEdit->text().trimmed();
}

QString AddAccountDialog::password() const
{
    return m_passEdit->text();
}

} // namespace ex::ui

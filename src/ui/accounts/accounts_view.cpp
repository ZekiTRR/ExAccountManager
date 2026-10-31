#include "ui/accounts/accounts_view.h"

#include "core/launcher/launch_uri.h"
#include "core/models/domain.h"
#include "core/roblox/bulk_import.h"
#include "core/roblox/cdp_login.h"
#include "ui/accounts/account_delegate.h"
#include "ui/accounts/add_account_dialog.h"
#include "ui/accounts/cdp_login_dialog.h"
#include "ui/app_context.h"
#include "ui/material/controls.h"
#include "ui/material/field.h"
#include "ui/material/icons.h"
#include "ui/material/segmented.h"
#include "ui/theme.h"

#include <QCheckBox>
#include <QCursor>
#include <QDate>
#include <QDateTime>
#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace ex::ui {
namespace {

QString accountsWord(int count)
{
    const int mod10 = count % 10;
    const int mod100 = count % 100;
    if (mod10 == 1 && mod100 != 11)
        return QObject::tr("аккаунт");
    if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14))
        return QObject::tr("аккаунта");
    return QObject::tr("аккаунтов");
}

QString lastUsedText(const QDateTime& time)
{
    if (!time.isValid())
        return {};
    const qint64 secs = time.secsTo(QDateTime::currentDateTime());
    if (secs < 0 || secs < 60)
        return QObject::tr("только что");
    if (secs < 3600)
        return QObject::tr("%1 мин назад").arg(secs / 60);
    if (secs < 86400)
        return QObject::tr("%1 ч назад").arg(secs / 3600);
    if (secs < 172800)
        return QObject::tr("вчера");
    if (secs < 604800)
        return QObject::tr("%1 дн назад").arg(secs / 86400);
    return time.toString(QStringLiteral("dd.MM"));
}

QString presenceLabel(ex::model::PresenceType type)
{
    switch (type) {
    case ex::model::PresenceType::InGame: return QObject::tr("В игре");
    case ex::model::PresenceType::Online: return QObject::tr("В сети");
    case ex::model::PresenceType::InStudio: return QObject::tr("В Studio");
    default: return {};
    }
}

QLabel* hintLabel(const QString& text)
{
    auto* label = controls::label(text, QStringLiteral("fieldHint"));
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    label->setMinimumWidth(0);
    return label;
}

} // namespace

AccountsView::AccountsView(AppContext& ctx, QWidget* parent)
    : QWidget(parent)
    , m_ctx(ctx)
{
    buildUi();

    connect(&ctx, &AppContext::accountsChanged, this, &AccountsView::reload);
    connect(&ctx.accounts(), &ex::roblox::AccountService::presenceUpdated,
            this, [this]() { populateTree(); });
    connect(&ctx.accounts(), &ex::roblox::AccountService::avatarsUpdated,
            this, [this]() { populateTree(); });

    connect(&ctx.launcher(), &ex::launcher::LaunchService::launchFinished,
            this, [this](const ex::launcher::LaunchResult& r) {
        if (r.ok) {
            setStatus(tr("Запуск отправлен в Roblox"), QStringLiteral("success"));
        } else {
            const auto acc = m_ctx.store().accountById(r.accountId);
            const QString name = acc ? acc->aliasOrUsername()
                                     : tr("аккаунт #%1").arg(r.accountId);
            m_lastLaunchError = tr("%1: %2").arg(name, launchErrorText(r.error));
            setStatus(tr("Ошибка запуска · %1").arg(m_lastLaunchError),
                      QStringLiteral("error"));
        }
        reload();
    });
    connect(&ctx.launcher(), &ex::launcher::LaunchService::batchFinished,
            this, [this](int ok, int fail) {
        setLaunching(false);
        if (fail > 0) {
            QString text = tr("Запуск: успешно %1, ошибок %2").arg(ok).arg(fail);
            if (!m_lastLaunchError.isEmpty())
                text += tr(" · %1").arg(m_lastLaunchError);
            setStatus(text, QStringLiteral("error"));
        } else {
            setStatus(tr("Готово · запущено %1 %2").arg(ok).arg(accountsWord(ok)),
                      QStringLiteral("success"));
        }
    });

    auto* presenceTimer = new QTimer(this);
    presenceTimer->setInterval(60000);
    connect(presenceTimer, &QTimer::timeout, this, [this]() {
        if (m_ctx.settings().get(QStringLiteral("presence.enabled")).toBool())
            m_ctx.accounts().refreshPresence({});
    });
    presenceTimer->start();

    reload();
    if (m_ctx.vaultUnlocked()) {
        m_ctx.accounts().refreshPresence({});
        m_ctx.accounts().refreshAvatars({});
    }
}

void AccountsView::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(16);

    buildHeader(root);

    auto* content = new QHBoxLayout;
    content->setContentsMargins(0, 0, 0, 0);
    content->setSpacing(16);

    // ---------------- карточка списка
    auto* listCard = controls::card(this);
    auto* listLayout = new QVBoxLayout(listCard);
    listLayout->setContentsMargins(6, 6, 6, 8);
    listLayout->setSpacing(0);

    auto* listTop = new QHBoxLayout;
    listTop->setContentsMargins(8, 0, 5, 0);
    listTop->setSpacing(4);
    auto* listTitle = controls::label(tr("Все аккаунты"), QStringLiteral("dim"));
    QFont listFont = listTitle->font();
    listFont.setPixelSize(12);
    listFont.setWeight(QFont::Medium);
    listTitle->setFont(listFont);
    m_listCount = controls::label(QStringLiteral("0"), QStringLiteral("tertiary"));
    listTop->addWidget(listTitle);
    listTop->addWidget(m_listCount);
    listTop->addStretch(1);

    auto* refreshBtn = controls::iconButton(
        QStringLiteral("refresh"), tr("Обновить статусы"), listCard);
    connect(refreshBtn, &QPushButton::clicked, this, &AccountsView::onRefresh);
    auto* importBtn = controls::iconButton(
        QStringLiteral("upload"), tr("Импорт из файла"), listCard);
    connect(importBtn, &QPushButton::clicked, this, &AccountsView::onImport);

    auto* groupsBtn = controls::iconButton(
        QStringLiteral("folder"), tr("Группы"), listCard);
    m_groupsMenu = new QMenu(this);
    m_groupsMenu->addAction(tr("Создать группу"), this, &AccountsView::onAddGroup);
    m_groupsMenu->addAction(tr("Переименовать группу"), this,
                            &AccountsView::onRenameGroup);
    m_groupsMenu->addSeparator();
    m_groupsMenu->addAction(tr("Удалить группу"), this, &AccountsView::onDeleteGroup);
    connect(groupsBtn, &QPushButton::clicked, this, [this, groupsBtn]() {
        m_groupsMenu->exec(groupsBtn->mapToGlobal(
            QPoint(0, groupsBtn->height() + 6)));
    });

    auto* sep = new QFrame(listCard);
    sep->setProperty("class", QStringLiteral("separator"));
    sep->setFixedSize(1, 16);

    m_deleteBtn = controls::iconButton(
        QStringLiteral("trash"), tr("Удалить выбранные"), listCard);
    m_deleteBtn->setProperty("danger", true);
    connect(m_deleteBtn, &QPushButton::clicked, this,
            &AccountsView::onDeleteSelected);

    listTop->addWidget(refreshBtn);
    listTop->addWidget(importBtn);
    listTop->addWidget(groupsBtn);
    listTop->addSpacing(5);
    listTop->addWidget(sep);
    listTop->addSpacing(5);
    listTop->addWidget(m_deleteBtn);
    listLayout->addLayout(listTop);
    listLayout->addSpacing(4);

    m_tree = new QTreeWidget(listCard);
    m_tree->setObjectName(QStringLiteral("accountTree"));
    m_tree->setColumnCount(1);
    m_tree->header()->hide();
    m_tree->setRootIsDecorated(false);
    m_tree->setIndentation(0);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setMouseTracking(true);
    m_tree->viewport()->setMouseTracking(true);
    m_tree->viewport()->setAttribute(Qt::WA_Hover);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setExpandsOnDoubleClick(false);
    m_tree->setAnimated(true);
    m_tree->setUniformRowHeights(false);

    m_delegate = new AccountDelegate(m_avatars, m_tree);
    m_tree->setItemDelegate(m_delegate);
    connect(m_delegate, &AccountDelegate::avatarsUpdated, m_tree,
            [this]() { m_tree->viewport()->update(); });
    connect(m_delegate, &AccountDelegate::favoriteRequested, this,
            &AccountsView::onFavoriteToggled);
    connect(m_delegate, &AccountDelegate::quickLaunchRequested, this,
            &AccountsView::onQuickLaunch);
    connect(m_delegate, &AccountDelegate::groupToggleRequested, this,
            &AccountsView::onGroupToggle);

    connect(m_tree, &QTreeWidget::itemClicked, this, &AccountsView::onItemClicked);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            &AccountsView::onItemDoubleClicked);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this,
            &AccountsView::onTreeContextMenu);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this]() {
        if (!m_populating)
            updateSelectionUi();
    });

    m_emptyPage = new QWidget(listCard);
    auto* emptyLayout = new QVBoxLayout(m_emptyPage);
    emptyLayout->setContentsMargins(20, 24, 20, 24);
    emptyLayout->setSpacing(8);
    emptyLayout->addStretch(1);
    auto* emptyTile = new QFrame(m_emptyPage);
    emptyTile->setProperty("class", QStringLiteral("innerBox"));
    emptyTile->setFixedSize(46, 46);
    auto* emptyTileLayout = new QVBoxLayout(emptyTile);
    emptyTileLayout->setContentsMargins(0, 0, 0, 0);
    m_emptyIcon = new icons::IconLabel(QStringLiteral("search"), 20, emptyTile);
    m_emptyIcon->setTone(QStringLiteral("muted"));
    emptyTileLayout->addWidget(m_emptyIcon, 0, Qt::AlignCenter);
    emptyLayout->addWidget(emptyTile, 0, Qt::AlignHCenter);

    m_emptyTitle = controls::label(tr("Ничего не найдено"),
                                   QStringLiteral("titleSmall"));
    m_emptyTitle->setAlignment(Qt::AlignHCenter);
    emptyLayout->addWidget(m_emptyTitle);

    m_emptySub = controls::label(
        tr("Попробуйте изменить запрос или сбросить фильтр."),
        QStringLiteral("pageSub"));
    m_emptySub->setAlignment(Qt::AlignHCenter);
    m_emptySub->setWordWrap(true);
    m_emptySub->setMaximumWidth(280);
    emptyLayout->addWidget(m_emptySub, 0, Qt::AlignHCenter);

    m_emptyReset = controls::button(tr("Сбросить поиск"),
                                    QStringLiteral("text"), m_emptyPage);
    m_emptyReset->setCursor(Qt::PointingHandCursor);
    connect(m_emptyReset, &QPushButton::clicked, this, [this]() {
        m_searchEdit->clear();
    });
    emptyLayout->addWidget(m_emptyReset, 0, Qt::AlignHCenter);
    emptyLayout->addStretch(1);

    m_listStack = new QStackedWidget(listCard);
    m_listStack->addWidget(m_tree);
    m_listStack->addWidget(m_emptyPage);
    m_listStack->setCurrentIndex(0);
    listLayout->addWidget(m_listStack, 1);

    content->addWidget(listCard, 1);

    // ---------------- панель запуска
    auto* panel = controls::card(this);
    panel->setFixedWidth(330);
    auto* launchLayout = new QVBoxLayout(panel);
    launchLayout->setContentsMargins(16, 15, 16, 14);
    launchLayout->setSpacing(0);

    auto* launchHead = new QHBoxLayout;
    launchHead->setSpacing(9);
    auto* mark = new QFrame(panel);
    mark->setProperty("class", QStringLiteral("launchMark"));
    mark->setFixedSize(28, 28);
    auto* markLayout = new QVBoxLayout(mark);
    markLayout->setContentsMargins(0, 0, 0, 0);
    auto* markIcon = new icons::IconLabel(QStringLiteral("play-filled"), 15, mark);
    markIcon->setTone(QStringLiteral("accent"));
    markLayout->addWidget(markIcon, 0, Qt::AlignCenter);
    launchHead->addWidget(mark);
    launchHead->addWidget(controls::label(tr("Запуск"),
                                          QStringLiteral("titleSmall")));
    launchHead->addStretch(1);
    launchHead->addWidget(controls::label(tr("Быстрый старт"),
                                          QStringLiteral("chip")));
    launchLayout->addLayout(launchHead);
    launchLayout->addSpacing(12);

    m_selectionBox = new QFrame(panel);
    m_selectionBox->setProperty("class", QStringLiteral("innerBox"));
    m_selectionBox->setFixedHeight(54);
    m_selectionLayout = new QHBoxLayout(m_selectionBox);
    m_selectionLayout->setContentsMargins(9, 8, 9, 8);
    m_selectionLayout->setSpacing(8);
    launchLayout->addWidget(m_selectionBox);
    launchLayout->addSpacing(13);

    auto* placeLabelRow = new QHBoxLayout;
    placeLabelRow->setSpacing(6);
    placeLabelRow->addWidget(controls::label(tr("Place ID или ссылка"),
                                             QStringLiteral("fieldLabel")));
    placeLabelRow->addStretch(1);
    m_placeTag = controls::label(QStringLiteral("—"),
                                 QStringLiteral("fieldTag"));
    placeLabelRow->addWidget(m_placeTag);
    launchLayout->addLayout(placeLabelRow);
    launchLayout->addSpacing(6);

    m_placeField = new Field(QStringLiteral("gamepad"), panel);
    m_placeField->setPlaceholder(tr("Например, 2809202155"));
    connect(m_placeField->lineEdit(), &QLineEdit::textChanged, this,
            [this]() { updatePlaceHint(); });
    launchLayout->addWidget(m_placeField);
    launchLayout->addSpacing(6);

    auto* placeHintRow = new QHBoxLayout;
    placeHintRow->setContentsMargins(2, 0, 0, 0);
    placeHintRow->setSpacing(5);
    m_placeHintIcon = new icons::IconLabel(QStringLiteral("info"), 13, panel);
    m_placeHintIcon->setTone(QStringLiteral("muted"));
    m_placeHintText = hintLabel(tr("ID игры или ссылка на Roblox"));
    placeHintRow->addWidget(m_placeHintIcon, 0, Qt::AlignVCenter);
    placeHintRow->addWidget(m_placeHintText, 1);
    launchLayout->addLayout(placeHintRow);
    launchLayout->addSpacing(12);

    launchLayout->addWidget(controls::label(tr("Режим запуска"),
                                            QStringLiteral("fieldLabel")));
    launchLayout->addSpacing(6);
    m_modeSeg = new Segmented(panel);
    m_modeSeg->addItem(tr("Один сервер"),
                       static_cast<int>(ex::launcher::BatchMode::SameServer));
    m_modeSeg->addItem(tr("Разные"),
                       static_cast<int>(ex::launcher::BatchMode::DifferentServers));
    m_modeSeg->addItem(tr("VIP-сервер"),
                       static_cast<int>(ex::launcher::BatchMode::PrivateServer));
    m_modeSeg->setFixedHeight(44);
    m_modeSeg->setToolTip(tr("Один сервер / разные сервера / VIP-сервер"));
    connect(m_modeSeg, &Segmented::currentIndexChanged, this,
            [this](int) { updateTargetFields(); });
    launchLayout->addWidget(m_modeSeg);
    launchLayout->addSpacing(12);

    m_jobGroup = new QWidget(panel);
    auto* jobLayout = new QVBoxLayout(m_jobGroup);
    jobLayout->setContentsMargins(0, 0, 0, 0);
    jobLayout->setSpacing(6);
    auto* jobLabelRow = new QHBoxLayout;
    jobLabelRow->setSpacing(6);
    jobLabelRow->addWidget(controls::label(tr("Job ID"),
                                           QStringLiteral("fieldLabel")));
    jobLabelRow->addWidget(controls::label(
        tr("необязательно"), QStringLiteral("tertiary")));
    jobLabelRow->addStretch(1);
    jobLayout->addLayout(jobLabelRow);
    m_jobField = new Field(QStringLiteral("terminal"), m_jobGroup);
    m_jobField->setPlaceholder(tr("Авто — ближайший сервер"));
    jobLayout->addWidget(m_jobField);
    launchLayout->addWidget(m_jobGroup);
    launchLayout->addSpacing(12);

    m_vipGroup = new QWidget(panel);
    auto* vipLayout = new QVBoxLayout(m_vipGroup);
    vipLayout->setContentsMargins(0, 0, 0, 0);
    vipLayout->setSpacing(6);
    vipLayout->addWidget(controls::label(tr("Ссылка на VIP-сервер"),
                                         QStringLiteral("fieldLabel")));
    m_vipField = new Field(QStringLiteral("link"), m_vipGroup);
    m_vipField->setPlaceholder(
        tr("https://www.roblox.com/share?code=…"));
    m_vipField->lineEdit()->setToolTip(tr(
        "Поддерживаются:\n"
        "https://www.roblox.com/share?code=...&type=Server\n"
        "https://www.roblox.com/games/ID/Name?privateServerLinkCode=...\n"
        "или только код privateServerLinkCode (ID игры — из поля Place)"));
    connect(m_vipField->lineEdit(), &QLineEdit::textChanged, this, [this]() {
        m_vipField->setInvalid(false);
        setFieldHint(m_vipHintIcon, m_vipHintText, QStringLiteral("info"),
                     tr("share-ссылка или privateServerLinkCode"),
                     QStringLiteral("muted"));
    });
    vipLayout->addWidget(m_vipField);
    auto* vipHintRow = new QHBoxLayout;
    vipHintRow->setContentsMargins(2, 0, 0, 0);
    vipHintRow->setSpacing(5);
    m_vipHintIcon = new icons::IconLabel(QStringLiteral("info"), 13, m_vipGroup);
    m_vipHintIcon->setTone(QStringLiteral("muted"));
    m_vipHintText = hintLabel(tr("share-ссылка или privateServerLinkCode"));
    vipHintRow->addWidget(m_vipHintIcon, 0, Qt::AlignVCenter);
    vipHintRow->addWidget(m_vipHintText, 1);
    vipLayout->addLayout(vipHintRow);
    m_vipGroup->hide();
    launchLayout->addWidget(m_vipGroup);
    launchLayout->addSpacing(12);

    auto* windowRow = new QHBoxLayout;
    windowRow->setContentsMargins(0, 0, 0, 0);
    windowRow->setSpacing(6);
    m_windowSizeCheck = new QCheckBox(tr("Размер окна"), panel);
    m_windowSizeCheck->setToolTip(tr(
        "Задать размер окна Roblox при запуске, например 100 × 100.\n"
        "Если выключено — Roblox откроется со своим размером."));
    QFont windowCheckFont = m_windowSizeCheck->font();
    windowCheckFont.setPixelSize(11);
    m_windowSizeCheck->setFont(windowCheckFont);
    windowRow->addWidget(m_windowSizeCheck);
    windowRow->addStretch(1);

    m_windowWidth = new controls::NumberSpinBox(panel);
    m_windowWidth->setRange(100, 7680);
    m_windowWidth->setSingleStep(10);
    m_windowWidth->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_windowWidth->setAlignment(Qt::AlignRight);
    m_windowWidth->setFixedWidth(66);
    m_windowWidth->setToolTip(tr("Ширина окна, px"));
    m_windowWidth->setAccessibleName(tr("Ширина окна"));

    m_windowHeight = new controls::NumberSpinBox(panel);
    m_windowHeight->setRange(100, 4320);
    m_windowHeight->setSingleStep(10);
    m_windowHeight->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_windowHeight->setAlignment(Qt::AlignRight);
    m_windowHeight->setFixedWidth(66);
    m_windowHeight->setToolTip(tr("Высота окна, px"));
    m_windowHeight->setAccessibleName(tr("Высота окна"));

    windowRow->addWidget(m_windowWidth);
    windowRow->addWidget(controls::label(QStringLiteral("×"),
                                         QStringLiteral("pageSub")));
    windowRow->addWidget(m_windowHeight);
    launchLayout->addLayout(windowRow);
    launchLayout->addSpacing(10);

    const bool customWindow =
        m_ctx.settings().get(QStringLiteral("launcher.windowSizeEnabled")).toBool();
    const int windowWidth =
        m_ctx.settings().get(QStringLiteral("launcher.windowWidth"), 800).toInt();
    const int windowHeight =
        m_ctx.settings().get(QStringLiteral("launcher.windowHeight"), 600).toInt();
    m_windowSizeCheck->setChecked(customWindow);
    m_windowWidth->setValue(qBound(100, windowWidth, 7680));
    m_windowHeight->setValue(qBound(100, windowHeight, 4320));
    m_windowWidth->setEnabled(customWindow);
    m_windowHeight->setEnabled(customWindow);
    connect(m_windowSizeCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_windowWidth->setEnabled(on);
        m_windowHeight->setEnabled(on);
    });

    auto* settingsRow = new QHBoxLayout;
    settingsRow->setContentsMargins(0, 0, 0, 0);
    settingsRow->setSpacing(4);
    auto* settingsIcon = new icons::IconLabel(QStringLiteral("settings"), 13, panel);
    settingsIcon->setTone(QStringLiteral("muted"));
    m_launchSettingsBtn = new QPushButton(panel);
    m_launchSettingsBtn->setProperty("class", QStringLiteral("linkBtn"));
    m_launchSettingsBtn->setCursor(Qt::PointingHandCursor);
    connect(m_launchSettingsBtn, &QPushButton::clicked, this,
            &AccountsView::settingsRequested);
    auto* chevron = new icons::IconLabel(QStringLiteral("chevron-right"), 12, panel);
    chevron->setTone(QStringLiteral("muted"));
    settingsRow->addWidget(settingsIcon);
    settingsRow->addWidget(m_launchSettingsBtn);
    settingsRow->addWidget(chevron);
    settingsRow->addStretch(1);
    launchLayout->addLayout(settingsRow);
    launchLayout->addSpacing(12);

    m_launchBtn = controls::button(QString(), QStringLiteral("filled"), panel);
    m_launchBtn->setFixedHeight(44);
    m_launchBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    icons::setGlyph(m_launchBtn, QStringLiteral("play-filled"),
                    QStringLiteral("onAccent"));
    connect(m_launchBtn, &QPushButton::clicked, this, &AccountsView::onLaunch);
    launchLayout->addWidget(m_launchBtn);
    launchLayout->addSpacing(10);

    auto* statusRow = new QHBoxLayout;
    statusRow->setContentsMargins(2, 0, 0, 0);
    statusRow->setSpacing(7);
    m_statusDot = new controls::StatusDot(6, panel);
    m_statusLabel = controls::label(tr("Готов к запуску"),
                                    QStringLiteral("statusText"));
    statusRow->addWidget(m_statusDot, 0, Qt::AlignVCenter);
    statusRow->addWidget(m_statusLabel, 1);
    launchLayout->addLayout(statusRow);

    content->addWidget(panel, 0, Qt::AlignTop);
    root->addLayout(content, 1);

    updateTargetFields();
    updateLaunchSettingsHint();
    updateSelectionBox({});
    updateHeaderStats();
    refreshListState();
    refreshLaunchButton();
}

void AccountsView::buildHeader(QVBoxLayout* root)
{
    auto* header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(16);

    auto* copy = new QVBoxLayout;
    copy->setContentsMargins(0, 0, 0, 0);
    copy->setSpacing(5);
    copy->addWidget(controls::label(tr("Аккаунты"),
                                    QStringLiteral("pageTitle")));

    auto* subRow = new QHBoxLayout;
    subRow->setContentsMargins(0, 0, 0, 0);
    subRow->setSpacing(7);
    m_pageSubtitle = controls::label(QString(), QStringLiteral("pageSub"));
    subRow->addWidget(m_pageSubtitle);
    auto* dot = new controls::StatusDot(6);
    subRow->addWidget(dot, 0, Qt::AlignVCenter);
    subRow->addWidget(controls::label(tr("обновлено только что"),
                                      QStringLiteral("pageSub")));
    subRow->addStretch(1);
    copy->addLayout(subRow);

    header->addLayout(copy, 1);

    auto* actions = new QHBoxLayout;
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(8);

    m_searchField = new Field(QStringLiteral("search"), this);
    m_searchField->setFixedWidth(236);
    m_searchField->setPlaceholder(tr("Поиск аккаунтов"));
    m_searchEdit = m_searchField->lineEdit();
    m_searchField->setTrailingIcon(QStringLiteral("close"),
                                   tr("Очистить поиск"));
    m_searchField->trailingButton()->hide();
    connect(m_searchEdit, &QLineEdit::textChanged, this,
            &AccountsView::onSearchChanged);
    connect(m_searchField, &Field::trailingClicked, this, [this]() {
        m_searchEdit->clear();
        m_searchEdit->setFocus(Qt::MouseFocusReason);
    });
    actions->addWidget(m_searchField);

    auto* addBtn = controls::button(tr("Добавить"),
                                    QStringLiteral("filled"), this);
    addBtn->setFixedHeight(40);
    addBtn->setMinimumWidth(118);
    icons::setGlyph(addBtn, QStringLiteral("plus"), QStringLiteral("onAccent"));
    connect(addBtn, &QPushButton::clicked, this, &AccountsView::onAddAccount);
    actions->addWidget(addBtn);

    header->addLayout(actions, 0);
    root->addLayout(header);
}

void AccountsView::updateTargetFields()
{
    if (!m_modeSeg || !m_jobGroup || !m_vipGroup)
        return;
    const auto mode = static_cast<ex::launcher::BatchMode>(
        m_modeSeg->currentData().toInt());
    const bool vip = mode == ex::launcher::BatchMode::PrivateServer;
    m_jobGroup->setVisible(!vip);
    m_vipGroup->setVisible(vip);
    m_vipField->setInvalid(false);
    setFieldHint(m_vipHintIcon, m_vipHintText, QStringLiteral("info"),
                 tr("share-ссылка или privateServerLinkCode"),
                 QStringLiteral("muted"));
}

void AccountsView::updateLaunchSettingsHint()
{
    if (!m_launchSettingsBtn)
        return;
    const int concurrency =
        m_ctx.settings().get(QStringLiteral("launcher.concurrency")).toInt();
    const int delay =
        m_ctx.settings().get(QStringLiteral("launcher.delaySeconds")).toInt();
    m_launchSettingsBtn->setText(
        delay > 0
            ? tr("%1 одновременно · пауза %2 с").arg(concurrency).arg(delay)
            : tr("%1 одновременно · без паузы").arg(concurrency));
}

void AccountsView::setStatus(const QString& text, const QString& tone)
{
    if (!m_statusLabel || !m_statusDot)
        return;
    m_statusLabel->setText(text);
    controls::setTone(m_statusLabel, tone);
    m_statusDot->setTone(tone);
}

void AccountsView::setLaunching(bool launching)
{
    m_launching = launching;
    refreshLaunchButton();
    if (launching) {
        setStatus(tr("Запуск…"), QStringLiteral("busy"));
    }
}

void AccountsView::setFieldHint(icons::IconLabel* icon, QLabel* label,
                                const QString& iconName, const QString& text,
                                const QString& tone)
{
    if (!icon || !label)
        return;
    icon->setIcon(iconName);
    icon->setTone(tone);
    label->setText(text);
    label->setProperty("class", tone == QLatin1String("error")
                                    ? QStringLiteral("fieldError")
                                    : QStringLiteral("fieldHint"));
    controls::repolish(label);
}

void AccountsView::updatePlaceHint()
{
    if (!m_placeField)
        return;
    const QString value = m_placeField->text().trimmed();
    m_placeField->setInvalid(false);
    if (value.isEmpty()) {
        m_placeTag->setText(QStringLiteral("—"));
        setFieldHint(m_placeHintIcon, m_placeHintText, QStringLiteral("info"),
                     tr("ID игры или ссылка на Roblox"),
                     QStringLiteral("muted"));
        return;
    }

    const auto parsed = ex::launcher::LaunchUri::parseInput(value);
    if (parsed.placeId > 0) {
        m_placeTag->setText(QString::number(parsed.placeId));
        setFieldHint(m_placeHintIcon, m_placeHintText, QStringLiteral("check"),
                     tr("Place %1 · цель распознана").arg(parsed.placeId),
                     QStringLiteral("success"));
    } else if (!parsed.shareCode.isEmpty()) {
        m_placeTag->setText(QStringLiteral("VIP"));
        setFieldHint(m_placeHintIcon, m_placeHintText, QStringLiteral("check"),
                     tr("VIP-приглашение распознано"),
                     QStringLiteral("success"));
    } else if (!parsed.privateServerCode.isEmpty()) {
        m_placeTag->setText(QStringLiteral("VIP"));
        setFieldHint(m_placeHintIcon, m_placeHintText, QStringLiteral("check"),
                     tr("Код VIP-сервера распознан"),
                     QStringLiteral("success"));
    } else {
        m_placeTag->setText(QStringLiteral("—"));
        setFieldHint(m_placeHintIcon, m_placeHintText, QStringLiteral("alert"),
                     tr("Не удалось найти ID игры"),
                     QStringLiteral("error"));
    }
}

void AccountsView::resetHintStates()
{
    m_placeField->setInvalid(false);
    m_vipField->setInvalid(false);
    updatePlaceHint();
    setFieldHint(m_vipHintIcon, m_vipHintText, QStringLiteral("info"),
                 tr("share-ссылка или privateServerLinkCode"),
                 QStringLiteral("muted"));
}

void AccountsView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    updateLaunchSettingsHint();
}

bool AccountsView::accountMatchesFilter(qint64 accountId) const
{
    if (m_searchFilter.isEmpty())
        return true;
    const auto account = m_ctx.store().accountById(accountId);
    if (!account)
        return false;
    return account->aliasOrUsername().contains(m_searchFilter,
                                               Qt::CaseInsensitive)
        || account->username.contains(m_searchFilter, Qt::CaseInsensitive)
        || QString::number(account->userId).contains(m_searchFilter);
}

QTreeWidgetItem* AccountsView::groupItem(QTreeWidget* tree, qint64 groupId,
                                         const QString& name)
{
    auto it = m_groupItems.constFind(groupId);
    if (it != m_groupItems.constEnd())
        return it.value();
    auto* item = new QTreeWidgetItem(tree);
    item->setText(0, name.isEmpty() ? tr("Без группы") : name);
    item->setData(0, AccountDelegate::RoleGroupId,
                  static_cast<qlonglong>(groupId));
    item->setFlags(Qt::ItemIsEnabled);
    item->setExpanded(true);
    m_groupItems.insert(groupId, item);
    return item;
}

void AccountsView::populateTree()
{
    const QList<qint64> previouslySelected = selectedAccountIds();
    const QSet<qint64> selected(previouslySelected.cbegin(),
                                previouslySelected.cend());

    for (auto it = m_groupItems.constBegin();
         it != m_groupItems.constEnd(); ++it) {
        if (!it.value()->isExpanded())
            m_collapsedGroups.insert(it.key());
        else
            m_collapsedGroups.remove(it.key());
    }

    m_populating = true;
    {
        const QSignalBlocker blocker(m_tree);
        m_groupItems.clear();
        m_accountItems.clear();
        m_tree->clear();

        const auto groups = m_ctx.store().groups();
        QHash<qint64, QString> groupNames;
        for (const auto& group : groups) {
            groupNames.insert(group.id, group.name);
            groupItem(m_tree, group.id, group.name);
        }

        const auto accounts = m_ctx.store().accounts();
        for (const auto& account : accounts) {
            if (!accountMatchesFilter(account.id))
                continue;

            QString name;
            if (account.groupId == 0)
                name = tr("Без группы");
            else
                name = groupNames.value(account.groupId,
                                        tr("Группа %1").arg(account.groupId));
            QTreeWidgetItem* parent = groupItem(m_tree, account.groupId, name);

            auto* item = new QTreeWidgetItem(parent);
            item->setText(0, account.aliasOrUsername());
            item->setData(0, AccountDelegate::RoleAccountId, account.id);
            item->setData(0, AccountDelegate::RoleUsername, account.username);
            const auto presence = m_ctx.accounts().presenceFor(account.id);
            item->setData(0, AccountDelegate::RolePresence,
                          static_cast<int>(presence.type));
            item->setData(0, AccountDelegate::RoleFavorite, account.favorite);
            const bool active = presence.type == ex::model::PresenceType::InGame
                || presence.type == ex::model::PresenceType::Online
                || presence.type == ex::model::PresenceType::InStudio;
            item->setData(0, AccountDelegate::RoleLastUsed,
                          active ? tr("сейчас") : lastUsedText(account.lastUsed));
            item->setData(0, AccountDelegate::RoleAvatarUrl, account.avatarUrl);
            item->setToolTip(0, account.username.isEmpty()
                                    ? account.aliasOrUsername()
                                    : QStringLiteral("@") + account.username);
            if (selected.contains(account.id))
                item->setSelected(true);
            m_accountItems.insert(account.id, item);
        }

        for (auto it = m_groupItems.constBegin();
             it != m_groupItems.constEnd(); ++it) {
            QTreeWidgetItem* group = it.value();
            const int count = group->childCount();
            const bool allSelected = count > 0;
            bool all = allSelected;
            for (int i = 0; i < count; ++i) {
                if (!group->child(i)->isSelected()) {
                    all = false;
                    break;
                }
            }
            group->setData(0, AccountDelegate::RoleGroupCount, count);
            group->setData(0, AccountDelegate::RoleGroupAllSelected, all);
            group->setHidden(!m_searchFilter.isEmpty() && count == 0);
            const bool expanded = !m_collapsedGroups.contains(it.key())
                || !m_searchFilter.isEmpty();
            group->setExpanded(expanded && count > 0);
        }
    }
    m_populating = false;

    refreshListState();
    updateHeaderStats();
    updateGroupSelectionFlags();
    updateSelectionUi();
}

void AccountsView::reload()
{
    populateTree();
    updateLaunchSettingsHint();
    updateHeaderStats();
}

void AccountsView::refreshListState()
{
    const int total = m_ctx.store().accounts().size();
    const int visible = m_accountItems.size();
    m_listCount->setText(m_searchFilter.isEmpty()
                             ? QString::number(total)
                             : tr("%1 из %2").arg(visible).arg(total));

    const bool empty = visible == 0;
    m_listStack->setCurrentIndex(empty ? 1 : 0);
    if (!empty)
        return;

    const bool filtered = !m_searchFilter.isEmpty();
    m_emptyIcon->setIcon(filtered ? QStringLiteral("search")
                                  : QStringLiteral("user-plus"));
    m_emptyTitle->setText(filtered ? tr("Ничего не найдено")
                                   : tr("Пока нет аккаунтов"));
    m_emptySub->setText(filtered
                            ? tr("Попробуйте изменить запрос или сбросить фильтр.")
                            : tr("Добавьте первый аккаунт, чтобы начать."));
    m_emptyReset->setVisible(filtered);
}

void AccountsView::updateHeaderStats()
{
    const auto accounts = m_ctx.store().accounts();
    int online = 0;
    for (const auto& account : accounts) {
        const auto type = m_ctx.accounts().presenceFor(account.id).type;
        if (type == ex::model::PresenceType::InGame
            || type == ex::model::PresenceType::Online
            || type == ex::model::PresenceType::InStudio) {
            ++online;
        }
    }
    m_pageSubtitle->setText(
        tr("%1 %2 · %3 в сети")
            .arg(accounts.size())
            .arg(accountsWord(accounts.size()))
            .arg(online));
}

QList<qint64> AccountsView::selectedAccountIds() const
{
    QSet<qint64> seen;
    QList<qint64> out;
    const auto add = [&seen, &out](qint64 id) {
        if (id > 0 && !seen.contains(id)) {
            seen.insert(id);
            out.append(id);
        }
    };
    for (QTreeWidgetItem* item : m_tree->selectedItems()) {
        const QVariant accountId = item->data(0, AccountDelegate::RoleAccountId);
        if (accountId.isValid()) {
            add(accountId.toLongLong());
            continue;
        }
        if (item->data(0, AccountDelegate::RoleGroupId).isValid()) {
            for (int i = 0; i < item->childCount(); ++i) {
                const QVariant childId =
                    item->child(i)->data(0, AccountDelegate::RoleAccountId);
                if (childId.isValid())
                    add(childId.toLongLong());
            }
        }
    }
    return out;
}

void AccountsView::updateSelectionUi()
{
    QList<ex::model::Account> selected;
    for (qint64 id : selectedAccountIds()) {
        if (const auto account = m_ctx.store().accountById(id))
            selected.append(*account);
    }
    updateSelectionBox(selected);
    updateGroupSelectionFlags();
    m_deleteBtn->setEnabled(!selected.isEmpty());
    refreshLaunchButton();
}

void AccountsView::updateSelectionBox(
    const QList<ex::model::Account>& selected)
{
    controls::clearLayout(m_selectionLayout);
    m_selectionBox->setProperty("empty", selected.isEmpty());
    controls::repolish(m_selectionBox);

    if (selected.isEmpty()) {
        auto* icon = new icons::IconLabel(QStringLiteral("accounts"), 16,
                                          m_selectionBox);
        icon->setTone(QStringLiteral("muted"));
        m_selectionLayout->addWidget(icon, 0, Qt::AlignVCenter);

        auto* copy = new QVBoxLayout;
        copy->setContentsMargins(0, 0, 0, 0);
        copy->setSpacing(1);
        copy->addWidget(controls::label(tr("Аккаунты не выбраны"),
                                        QStringLiteral("selTitle")));
        copy->addWidget(controls::label(tr("Отметьте один или несколько слева"),
                                        QStringLiteral("selSub")));
        m_selectionLayout->addLayout(copy, 1);

        auto* allBtn = new QPushButton(tr("Все"), m_selectionBox);
        allBtn->setProperty("class", QStringLiteral("linkBtn"));
        allBtn->setCursor(Qt::PointingHandCursor);
        connect(allBtn, &QPushButton::clicked, this,
                &AccountsView::selectAllVisibleAccounts);
        m_selectionLayout->addWidget(allBtn, 0, Qt::AlignVCenter);
        return;
    }

    const int shownCount = qMin(3, static_cast<int>(selected.size()));
    auto* avatars = new QHBoxLayout;
    avatars->setContentsMargins(0, 0, 0, 0);
    avatars->setSpacing(3);
    for (int i = 0; i < shownCount; ++i) {
        const auto& account = selected.at(i);
        auto* chip = new QLabel(m_selectionBox);
        chip->setFixedSize(28, 28);
        chip->setPixmap(AvatarCache::monogram(
            account.username.isEmpty() ? account.aliasOrUsername()
                                       : account.username));
        chip->setScaledContents(true);
        avatars->addWidget(chip);
    }
    if (selected.size() > shownCount) {
        auto* more = new QLabel(QStringLiteral("+%1")
                                    .arg(selected.size() - shownCount),
                                m_selectionBox);
        more->setProperty("class", QStringLiteral("stackMore"));
        more->setFixedSize(28, 28);
        more->setAlignment(Qt::AlignCenter);
        avatars->addWidget(more);
    }
    auto* avatarHost = new QWidget(m_selectionBox);
    avatarHost->setLayout(avatars);
    avatars->setContentsMargins(0, 0, 0, 0);
    m_selectionLayout->addWidget(avatarHost, 0, Qt::AlignVCenter);

    QStringList allNames;
    for (const auto& account : selected)
        allNames.append(account.aliasOrUsername());
    QString names;
    if (selected.size() <= 2) {
        names = allNames.join(QStringLiteral(", "));
    } else {
        names = tr("%1, %2 и ещё %3")
                    .arg(allNames.at(0), allNames.at(1))
                    .arg(selected.size() - 2);
    }

    auto* copy = new QVBoxLayout;
    copy->setContentsMargins(0, 0, 0, 0);
    copy->setSpacing(1);
    auto* title = controls::label(
        tr("%1 %2").arg(selected.size()).arg(accountsWord(selected.size())),
        QStringLiteral("selTitle"));
    auto* sub = controls::label(names, QStringLiteral("selSub"));
    sub->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    sub->setToolTip(allNames.join(QStringLiteral(", ")));
    copy->addWidget(title);
    copy->addWidget(sub);
    m_selectionLayout->addLayout(copy, 1);

    auto* clear = new QPushButton(m_selectionBox);
    clear->setProperty("class", QStringLiteral("fieldAction"));
    clear->setCursor(Qt::PointingHandCursor);
    clear->setToolTip(tr("Снять выбор"));
    icons::setGlyph(clear, QStringLiteral("close"), QStringLiteral("muted"));
    clear->setFixedSize(24, 24);
    connect(clear, &QPushButton::clicked, this, &AccountsView::clearSelection);
    m_selectionLayout->addWidget(clear, 0, Qt::AlignVCenter);
}

void AccountsView::refreshLaunchButton()
{
    if (!m_launchBtn)
        return;
    const int count = selectedAccountIds().size();
    m_launchBtn->setEnabled(count > 0 && !m_launching);
    if (m_launching) {
        m_launchBtn->setText(tr("Запускаем…"));
    } else if (count == 0) {
        m_launchBtn->setText(tr("Запустить"));
    } else {
        m_launchBtn->setText(tr("Запустить %1 %2")
                                 .arg(count)
                                 .arg(accountsWord(count)));
    }
}

void AccountsView::updateGroupSelectionFlags()
{
    for (auto it = m_groupItems.constBegin();
         it != m_groupItems.constEnd(); ++it) {
        QTreeWidgetItem* group = it.value();
        const int count = group->childCount();
        bool all = count > 0;
        for (int i = 0; i < count; ++i) {
            if (!group->child(i)->isSelected()) {
                all = false;
                break;
            }
        }
        group->setData(0, AccountDelegate::RoleGroupAllSelected, all);
    }
    if (m_tree)
        m_tree->viewport()->update();
}

void AccountsView::selectAllVisibleAccounts()
{
    const QSignalBlocker blocker(m_tree);
    m_tree->clearSelection();
    for (auto* item : m_accountItems) {
        if (!item->isHidden())
            item->setSelected(true);
    }
    updateSelectionUi();
}

void AccountsView::clearSelection()
{
    {
        const QSignalBlocker blocker(m_tree);
        m_tree->clearSelection();
    }
    updateSelectionUi();
}

void AccountsView::selectGroupAccounts(QTreeWidgetItem* group, bool select)
{
    if (!group)
        return;
    {
        const QSignalBlocker blocker(m_tree);
        m_tree->clearSelection();
        if (select) {
            group->setExpanded(true);
            m_collapsedGroups.remove(group->data(0, AccountDelegate::RoleGroupId)
                                         .toLongLong());
            for (int i = 0; i < group->childCount(); ++i)
                group->child(i)->setSelected(true);
        }
    }
    updateSelectionUi();
}

void AccountsView::toggleGroupSelection(QTreeWidgetItem* group)
{
    if (!group || group->childCount() == 0)
        return;
    bool all = true;
    for (int i = 0; i < group->childCount(); ++i) {
        if (!group->child(i)->isSelected()) {
            all = false;
            break;
        }
    }
    selectGroupAccounts(group, !all);
}

void AccountsView::onItemClicked(QTreeWidgetItem* item, int)
{
    if (!item)
        return;
    if (item->data(0, AccountDelegate::RoleGroupId).isValid()) {
        m_activeGroupId = item->data(0, AccountDelegate::RoleGroupId).toLongLong();
        const int x = m_tree->viewport()->mapFromGlobal(QCursor::pos()).x();
        const QRect rect = m_tree->visualItemRect(item);
        if (x < rect.left() + 44)
            return; // chevron уже обработан делегатом
        toggleGroupSelection(item);
        return;
    }
    const QVariant accountId = item->data(0, AccountDelegate::RoleAccountId);
    if (accountId.isValid()) {
        if (const auto account = m_ctx.store().accountById(accountId.toLongLong()))
            m_activeGroupId = account->groupId;
    }
}

void AccountsView::onItemDoubleClicked(QTreeWidgetItem* item, int)
{
    if (!item || !item->data(0, AccountDelegate::RoleAccountId).isValid())
        return;
    onLaunch();
}

void AccountsView::onGroupToggle(qint64 groupId)
{
    QTreeWidgetItem* item = m_groupItems.value(groupId, nullptr);
    if (!item)
        return;
    const bool willExpand = !item->isExpanded();
    item->setExpanded(willExpand);
    if (willExpand)
        m_collapsedGroups.remove(groupId);
    else
        m_collapsedGroups.insert(groupId);
    m_tree->viewport()->update();
}

void AccountsView::onFavoriteToggled(qint64 accountId)
{
    auto account = m_ctx.store().accountById(accountId);
    if (!account)
        return;
    account->favorite = !account->favorite;
    if (m_ctx.store().updateAccount(*account)) {
        if (auto* item = m_accountItems.value(accountId, nullptr))
            item->setData(0, AccountDelegate::RoleFavorite, account->favorite);
        m_tree->viewport()->update();
    }
}

void AccountsView::onQuickLaunch(qint64 accountId)
{
    auto* item = m_accountItems.value(accountId, nullptr);
    if (!item)
        return;
    {
        const QSignalBlocker blocker(m_tree);
        m_tree->clearSelection();
        m_tree->setCurrentItem(item);
        item->setSelected(true);
    }
    updateSelectionUi();
    onLaunch();
}

void AccountsView::onSearchChanged(const QString& text)
{
    m_searchFilter = text.trimmed();
    if (m_searchField && m_searchField->trailingButton())
        m_searchField->trailingButton()->setVisible(!text.isEmpty());
    populateTree();
}

void AccountsView::onAddAccount()
{
    showAddDialog(0);
}

void AccountsView::onImport()
{
    showAddDialog(1);
}

void AccountsView::showAddDialog(int mode)
{
    if (!m_ctx.vaultUnlocked()) {
        QMessageBox::warning(this, tr("Хранилище cookie"),
                             tr("Хранилище секретов не разблокировано, поэтому cookie "
                                "некуда сохранить. Перезапустите приложение и "
                                "разблокируйте хранилище."));
        return;
    }

    AddAccountDialog dlg(this);
    dlg.setMode(static_cast<AddAccountDialog::Mode>(mode));
    if (dlg.exec() != QDialog::Accepted)
        return;

    switch (dlg.mode()) {
    case AddAccountDialog::Mode::Cookie: {
        const QString cookie = dlg.cookieText();
        if (cookie.isEmpty())
            return;
        setStatus(tr("Валидация cookie…"), QStringLiteral("busy"));
        m_ctx.accounts().addFromCookie(cookie, [this](bool ok, qint64 accountId,
                                                     const QString& err) {
            setStatus(ok ? tr("Аккаунт добавлен")
                         : tr("Ошибка: %1").arg(accountErrorText(err)),
                      ok ? QStringLiteral("success") : QStringLiteral("error"));
            if (ok) {
                reload();
                // Аватар тянем сразу, не дожидаясь следующего запуска.
                m_ctx.accounts().refreshAvatars({accountId});
            }
        });
        break;
    }
    case AddAccountDialog::Mode::Bulk: {
        const auto entries = ex::roblox::BulkImportParser::parse(dlg.bulkText());
        if (entries.isEmpty()) {
            setStatus(tr("Нечего импортировать"), QStringLiteral("error"));
            return;
        }
        QList<ex::roblox::BulkEntry> cookieEntries;
        QList<ex::roblox::CdpLoginRequest> loginRequests;
        for (const auto& entry : entries) {
            if (!entry.cookie.isEmpty()) {
                cookieEntries.append(entry);
            } else if (!entry.username.isEmpty() && !entry.password.isEmpty()) {
                ex::roblox::CdpLoginRequest request;
                request.username = entry.username;
                request.password = entry.password;
                loginRequests.append(request);
            }
        }
        if (cookieEntries.isEmpty() && loginRequests.isEmpty()) {
            setStatus(tr("Нечего импортировать"), QStringLiteral("error"));
            return;
        }
        if (!cookieEntries.isEmpty()) {
            setStatus(tr("Импорт %1 cookie…").arg(cookieEntries.size()),
                      QStringLiteral("busy"));
            m_ctx.accounts().importBulk(cookieEntries,
                                        [this](int added, int failed) {
                setStatus(tr("Импорт: добавлено %1, ошибок %2")
                              .arg(added).arg(failed),
                          failed > 0 ? QStringLiteral("error")
                                     : QStringLiteral("success"));
                reload();
                if (added > 0)
                    m_ctx.accounts().refreshAvatars({});
            });
        }
        if (!loginRequests.isEmpty()) {
            CdpLoginDialog loginDlg(m_ctx, this);
            loginDlg.start(loginRequests, 2);
            loginDlg.exec();
            reload();
            m_ctx.accounts().refreshAvatars({});
        }
        break;
    }
    case AddAccountDialog::Mode::LoginPass: {
        const QString user = dlg.username();
        const QString pass = dlg.password();
        if (user.isEmpty() || pass.isEmpty()) {
            setStatus(tr("Введите логин и пароль"), QStringLiteral("error"));
            return;
        }
        ex::roblox::CdpLoginRequest request;
        request.username = user;
        request.password = pass;
        CdpLoginDialog loginDlg(m_ctx, this);
        loginDlg.start({request}, 1);
        loginDlg.exec();
        reload();
        m_ctx.accounts().refreshAvatars({});
        break;
    }
    }
}

void AccountsView::onDeleteSelected()
{
    const QList<qint64> ids = selectedAccountIds();
    if (ids.isEmpty()) {
        setStatus(tr("Сначала выберите аккаунты"), QStringLiteral("error"));
        return;
    }
    if (QMessageBox::question(
            this, tr("Удаление"),
            tr("Удалить %1 аккаунт(ов) вместе с сохранёнными cookie?").arg(ids.size()))
        != QMessageBox::Yes) {
        return;
    }
    m_ctx.store().deleteAccounts(ids);
    reload();
}

void AccountsView::onRefresh()
{
    setStatus(tr("Обновление статусов…"), QStringLiteral("busy"));
    m_ctx.accounts().revalidateAll([this]() {
        setStatus(tr("Статусы обновлены"), QStringLiteral("success"));
        reload();
    });
}

void AccountsView::onLaunch()
{
    const QList<qint64> ids = selectedAccountIds();
    if (ids.isEmpty()) {
        setStatus(tr("Сначала выберите аккаунты"), QStringLiteral("error"));
        return;
    }
    if (!m_ctx.launcher().isQueueEmpty()) {
        setStatus(tr("Дождитесь завершения текущей очереди запуска"),
                  QStringLiteral("busy"));
        return;
    }
    const auto mode = static_cast<ex::launcher::BatchMode>(
        m_modeSeg->currentData().toInt());
    launchSelection(mode);
}

void AccountsView::launchSelection(ex::launcher::BatchMode mode)
{
    const QList<qint64> ids = selectedAccountIds();
    if (ids.isEmpty()) {
        setStatus(tr("Сначала выберите аккаунты"), QStringLiteral("error"));
        return;
    }

    resetHintStates();
    const ex::launcher::ParsedInput parsed =
        ex::launcher::LaunchUri::parseInput(m_placeField->text());
    QString jobId = m_jobField->text().trimmed();
    if (jobId.isEmpty())
        jobId = parsed.jobId;

    ex::model::LaunchTarget target;
    if (mode == ex::launcher::BatchMode::PrivateServer) {
        const QString vipText = m_vipField->text().trimmed();
        ex::launcher::ParsedInput vip =
            ex::launcher::LaunchUri::parseInput(vipText);
        if (vipText.isEmpty())
            vip = parsed;

        if (!vip.shareCode.isEmpty()) {
            target.kind = ex::model::LaunchTarget::Kind::ShareLink;
            target.shareCode = vip.shareCode;
        } else {
            qint64 placeId = vip.placeId > 0 ? vip.placeId : parsed.placeId;
            QString code = vip.privateServerCode;
            if (!vipText.isEmpty() && !vipText.contains(QLatin1String("://"))) {
                code = vipText;
                placeId = parsed.placeId;
            }
            if (placeId <= 0) {
                m_placeField->setInvalid(true);
                setFieldHint(m_placeHintIcon, m_placeHintText,
                             QStringLiteral("alert"),
                             tr("VIP-ссылке нужен ID игры — заполните Place"),
                             QStringLiteral("error"));
                m_placeField->lineEdit()->setFocus(Qt::OtherFocusReason);
                setStatus(tr("Укажите Place ID для VIP-сервера"),
                          QStringLiteral("error"));
                return;
            }
            if (code.isEmpty()) {
                m_vipField->setInvalid(true);
                setFieldHint(m_vipHintIcon, m_vipHintText,
                             QStringLiteral("alert"),
                             tr("Вставьте ссылку-приглашение или код сервера"),
                             QStringLiteral("error"));
                m_vipField->lineEdit()->setFocus(Qt::OtherFocusReason);
                setStatus(tr("Вставьте VIP-ссылку"), QStringLiteral("error"));
                return;
            }
            target.kind = ex::model::LaunchTarget::Kind::PrivateServer;
            target.placeId = placeId;
            target.privateServerCode = code;
        }
    } else if (!parsed.shareCode.isEmpty()) {
        target.kind = ex::model::LaunchTarget::Kind::ShareLink;
        target.shareCode = parsed.shareCode;
    } else if (!parsed.privateServerCode.isEmpty()) {
        if (parsed.placeId <= 0) {
            m_placeField->setInvalid(true);
            setFieldHint(m_placeHintIcon, m_placeHintText,
                         QStringLiteral("alert"),
                         tr("VIP-ссылке нужен ID игры — заполните Place"),
                         QStringLiteral("error"));
            setStatus(tr("VIP-ссылка должна содержать ID игры"),
                      QStringLiteral("error"));
            return;
        }
        target.kind = ex::model::LaunchTarget::Kind::PrivateServer;
        target.placeId = parsed.placeId;
        target.privateServerCode = parsed.privateServerCode;
    } else if (parsed.placeId > 0 && !jobId.isEmpty()) {
        target.kind = ex::model::LaunchTarget::Kind::Job;
        target.placeId = parsed.placeId;
        target.jobId = jobId;
    } else if (parsed.placeId > 0) {
        target.kind = ex::model::LaunchTarget::Kind::Place;
        target.placeId = parsed.placeId;
    } else {
        m_placeField->setInvalid(true);
        setFieldHint(m_placeHintIcon, m_placeHintText,
                     QStringLiteral("alert"),
                     tr("Укажите Place ID, ссылку на игру или VIP-ссылку"),
                     QStringLiteral("error"));
        m_placeField->lineEdit()->setFocus(Qt::OtherFocusReason);
        setStatus(tr("Не указана цель запуска"), QStringLiteral("error"));
        return;
    }

    if (!target.isValid()) {
        setStatus(tr("Некорректная цель запуска"), QStringLiteral("error"));
        return;
    }

    ex::launcher::BatchRequest request;
    request.accountIds = ids;
    request.target = target;
    request.mode = mode;
    request.concurrency =
        m_ctx.settings().get(QStringLiteral("launcher.concurrency")).toInt();
    request.delaySeconds =
        m_ctx.settings().get(QStringLiteral("launcher.delaySeconds")).toInt();
    request.options.robloxPath =
        m_ctx.settings().get(QStringLiteral("launcher.robloxPath")).toString();

    const bool customWindow = m_windowSizeCheck->isChecked();
    m_ctx.settings().set(QStringLiteral("launcher.windowSizeEnabled"), customWindow);
    m_ctx.settings().set(QStringLiteral("launcher.windowWidth"),
                         m_windowWidth->value());
    m_ctx.settings().set(QStringLiteral("launcher.windowHeight"),
                         m_windowHeight->value());
    if (customWindow) {
        ex::model::WindowProfile window;
        window.enabled = true;
        window.width = m_windowWidth->value();
        window.height = m_windowHeight->value();
        request.options.window = window;
    }

    m_lastLaunchError.clear();
    setLaunching(true);
    setStatus(tr("Запуск · %1 %2…")
                  .arg(ids.size()).arg(accountsWord(ids.size())),
              QStringLiteral("busy"));
    m_ctx.launcher().launchBatch(request);
}

void AccountsView::onTreeContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    QMenu menu(this);

    if (!item) {
        menu.addAction(tr("Создать группу"), this, &AccountsView::onAddGroup);
    } else if (item->data(0, AccountDelegate::RoleGroupId).isValid()) {
        const qint64 groupId =
            item->data(0, AccountDelegate::RoleGroupId).toLongLong();
        m_activeGroupId = groupId;
        m_tree->setCurrentItem(item);

        menu.addAction(tr("Выбрать все аккаунты"), this, [this, item]() {
            selectGroupAccounts(item, true);
        });
        menu.addAction(tr("Запустить группу"), this, [this, item]() {
            selectGroupAccounts(item, true);
            onLaunch();
        });
        if (groupId > 0) {
            menu.addSeparator();
            menu.addAction(tr("Переименовать группу"), this, [this, groupId]() {
                renameGroup(groupId);
            });
            menu.addAction(tr("Удалить группу"), this, [this, groupId]() {
                deleteGroupById(groupId);
            });
        }
    } else if (item->data(0, AccountDelegate::RoleAccountId).isValid()) {
        const qint64 accountId =
            item->data(0, AccountDelegate::RoleAccountId).toLongLong();
        if (const auto account = m_ctx.store().accountById(accountId))
            m_activeGroupId = account->groupId;
        if (!item->isSelected()) {
            m_tree->clearSelection();
            item->setSelected(true);
        }
        m_tree->setCurrentItem(item);

        auto* assign = menu.addMenu(tr("Переместить в группу"));
        assign->addAction(tr("Без группы"), this, [this]() {
            assignSelectedToGroup(0);
        });
        for (const auto& group : m_ctx.store().groups()) {
            assign->addAction(group.name, this, [this, id = group.id]() {
                assignSelectedToGroup(id);
            });
        }
        menu.addSeparator();
        menu.addAction(tr("Запустить выбранные"), this, &AccountsView::onLaunch);
    }
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void AccountsView::onAddGroup()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Новая группа"), tr("Название группы:"),
        QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;

    ex::model::Group group;
    group.name = name;
    group.sortOrder = m_ctx.store().groups().size();
    if (!m_ctx.store().addGroup(group)) {
        QMessageBox::warning(this, tr("Группы"),
                             tr("Не удалось создать группу. Возможно, имя уже занято."));
        return;
    }
    reload();
}

qint64 AccountsView::currentGroupId() const
{
    if (m_activeGroupId >= 0)
        return m_activeGroupId;
    QTreeWidgetItem* item = m_tree->currentItem();
    if (!item)
        return -1;
    const QVariant groupId = item->data(0, AccountDelegate::RoleGroupId);
    if (groupId.isValid())
        return groupId.toLongLong();
    const QVariant accountId = item->data(0, AccountDelegate::RoleAccountId);
    if (accountId.isValid()) {
        if (const auto account = m_ctx.store().accountById(accountId.toLongLong()))
            return account->groupId;
    }
    return -1;
}

QString AccountsView::groupName(qint64 groupId) const
{
    for (const auto& group : m_ctx.store().groups()) {
        if (group.id == groupId)
            return group.name;
    }
    return groupId == 0 ? tr("Без группы") : QString();
}

void AccountsView::onRenameGroup()
{
    const qint64 groupId = currentGroupId();
    if (groupId <= 0) {
        setStatus(tr("Выберите группу для переименования"),
                  QStringLiteral("error"));
        return;
    }
    renameGroup(groupId);
}

void AccountsView::renameGroup(qint64 groupId)
{
    const auto groups = m_ctx.store().groups();
    auto it = std::find_if(groups.cbegin(), groups.cend(),
                           [groupId](const ex::model::Group& group) {
                               return group.id == groupId;
                           });
    if (it == groups.cend())
        return;

    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Переименовать группу"), tr("Название группы:"),
        QLineEdit::Normal, it->name, &ok).trimmed();
    if (!ok || name.isEmpty() || name == it->name)
        return;

    ex::model::Group group = *it;
    group.name = name;
    if (!m_ctx.store().updateGroup(group)) {
        QMessageBox::warning(this, tr("Группы"),
                             tr("Не удалось переименовать. Возможно, имя уже занято."));
        return;
    }
    reload();
}

void AccountsView::onDeleteGroup()
{
    const qint64 groupId = currentGroupId();
    if (groupId <= 0) {
        setStatus(tr("Выберите группу для удаления"), QStringLiteral("error"));
        return;
    }
    deleteGroupById(groupId);
}

void AccountsView::deleteGroupById(qint64 groupId)
{
    if (groupId <= 0)
        return;
    const QString name = groupName(groupId);
    if (QMessageBox::question(
            this, tr("Удаление группы"),
            tr("Удалить группу «%1»? Аккаунты останутся, но без группы.").arg(name))
        != QMessageBox::Yes) {
        return;
    }
    if (!m_ctx.store().deleteGroup(groupId)) {
        QMessageBox::warning(this, tr("Группы"),
                             tr("Не удалось удалить группу."));
        return;
    }
    m_activeGroupId = -1;
    reload();
}

void AccountsView::assignSelectedToGroup(qint64 groupId)
{
    const QList<qint64> ids = selectedAccountIds();
    if (ids.isEmpty())
        return;
    int moved = 0;
    for (qint64 id : ids) {
        auto account = m_ctx.store().accountById(id);
        if (!account || account->groupId == groupId)
            continue;
        account->groupId = groupId;
        if (m_ctx.store().updateAccount(*account))
            ++moved;
    }
    if (moved > 0) {
        const QString target = groupName(groupId);
        setStatus(tr("Перемещено в группу «%1»: %2").arg(target).arg(moved),
                  QStringLiteral("success"));
        reload();
    }
}

QString AccountsView::launchErrorText(const QString& error)
{
    if (error == QLatin1String("vault-locked"))
        return tr("хранилище cookie заблокировано — перезапустите вход");
    if (error == QLatin1String("no-session"))
        return tr("нет сохранённой сессии — добавьте аккаунт заново");
    if (error == QLatin1String("cookie-rejected"))
        return tr("Roblox отклонил cookie — войдите заново");
    if (error == QLatin1String("network-error"))
        return tr("нет связи с Roblox");
    if (error == QLatin1String("rate-limited"))
        return tr("слишком много запросов, повторите позже");
    if (error == QLatin1String("blocked"))
        return tr("Roblox заблокировал запрос — повторите позже");
    if (error == QLatin1String("csrf-failed"))
        return tr("не удалось пройти CSRF-проверку Roblox");
    if (error == QLatin1String("no-ticket-header"))
        return tr("Roblox не выдал auth ticket");
    if (error == QLatin1String("shell-failed"))
        return tr("Windows не смог открыть roblox-player");
    if (error == QLatin1String("curl-failed")
        || error == QLatin1String("curl-start-failed")) {
        return tr("не удалось получить auth ticket");
    }
    if (error == QLatin1String("bad-target")
        || error == QLatin1String("share-link-invalid")) {
        return tr("некорректная цель запуска");
    }
    if (error == QLatin1String("not-a-private-server-link"))
        return tr("ссылка не является приглашением на VIP-сервер");
    if (error.startsWith(QLatin1String("share-link-")))
        return tr("VIP-приглашение недействительно (%1)").arg(error.mid(11));
    if (error.startsWith(QLatin1String("http-")))
        return tr("Roblox ответил ошибкой %1").arg(error.mid(5));
    return error.isEmpty() ? tr("неизвестная ошибка") : error;
}

QString AccountsView::accountErrorText(const QString& error)
{
    if (error == QLatin1String("not-a-cookie")) {
        return tr("это не похоже на .ROBLOSECURITY cookie — значение "
                  "обрезано или скопировано не полностью");
    }
    if (error == QLatin1String("cookie-rejected"))
        return tr("Roblox отклонил cookie. Вставьте свежее значение "
                  "или войдите по логину и паролю");
    if (error == QLatin1String("no-ticket-header"))
        return tr("Roblox не выдал auth ticket для этого cookie");
    if (error == QLatin1String("network-error"))
        return tr("нет связи с Roblox");
    if (error == QLatin1String("vault-locked"))
        return tr("хранилище секретов заблокировано");
    if (error == QLatin1String("store-failed"))
        return tr("не удалось сохранить аккаунт в базе");
    if (error == QLatin1String("bad-account-info"))
        return tr("некорректные данные аккаунта");
    if (error == QLatin1String("curl-failed")
        || error == QLatin1String("curl-start-failed")) {
        return tr("не удалось проверить запуск");
    }
    if (error == QLatin1String("bad-response"))
        return tr("Roblox вернул неожиданный ответ");
    if (error == QLatin1String("rate-limited"))
        return tr("слишком много запросов, повторите позже");
    if (error == QLatin1String("blocked"))
        return tr("Roblox заблокировал запрос — повторите позже");
    if (error.startsWith(QLatin1String("http-")))
        return tr("Roblox ответил ошибкой %1").arg(error.mid(5));
    return error;
}

} // namespace ex::ui

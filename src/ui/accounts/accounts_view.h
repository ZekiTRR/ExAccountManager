#pragma once

#include "core/launcher/launch_service.h"
#include "core/models/account.h"
#include "ui/accounts/avatar_cache.h"

#include <QHash>
#include <QList>
#include <QPoint>
#include <QSet>
#include <QWidget>

class QCheckBox;
class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QShowEvent;
class QSpinBox;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

namespace ex::ui {
namespace controls { class StatusDot; }
namespace icons { class IconLabel; }

class AccountDelegate;
class AppContext;
class Field;
class Segmented;

// Экран аккаунтов в стиле макета: карточка списка с группами и
// отдельная панель запуска справа. Группа выбирается кликом, звёздочка
// переключает избранное, play на строке — быстрый запуск.
class AccountsView : public QWidget {
    Q_OBJECT
public:
    explicit AccountsView(AppContext& ctx, QWidget* parent = nullptr);

    void reload();
    QList<qint64> selectedAccountIds() const;

signals:
    void settingsRequested();

protected:
    void showEvent(QShowEvent* event) override;

private slots:
    void onAddAccount();
    void onImport();
    void onDeleteSelected();
    void onRefresh();
    void onLaunch();
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onTreeContextMenu(const QPoint& pos);
    void onAddGroup();
    void onRenameGroup();
    void onDeleteGroup();
    void onSearchChanged(const QString& text);

private:
    void buildUi();
    void buildHeader(QVBoxLayout* root);
    void populateTree();
    QTreeWidgetItem* groupItem(QTreeWidget* tree, qint64 groupId, const QString& name);
    void selectGroupAccounts(QTreeWidgetItem* group, bool select);
    void toggleGroupSelection(QTreeWidgetItem* group);
    void assignSelectedToGroup(qint64 groupId);
    qint64 currentGroupId() const;
    QString groupName(qint64 groupId) const;
    void renameGroup(qint64 groupId);
    void deleteGroupById(qint64 groupId);
    void updateTargetFields();
    void launchSelection(ex::launcher::BatchMode mode);
    [[nodiscard]] bool accountMatchesFilter(qint64 accountId) const;

    void updateHeaderStats();
    void updateSelectionUi();
    void updateSelectionBox(const QList<ex::model::Account>& selected);
    void updateGroupSelectionFlags();
    void refreshLaunchButton();
    void refreshListState();
    void updateLaunchSettingsHint();
    void showAddDialog(int mode);
    void selectAllVisibleAccounts();
    void clearSelection();
    void setStatus(const QString& text, const QString& tone = QString());
    void setLaunching(bool launching);
    void updatePlaceHint();
    void resetHintStates();
    void setFieldHint(icons::IconLabel* icon, QLabel* label,
                      const QString& iconName, const QString& text,
                      const QString& tone);
    void onFavoriteToggled(qint64 accountId);
    void onQuickLaunch(qint64 accountId);
    void onGroupToggle(qint64 groupId);

    static QString launchErrorText(const QString& error);
    static QString accountErrorText(const QString& error);

    AppContext& m_ctx;
    AvatarCache m_avatars;
    AccountDelegate* m_delegate = nullptr;
    QTreeWidget* m_tree = nullptr;
    QStackedWidget* m_listStack = nullptr;
    QWidget* m_emptyPage = nullptr;
    icons::IconLabel* m_emptyIcon = nullptr;
    QLabel* m_emptyTitle = nullptr;
    QLabel* m_emptySub = nullptr;
    QPushButton* m_emptyReset = nullptr;
    QHash<qint64, QTreeWidgetItem*> m_groupItems;
    QHash<qint64, QTreeWidgetItem*> m_accountItems;

    Field* m_searchField = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QLabel* m_pageSubtitle = nullptr;
    QLabel* m_listCount = nullptr;
    QPushButton* m_deleteBtn = nullptr;
    QMenu* m_groupsMenu = nullptr;

    QFrame* m_selectionBox = nullptr;
    QHBoxLayout* m_selectionLayout = nullptr;
    Field* m_placeField = nullptr;
    Field* m_jobField = nullptr;
    Field* m_vipField = nullptr;
    QWidget* m_jobGroup = nullptr;
    QWidget* m_vipGroup = nullptr;
    QLabel* m_placeTag = nullptr;
    icons::IconLabel* m_placeHintIcon = nullptr;
    QLabel* m_placeHintText = nullptr;
    icons::IconLabel* m_vipHintIcon = nullptr;
    QLabel* m_vipHintText = nullptr;
    Segmented* m_modeSeg = nullptr;
    QCheckBox* m_windowSizeCheck = nullptr;
    QSpinBox* m_windowWidth = nullptr;
    QSpinBox* m_windowHeight = nullptr;
    QPushButton* m_launchBtn = nullptr;
    QPushButton* m_launchSettingsBtn = nullptr;
    controls::StatusDot* m_statusDot = nullptr;
    QLabel* m_statusLabel = nullptr;

    QSet<qint64> m_collapsedGroups;
    qint64 m_activeGroupId = -1;
    QString m_lastLaunchError;
    QString m_searchFilter;
    bool m_selectingGroup = false;
    bool m_populating = false;
    bool m_launching = false;
};

} // namespace ex::ui

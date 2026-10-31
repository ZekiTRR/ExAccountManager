#pragma once

#include <QStyledItemDelegate>

namespace ex::ui {

class AvatarCache;

// Account list rows in the new mockup style: group header with
// chevron/counter/"Select", an account card with checkbox, avatar,
// presence dot, @username, last launch and favourite.
class AccountDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    enum Role {
        RoleAccountId = Qt::UserRole + 2,
        RoleGroupId = Qt::UserRole + 3,
        RoleUsername,
        RolePresence,
        RoleFavorite,
        RoleLastUsed,
        RoleAvatarUrl,
        RoleGroupCount,
        RoleGroupAllSelected,
    };

    explicit AccountDelegate(AvatarCache& avatars, QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override;
    bool editorEvent(QEvent* event, QAbstractItemModel* model,
                     const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;

signals:
    void avatarsUpdated();
    void favoriteRequested(qint64 accountId);
    void quickLaunchRequested(qint64 accountId);
    void groupToggleRequested(qint64 groupId);

private:
    AvatarCache& m_avatars;
};

} // namespace ex::ui

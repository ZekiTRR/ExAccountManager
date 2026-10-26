#pragma once

#include <QStyledItemDelegate>

namespace ex::ui {

class AvatarCache;

// Строки списка аккаунтов в стиле нового макета: заголовок группы с
// chevron/счётчиком/«Выбрать», карточка аккаунта с чекбоксом, аватаром,
// presence-точкой, @username, последним запуском и избранным.
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

#include "ui/accounts/account_delegate.h"

#include "core/models/account.h"
#include "ui/accounts/avatar_cache.h"
#include "ui/material/icons.h"
#include "ui/theme.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QTreeView>
#include <QUrl>

namespace ex::ui {
namespace {

constexpr int kGroupRowHeight = 34;
constexpr int kAccountRowHeight = 52;
constexpr int kAvatarSize = 40;
constexpr int kContentMarginX = 6;
constexpr int kRowRadius = 12;

QFont fontFor(const QWidget* widget, int pixelSize, QFont::Weight weight)
{
    QFont f = widget ? widget->font() : QFont();
    f.setPixelSize(pixelSize);
    f.setWeight(weight);
    return f;
}

QRectF groupChevronRect(const QRect& row)
{
    return QRectF(row.left() + kContentMarginX + 8,
                  row.center().y() - 8, 16, 16);
}

QRectF starRect(const QRect& row)
{
    return QRectF(row.right() - kContentMarginX - 12 - 24,
                  row.center().y() - 12, 24, 24);
}

QRectF quickRect(const QRect& row)
{
    const QRectF star = starRect(row);
    return QRectF(star.left() - 26, star.top(), 24, 24);
}

QString presenceText(ex::model::PresenceType type)
{
    switch (type) {
    case ex::model::PresenceType::InGame: return QObject::tr("В игре");
    case ex::model::PresenceType::Online: return QObject::tr("В сети");
    case ex::model::PresenceType::InStudio: return QObject::tr("В Studio");
    default: return {};
    }
}

QColor presenceColor(const theme::Palette& c, ex::model::PresenceType type)
{
    switch (type) {
    case ex::model::PresenceType::InGame: return c.presenceGame;
    case ex::model::PresenceType::Online: return c.presenceOnline;
    case ex::model::PresenceType::InStudio: return c.presenceStudio;
    default: return c.outline;
    }
}

} // namespace

AccountDelegate::AccountDelegate(AvatarCache& avatars, QObject* parent)
    : QStyledItemDelegate(parent)
    , m_avatars(avatars)
{
    connect(&m_avatars, &AvatarCache::avatarReady, this,
            &AccountDelegate::avatarsUpdated);
}

void AccountDelegate::paint(QPainter* p, const QStyleOptionViewItem& option,
                            const QModelIndex& index) const
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::SmoothPixmapTransform);
    const auto& c = theme::currentPalette();
    const QRect r = option.rect;
    const bool hovered = option.state & QStyle::State_MouseOver;

    if (index.data(RoleGroupId).isValid()) {
        const bool expanded = [&]() {
            if (const auto* view = qobject_cast<const QTreeView*>(option.widget))
                return view->isExpanded(index);
            return true;
        }();

        const QRectF bg(r.left() + kContentMarginX, r.top() + 2,
                        r.width() - kContentMarginX * 2, r.height() - 4);
        if (hovered) {
            QColor overlay = c.onSurface;
            overlay.setAlpha(8);
            p->setPen(Qt::NoPen);
            p->setBrush(overlay);
            p->drawRoundedRect(bg, 9, 9);
        }

        const QRectF chevron = groupChevronRect(r);
        icons::paint(p, expanded ? QStringLiteral("chevron-down")
                                 : QStringLiteral("chevron-right"),
                     chevron, c.iconDim);

        QFont nameFont = fontFor(option.widget, 12, QFont::Medium);
        p->setFont(nameFont);
        p->setPen(c.onSurfaceVariant);
        const QString name = index.data(Qt::DisplayRole).toString();
        const QFontMetrics fm(nameFont);
        const int nameX = qRound(chevron.right()) + 6;
        const int nameW = fm.horizontalAdvance(name);
        p->drawText(QRect(nameX, r.top(), qMin(nameW, r.width() - 220), r.height()),
                    Qt::AlignVCenter | Qt::AlignLeft,
                    fm.elidedText(name, Qt::ElideRight, qMax(20, r.width() - 240)));

        p->setFont(fontFor(option.widget, 11, QFont::Normal));
        p->setPen(c.outline);
        p->drawText(QRect(nameX + nameW + 5, r.top(), 36, r.height()),
                    Qt::AlignVCenter | Qt::AlignLeft,
                    QString::number(index.data(RoleGroupCount).toInt()));

        const bool allSelected = index.data(RoleGroupAllSelected).toBool();
        p->setFont(fontFor(option.widget, 11, QFont::Normal));
        QColor actionColor = c.outline;
        actionColor.setAlpha(hovered ? 255 : 150);
        p->setPen(actionColor);
        p->drawText(QRect(r.right() - kContentMarginX - 76, r.top(), 68, r.height()),
                    Qt::AlignVCenter | Qt::AlignRight,
                    allSelected ? tr("Снять") : tr("Выбрать"));

        p->restore();
        return;
    }

    const qint64 accountId = index.data(RoleAccountId).toLongLong();
    if (accountId <= 0) {
        p->restore();
        return;
    }

    const bool selected = option.state & QStyle::State_Selected;
    QColor rowBg = c.surfaceContainer;
    if (selected) {
        rowBg = c.secondaryContainer;
        p->setPen(QPen(QColor(c.accent.red(), c.accent.green(),
                              c.accent.blue(), 52), 1));
        p->setBrush(rowBg);
        p->drawRoundedRect(QRectF(r.left() + kContentMarginX, r.top() + 2,
                                  r.width() - kContentMarginX * 2,
                                  r.height() - 4).adjusted(0.5, 0.5, -0.5, -0.5),
                           kRowRadius, kRowRadius);
    } else if (hovered) {
        QColor overlay = c.onSurface;
        overlay.setAlpha(10);
        p->setPen(Qt::NoPen);
        p->setBrush(overlay);
        p->drawRoundedRect(QRectF(r.left() + kContentMarginX, r.top() + 2,
                                  r.width() - kContentMarginX * 2,
                                  r.height() - 4), kRowRadius, kRowRadius);
        rowBg = c.surfaceContainer;
    }

    const QRectF card(r.left() + kContentMarginX, r.top() + 2,
                      r.width() - kContentMarginX * 2, r.height() - 4);

    // checkbox
    const QRectF check(card.left() + 9, card.center().y() - 9, 18, 18);
    if (selected) {
        p->setPen(Qt::NoPen);
        p->setBrush(c.primary);
        p->drawRoundedRect(check, 5, 5);
        icons::paint(p, QStringLiteral("check"),
                     check.adjusted(3.5, 3.5, -3.5, -3.5), c.onPrimary);
    } else {
        QColor border = c.outline;
        border.setAlpha(130);
        p->setPen(QPen(border, 1.4));
        p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(check.adjusted(0.7, 0.7, -0.7, -0.7), 5, 5);
    }

    // avatar + presence
    const QRectF avatarRect(check.right() + 10,
                            card.center().y() - kAvatarSize / 2.0,
                            kAvatarSize, kAvatarSize);
    const QString name = index.data(Qt::DisplayRole).toString();
    const QString username = index.data(RoleUsername).toString();
    const QPixmap avatar = m_avatars.request(
        QUrl(index.data(RoleAvatarUrl).toString()),
        username.isEmpty() ? name : username);
    p->drawPixmap(avatarRect.toRect(), avatar);

    const auto presence = static_cast<ex::model::PresenceType>(
        index.data(RolePresence).toInt());
    if (presence != ex::model::PresenceType::Unknown) {
        QColor dot = presenceColor(c, presence);
        if (presence == ex::model::PresenceType::Offline)
            dot.setAlpha(110);
        const QPointF dotCenter(avatarRect.right() - 2,
                                avatarRect.bottom() - 2);
        p->setPen(QPen(rowBg, 2.5));
        p->setBrush(dot);
        p->drawEllipse(dotCenter, 5, 5);
    }

    // actions reserve
    const QRectF star = starRect(r);
    const QRectF quick = quickRect(r);
    const QString lastUsed = index.data(RoleLastUsed).toString();
    QFont metaFont = fontFor(option.widget, 11, QFont::Normal);
    p->setFont(metaFont);
    const QFontMetrics metaFm(metaFont);

    qreal timeRight = quick.left() - 6;
    if (!lastUsed.isEmpty()) {
        const int w = metaFm.horizontalAdvance(lastUsed);
        p->setPen(c.outline);
        p->drawText(QRectF(timeRight - w, r.top(), w, r.height()),
                    Qt::AlignVCenter | Qt::AlignRight, lastUsed);
        timeRight -= w + 8;
    }

    const int textX = qRound(avatarRect.right()) + 12;
    const int textRight = qMax(textX + 40, qRound(timeRight));
    const int textWidth = qMax(20, textRight - textX);

    QFont nameFont = fontFor(option.widget, 13, QFont::Medium);
    p->setFont(nameFont);
    p->setPen(selected ? c.onSecondaryContainer : c.onSurface);
    p->drawText(QRect(textX, r.top() + 8, textWidth, 18),
                Qt::AlignVCenter | Qt::AlignLeft,
                p->fontMetrics().elidedText(name, Qt::ElideRight, textWidth));

    const QString handle = username.isEmpty() ? QString() : QStringLiteral("@") + username;
    const QString status = presenceText(presence);
    p->setFont(metaFont);
    if (!handle.isEmpty()) {
        int available = textWidth;
        int statusWidth = 0;
        if (!status.isEmpty())
            statusWidth = metaFm.horizontalAdvance(status) + 11;
        available = qMax(20, available - statusWidth);
        const QString shown = metaFm.elidedText(handle, Qt::ElideRight, available);
        p->setPen(c.outline);
        p->drawText(QRect(textX, r.top() + 28, available, 16),
                    Qt::AlignVCenter | Qt::AlignLeft, shown);
        if (!status.isEmpty()) {
            const int x = textX + metaFm.horizontalAdvance(shown) + 5;
            p->setPen(c.outline);
            p->drawText(QRect(x, r.top() + 28, 8, 16),
                        Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("·"));
            p->setPen(presenceColor(c, presence));
            p->drawText(QRect(x + 9, r.top() + 28,
                              textRight - (x + 9), 16),
                        Qt::AlignVCenter | Qt::AlignLeft, status);
        }
    }

    if (hovered) {
        QColor soft = c.accent;
        soft.setAlpha(28);
        p->setPen(Qt::NoPen);
        p->setBrush(soft);
        p->drawRoundedRect(quick.adjusted(1, 1, -1, -1), 8, 8);
        icons::paint(p, QStringLiteral("play-filled"),
                     quick.adjusted(5, 5, -5, -5), c.accent);
    }

    const bool favorite = index.data(RoleFavorite).toBool();
    if (favorite) {
        icons::paint(p, QStringLiteral("star-filled"),
                     star.adjusted(5, 5, -5, -5), c.warning);
    } else {
        QColor starColor = c.outline;
        starColor.setAlpha(hovered ? 220 : 130);
        icons::paint(p, QStringLiteral("star"),
                     star.adjusted(5, 5, -5, -5), starColor);
    }

    p->restore();
}

QSize AccountDelegate::sizeHint(const QStyleOptionViewItem&,
                                const QModelIndex& index) const
{
    const bool isGroup = index.data(RoleGroupId).isValid();
    return {240, isGroup ? kGroupRowHeight : kAccountRowHeight};
}

bool AccountDelegate::editorEvent(QEvent* event, QAbstractItemModel* model,
                                  const QStyleOptionViewItem& option,
                                  const QModelIndex& index)
{
    Q_UNUSED(model);
    if (event->type() != QEvent::MouseButtonRelease)
        return false;
    auto* mouse = static_cast<QMouseEvent*>(event);
    if (mouse->button() != Qt::LeftButton)
        return false;

    const QPoint pos = mouse->position().toPoint();
    if (index.data(RoleGroupId).isValid()) {
        if (groupChevronRect(option.rect).adjusted(-5, -8, 5, 8).contains(pos)) {
            emit groupToggleRequested(index.data(RoleGroupId).toLongLong());
            return true;
        }
        return false;
    }

    const qint64 accountId = index.data(RoleAccountId).toLongLong();
    if (accountId <= 0)
        return false;
    if (starRect(option.rect).contains(pos)) {
        emit favoriteRequested(accountId);
        return true;
    }
    if (quickRect(option.rect).contains(pos)) {
        emit quickLaunchRequested(accountId);
        return true;
    }
    return false;
}

} // namespace ex::ui

#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace ex::ui {

// Headshot avatar cache (GET on avatarUrl — presentation only, the backend
// is left alone). Stores round pixmaps ready to paint; until the picture
// has not arrived, it returns a monogram from the account name.
class AvatarCache : public QObject {
    Q_OBJECT
public:
    explicit AvatarCache(QObject* parent = nullptr);

    // Round pixmap at once: cache, in-flight request, or monogram.
    // Each finished download emits avatarReady (repaint the view).
    [[nodiscard]] QPixmap request(const QUrl& url, const QString& fallbackName);

    // Cache without side effects (for the delegate while repainting).
    [[nodiscard]] QPixmap cachedOrMonogram(const QUrl& url,
                                           const QString& fallbackName) const;

    // Monogram: tone colour derived from the name, letter taken from it.
    [[nodiscard]] static QPixmap monogram(const QString& name);

signals:
    void avatarReady(const QString& url);

private:
    void startDownload(const QUrl& url);

    QNetworkAccessManager* m_nam = nullptr;
    QHash<QString, QPixmap> m_cache;
    QSet<QString> m_pending;
};

} // namespace ex::ui

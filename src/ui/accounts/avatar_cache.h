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

// Кэш аватаров-хедшотов (GET по avatarUrl — только презентация, бекенд
// не трогается). Хранит круглые пиксмапы готовые к отрисовке; пока картинка
// не пришла, отдаёт monogram по имени аккаунта.
class AvatarCache : public QObject {
    Q_OBJECT
public:
    explicit AvatarCache(QObject* parent = nullptr);

    // Круглая пиксмапа немедленно: кэш, идущий запрос или monogram.
    // По завершении каждой загрузки — avatarReady (перерисовать вьюху).
    [[nodiscard]] QPixmap request(const QUrl& url, const QString& fallbackName);

    // Кэш без побочных эффектов (для делегата при перерисовке).
    [[nodiscard]] QPixmap cachedOrMonogram(const QUrl& url,
                                           const QString& fallbackName) const;

    // Monogram: тон-цвет детерминирован именем, буква имени.
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

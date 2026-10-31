#include "ui/accounts/avatar_cache.h"

#include "ui/theme.h"

#include <QCryptographicHash>
#include <QImage>
#include <QImageReader>
#include <QLinearGradient>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>

namespace ex::ui {
namespace {

constexpr int kAvatarPx = 96; // lasts up to ~2.5x HiDPI when painted at 40px

// Rounded square from the downloaded avatar (shape from the mockup).
QPixmap makeRounded(const QImage& image)
{
    const QPixmap square = QPixmap::fromImage(image.scaled(
        kAvatarPx, kAvatarPx, Qt::KeepAspectRatioByExpanding,
        Qt::SmoothTransformation));
    QPixmap rounded(kAvatarPx, kAvatarPx);
    rounded.fill(Qt::transparent);
    QPainter p(&rounded);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, kAvatarPx, kAvatarPx), 28, 28);
    p.setClipPath(clip);
    p.drawPixmap(0, 0, square);
    return rounded;
}

QString initialsFor(const QString& name)
{
    const QStringList words = name.split(QRegularExpression(
        QStringLiteral("[\\s_\\-.]+")), Qt::SkipEmptyParts);
    QString out;
    for (const QString& word : words) {
        if (!word.isEmpty() && out.size() < 2)
            out.append(word.at(0).toUpper());
    }
    if (out.isEmpty()) {
        const QString trimmed = name.trimmed();
        out = trimmed.isEmpty() ? QStringLiteral("?")
                                : trimmed.left(2).toUpper();
    } else if (out.size() == 1 && !words.isEmpty()) {
        // For a name without spaces, take the next capitals (VoidWalker -> VW).
        const QString& word = words.first();
        for (int i = 1; i < word.size(); ++i) {
            if (word.at(i).isUpper()) {
                out.append(word.at(i));
                break;
            }
        }
        if (out.size() == 1 && word.size() > 1)
            out.append(word.at(1).toUpper());
    }
    return out.left(2);
}

} // namespace

AvatarCache::AvatarCache(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

QPixmap AvatarCache::request(const QUrl& url, const QString& fallbackName)
{
    if (url.isEmpty() || !url.isValid())
        return monogram(fallbackName);
    const QString key = url.toString();
    const auto it = m_cache.constFind(key);
    if (it != m_cache.constEnd())
        return it.value();
    if (!m_pending.contains(key))
        startDownload(url);
    return monogram(fallbackName);
}

QPixmap AvatarCache::cachedOrMonogram(const QUrl& url,
                                      const QString& fallbackName) const
{
    const auto it = m_cache.constFind(url.toString());
    return it != m_cache.constEnd() ? it.value() : monogram(fallbackName);
}

QPixmap AvatarCache::monogram(const QString& name)
{
    QPixmap pm(kAvatarPx, kAvatarPx);
    pm.fill(Qt::transparent);

    const QByteArray seed =
        QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Md5);
    const int hue = static_cast<uint8_t>(seed.at(0)) * 360 / 255;
    const bool dark = theme::currentPalette().dark;

    QLinearGradient g(0, 0, kAvatarPx, kAvatarPx);
    g.setColorAt(0.0, QColor::fromHsl(hue, dark ? 70 : 64, dark ? 60 : 60));
    g.setColorAt(1.0, QColor::fromHsl((hue + 42) % 360,
                                      dark ? 72 : 66, dark ? 42 : 46));

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(QRectF(0, 0, kAvatarPx, kAvatarPx), 28, 28);

    QFont f(QStringLiteral("Segoe UI Variable Text"));
    f.setPixelSize(36);
    f.setWeight(QFont::Medium);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRectF(0, 0, kAvatarPx, kAvatarPx), Qt::AlignCenter,
               initialsFor(name));
    return pm;
}

void AvatarCache::startDownload(const QUrl& url)
{
    const QString key = url.toString();
    m_pending.insert(key);

    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_nam->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply, key] {
        reply->deleteLater();
        m_pending.remove(key);

        if (reply->error() == QNetworkReply::NoError) {
            QImage image;
            if (QImageReader(reply).read(&image))
                m_cache.insert(key, makeRounded(image));
        }
        emit avatarReady(key);
    });
}

} // namespace ex::ui

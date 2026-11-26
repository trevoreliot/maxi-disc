// SPDX-License-Identifier: GPL-3.0-or-later
#include "coverimageprovider.h"

#include "tagreader.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QHash>
#include <QLoggingCategory>
#include <QMutex>
#include <QPainter>
#include <QRunnable>
#include <QSaveFile>

#include <algorithm>

Q_LOGGING_CATEGORY(lcCover, "mmm.cover")

namespace {

constexpr int kBuckets[] = {128, 256, 512, 1024};
constexpr int kDefaultBucket = 256;

QImage decode(QIODevice *device, int bucket)
{
    QImageReader reader(device);
    reader.setAutoTransform(true);
    const QSize full = reader.size();
    if (full.isValid() && (full.width() > bucket || full.height() > bucket)) {
        // Let the decoder downscale (much faster for big JPEGs than decode-then-scale).
        reader.setScaledSize(full.scaled(bucket, bucket, Qt::KeepAspectRatio));
    }
    QImage img = reader.read();
    if (img.isNull())
        return img;
    if (img.width() > bucket || img.height() > bucket)
        img = img.scaled(bucket, bucket, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (img.hasAlphaChannel()) {
        // JPEG cache: flatten transparency onto a neutral background.
        QImage flat(img.size(), QImage::Format_RGB32);
        flat.fill(QColor(0x30, 0x30, 0x30));
        QPainter p(&flat);
        p.drawImage(0, 0, img);
        p.end();
        img = flat;
    }
    return img;
}

class CoverResponse;

class CoverRunnable : public QObject, public QRunnable
{
    Q_OBJECT
public:
    CoverRunnable(QString id, QSize size, QString cacheDir, bool round)
        : m_id(std::move(id)), m_size(size), m_cacheDir(std::move(cacheDir)), m_round(round)
    {
        setAutoDelete(true);
    }
    void run() override
    {
        const QImage image = CoverLoader::load(m_id, m_size, m_cacheDir);
        Q_EMIT done(m_round ? CoverLoader::round(image) : image);
    }

Q_SIGNALS:
    void done(const QImage &image);

private:
    QString m_id;
    QSize m_size;
    QString m_cacheDir;
    bool m_round;
};

class CoverResponse : public QQuickImageResponse
{
    Q_OBJECT
public:
    CoverResponse(const QString &id, const QSize &size, const QString &cacheDir, bool round, QThreadPool *pool)
    {
        auto *runnable = new CoverRunnable(id, size, cacheDir, round);
        // Queued: if QML deletes this response first, the connection simply goes away.
        m_connection = connect(runnable, &CoverRunnable::done, this, &CoverResponse::onDone, Qt::QueuedConnection);
        pool->start(runnable); // the pool owns and deletes the runnable
    }

    QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    QString errorString() const override { return m_image.isNull() ? QStringLiteral("no cover art") : QString(); }

    void cancel() override
    {
        // The runnable may already be gone, so never touch it: just drop its result.
        // A cancelled response must still emit finished() so the engine can clean up.
        disconnect(m_connection);
        finish();
    }

private:
    void onDone(const QImage &image)
    {
        if (m_finished)
            return; // cancelled while the result was already queued
        m_image = image;
        finish();
    }

    void finish()
    {
        if (m_finished)
            return;
        m_finished = true;
        Q_EMIT finished();
    }

    QMetaObject::Connection m_connection;
    QImage m_image;
    bool m_finished = false;
};

} // namespace

int CoverLoader::bucketFor(const QSize &requested)
{
    const int want = std::max(requested.width(), requested.height());
    if (want <= 0)
        return kDefaultBucket;
    for (const int b : kBuckets) {
        if (want <= b)
            return b;
    }
    return kBuckets[std::size(kBuckets) - 1];
}

namespace {
QMutex s_revisionMutex;
QHash<qint64, int> s_revisions;
} // namespace

void CoverLoader::bumpRevision(qint64 key)
{
    const QMutexLocker lock(&s_revisionMutex);
    ++s_revisions[key];
}

QString CoverLoader::url(qint64 key, const QString &folderCover, const QString &artTrack)
{
    if (folderCover.isEmpty() && artTrack.isEmpty())
        return {};
    int revision = 0;
    {
        const QMutexLocker lock(&s_revisionMutex);
        revision = s_revisions.value(key);
    }
    const QByteArray payload = (folderCover + QLatin1Char('\n') + artTrack).toUtf8();
    return QStringLiteral("image://cover/%1/%2")
        .arg(revision > 0 ? QStringLiteral("%1.%2").arg(key).arg(revision) : QString::number(key))
        .arg(QString::fromLatin1(payload.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
}

QString CoverLoader::roundUrl(const QString &coverUrl)
{
    static const QString square = QStringLiteral("image://cover/");
    if (!coverUrl.startsWith(square))
        return {};
    return QStringLiteral("image://coverround/") + QStringView(coverUrl).mid(square.size());
}

QString CoverLoader::key(const QString &coverUrl)
{
    static const QString square = QStringLiteral("image://cover/");
    if (!coverUrl.startsWith(square))
        return {};
    const qsizetype slash = coverUrl.indexOf(QLatin1Char('/'), square.size());
    return slash < 0 ? QString() : coverUrl.mid(square.size(), slash - square.size());
}

QImage CoverLoader::round(const QImage &image)
{
    if (image.isNull())
        return {};
    const int side = std::min(image.width(), image.height());
    const QImage square = image.copy((image.width() - side) / 2, (image.height() - side) / 2, side, side);
    QImage out(side, side, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QBrush(square)); // the brush's texture starts at (0, 0), like the ellipse
    p.drawEllipse(QRectF(0, 0, side, side));
    p.end();
    return out;
}

std::pair<QString, QString> CoverLoader::decodeId(const QString &id)
{
    const qsizetype slash = id.indexOf(QLatin1Char('/'));
    if (slash < 0)
        return {};
    const QByteArray payload = QByteArray::fromBase64(id.mid(slash + 1).toLatin1(),
                                                      QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    const QString text = QString::fromUtf8(payload);
    const qsizetype nl = text.indexOf(QLatin1Char('\n'));
    if (nl < 0)
        return {text, QString()};
    return {text.left(nl), text.mid(nl + 1)};
}

QImage CoverLoader::load(const QString &id, const QSize &requested, const QString &cacheDir)
{
    const auto [folderCover, artTrack] = decodeId(id);
    const int bucket = bucketFor(requested);

    // Pick the source that exists: folder art beats embedded.
    QFileInfo source;
    bool embedded = false;
    if (!folderCover.isEmpty() && QFileInfo::exists(folderCover)) {
        source = QFileInfo(folderCover);
    } else if (!artTrack.isEmpty() && QFileInfo::exists(artTrack)) {
        source = QFileInfo(artTrack);
        embedded = true;
    } else {
        return {};
    }

    const QByteArray key = QCryptographicHash::hash(
                               QStringLiteral("%1|%2|%3|%4|%5")
                                   .arg(source.absoluteFilePath())
                                   .arg(source.lastModified().toMSecsSinceEpoch())
                                   .arg(source.size())
                                   .arg(bucket)
                                   .arg(embedded ? 1 : 0)
                                   .toUtf8(),
                               QCryptographicHash::Sha1)
                               .toHex();
    const QString shard = QString::fromLatin1(key.left(2));
    const QString cachePath = QDir(cacheDir).filePath(shard + QLatin1Char('/') + QString::fromLatin1(key) + QStringLiteral(".jpg"));

    if (QFileInfo::exists(cachePath)) {
        QImage cached(cachePath);
        if (!cached.isNull())
            return cached;
    }

    QImage img;
    if (embedded) {
        const auto cover = mmm::TagReader::readCover(artTrack);
        if (!cover)
            return {};
        QBuffer buf;
        buf.setData(cover->data);
        buf.open(QIODevice::ReadOnly);
        img = decode(&buf, bucket);
    } else {
        QFile f(folderCover);
        if (f.open(QIODevice::ReadOnly))
            img = decode(&f, bucket);
    }
    if (img.isNull()) {
        qCDebug(lcCover) << "undecodable cover art in" << source.filePath();
        return {};
    }

    if (QDir().mkpath(QDir(cacheDir).filePath(shard))) {
        QSaveFile out(cachePath);
        if (!(out.open(QIODevice::WriteOnly) && img.save(&out, "JPEG", 88) && out.commit()))
            qCDebug(lcCover) << "cannot write cover cache" << cachePath;
    }
    return img;
}

CoverImageProvider::CoverImageProvider(QString cacheDir, Shape shape)
    : m_cacheDir(std::move(cacheDir))
    , m_shape(shape)
{
    // Decoding is CPU-bound; leave cores for the UI and the scanner.
    m_pool.setMaxThreadCount(std::clamp(QThread::idealThreadCount() / 2, 2, 4));
}

CoverImageProvider::~CoverImageProvider()
{
    m_pool.clear();
    m_pool.waitForDone();
}

QQuickImageResponse *CoverImageProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    return new CoverResponse(id, requestedSize, m_cacheDir, m_shape == Shape::Round, &m_pool);
}

#include "coverimageprovider.moc"

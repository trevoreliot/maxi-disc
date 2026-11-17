// SPDX-License-Identifier: GPL-3.0-or-later
#include "coverimageprovider.h"

#include "tagreader.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QLoggingCategory>
#include <QPainter>
#include <QRunnable>
#include <QSaveFile>

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
    CoverRunnable(QString id, QSize size, QString cacheDir)
        : m_id(std::move(id)), m_size(size), m_cacheDir(std::move(cacheDir))
    {
        setAutoDelete(true);
    }
    void run() override { Q_EMIT done(CoverLoader::load(m_id, m_size, m_cacheDir)); }

Q_SIGNALS:
    void done(const QImage &image);

private:
    QString m_id;
    QSize m_size;
    QString m_cacheDir;
};

class CoverResponse : public QQuickImageResponse
{
    Q_OBJECT
public:
    CoverResponse(const QString &id, const QSize &size, const QString &cacheDir, QThreadPool *pool)
    {
        auto *runnable = new CoverRunnable(id, size, cacheDir);
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

CoverImageProvider::CoverImageProvider(QString cacheDir)
    : m_cacheDir(std::move(cacheDir))
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
    return new CoverResponse(id, requestedSize, m_cacheDir, &m_pool);
}

#include "coverimageprovider.moc"

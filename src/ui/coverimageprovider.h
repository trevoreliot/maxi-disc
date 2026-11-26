// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QImage>
#include <QQuickAsyncImageProvider>
#include <QSize>
#include <QString>
#include <QThreadPool>

// Decodes, resizes and disk-caches album art. Stateless apart from the cache directory;
// safe to call from any thread.
class CoverLoader
{
public:
    // Thumbnails are cached at a few fixed sizes so different views share cache entries.
    static int bucketFor(const QSize &requested);

    // `id` is the part after "image://cover/" (see AlbumListModel::coverUrl).
    // Tries folder art first, then the embedded cover of the art track. Returns a null
    // image when there's no art. Results are cached in `cacheDir` keyed on the source
    // file's path, mtime and size, so changed art is picked up automatically.
    static QImage load(const QString &id, const QSize &requested, const QString &cacheDir);

    // Folder cover path and art track path encoded in an id.
    static std::pair<QString, QString> decodeId(const QString &id);

    // "image://cover/<key>[.<revision>]/<base64url(folderCover \n artTrack)>", or "" when both
    // are empty. `key` (the album id) only keeps URLs of different albums apart in QML's image cache.
    static QString url(qint64 key, const QString &folderCover, const QString &artTrack);
    // Q4, the transport bar's disc: the same picture from the "coverround" provider (cut round),
    // and the part of a cover URL that only changes with the picture ("<key>[.<revision>]"; the
    // rest includes the track's path when the art is embedded). Both "" for "".
    static QString roundUrl(const QString &coverUrl);
    static QString key(const QString &coverUrl);
    // `image` centre-cropped to a square and cut to a smooth-edged circle (transparent corners).
    static QImage round(const QImage &image);
    // Call after an album's art changed in place (same files, new pictures): its URLs change,
    // so QML loads the new picture instead of its cached one. Thread-safe.
    static void bumpRevision(qint64 key);
};

// QML image provider "cover": image://cover/<id>. Loads on its own thread pool. With
// Shape::Round (registered as "coverround") the picture comes cut round (CoverLoader::round),
// from the same (square) cache.
class CoverImageProvider : public QQuickAsyncImageProvider
{
public:
    enum class Shape { Square, Round };
    explicit CoverImageProvider(QString cacheDir, Shape shape = Shape::Square);
    ~CoverImageProvider() override;

    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

private:
    QString m_cacheDir;
    Shape m_shape;
    QThreadPool m_pool;
};

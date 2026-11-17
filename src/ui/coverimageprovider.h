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
};

// QML image provider "cover": image://cover/<id>. Loads on its own thread pool.
class CoverImageProvider : public QQuickAsyncImageProvider
{
public:
    explicit CoverImageProvider(QString cacheDir);
    ~CoverImageProvider() override;

    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

private:
    QString m_cacheDir;
    QThreadPool m_pool;
};

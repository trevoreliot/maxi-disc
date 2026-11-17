// SPDX-License-Identifier: GPL-3.0-or-later
#include "artcache.h"

#include "libraryquery.h"
#include "tagreader.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

namespace mmm::ArtCache {

QString fileFor(const TrackRow &track, const QString &cacheDir)
{
    if (!track.coverPath.isEmpty() && QFileInfo::exists(track.coverPath))
        return track.coverPath;
    if (!track.hasEmbeddedArt)
        return {};

    const QFileInfo src(track.path);
    if (!src.exists())
        return {};
    const QByteArray key = QCryptographicHash::hash(QStringLiteral("%1|%2|%3")
                                                        .arg(src.absoluteFilePath())
                                                        .arg(src.lastModified().toMSecsSinceEpoch())
                                                        .arg(src.size())
                                                        .toUtf8(),
                                                    QCryptographicHash::Sha1)
                               .toHex();
    const QDir dir(cacheDir);
    for (const char *ext : {".jpg", ".png"}) {
        const QString existing = dir.filePath(QString::fromLatin1(key) + QLatin1String(ext));
        if (QFileInfo::exists(existing))
            return existing;
    }

    const auto cover = TagReader::readCover(track.path);
    if (!cover || !QDir().mkpath(cacheDir))
        return {};
    const bool png = cover->mimeType.contains(QLatin1String("png")) || cover->data.startsWith("\x89PNG");
    const QString out = dir.filePath(QString::fromLatin1(key) + (png ? QStringLiteral(".png") : QStringLiteral(".jpg")));
    QSaveFile f(out);
    if (!f.open(QIODevice::WriteOnly) || f.write(cover->data) != cover->data.size() || !f.commit())
        return {};
    return out;
}

} // namespace mmm::ArtCache

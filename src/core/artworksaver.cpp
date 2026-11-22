// SPDX-License-Identifier: GPL-3.0-or-later
#include "artworksaver.h"

#include "database.h"
#include "libraryquery.h"

#include <QDir>
#include <QSaveFile>

namespace mmm {

QString ArtworkSaver::imageMimeType(QByteArrayView data)
{
    if (data.startsWith("\xFF\xD8\xFF"))
        return QStringLiteral("image/jpeg");
    if (data.startsWith("\x89PNG\r\n\x1A\n"))
        return QStringLiteral("image/png");
    return {};
}

ArtworkSaveResult ArtworkSaver::save(Database &db, qint64 albumId, const QByteArray &image)
{
    ArtworkSaveResult r;
    const QString mime = imageMimeType(image);
    if (mime.isEmpty()) {
        r.error = QStringLiteral("the downloaded file isn't a JPEG or PNG image");
        return r;
    }
    if (image.size() > kMaxImageBytes) {
        r.error = QStringLiteral("the image is too big (%1 MB)").arg(image.size() / (1024 * 1024));
        return r;
    }

    const LibraryQuery query(db);
    const QList<TrackRow> tracks = query.albumTracks(albumId);
    if (tracks.isEmpty()) {
        r.error = QStringLiteral("the album is no longer in the library");
        return r;
    }

    const QString fileName = mime == QLatin1String("image/png") ? QStringLiteral("cover.png") : QStringLiteral("cover.jpg");
    for (const AlbumFolder &folder : query.albumFolders(albumId)) {
        if (folder.shared) {
            r.warnings << QStringLiteral("%1 holds other albums too: no %2 written there").arg(folder.path, fileName);
            continue;
        }
        const QString path = QDir(folder.path).filePath(fileName);
        QSaveFile out(path);
        if (out.open(QIODevice::WriteOnly) && out.write(image) == image.size() && out.commit())
            r.coverFiles << path;
        else
            r.warnings << QStringLiteral("cannot write %1: %2").arg(path, out.errorString());
    }

    QStringList paths;
    for (const TrackRow &t : tracks)
        paths << t.path;
    TagChanges changes;
    changes.frontCover = EmbeddedPicture{image, mime, QStringLiteral("Front Cover"), QString()};
    r.edit = editTracks(db, paths, changes);
    for (const QString &f : std::as_const(r.edit.failures))
        r.warnings << f;

    r.ok = !r.coverFiles.isEmpty() || !r.edit.written.isEmpty();
    if (!r.ok) {
        r.error = r.warnings.isEmpty() ? QStringLiteral("nothing could be written") : r.warnings.front();
        return r;
    }
    // Explicit, so it wins over whatever folder art the scanner had picked (and survives
    // its next pass: setFolderCover only fills albums that have none).
    if (!r.coverFiles.isEmpty())
        db.setAlbumCover(albumId, r.coverFiles.front());
    else
        db.setAlbumArtSkipped(albumId, false);
    return r;
}

} // namespace mmm

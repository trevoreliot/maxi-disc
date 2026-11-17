// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixturelibrary.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QSqlQuery>

#include <taglib/fileref.h>

#include "../core/testmedia.h"
#include "database.h"

namespace mmm::test {

namespace {

struct Track
{
    const char *path; // relative to dir
    const char *title;
    const char *artist;
    const char *album;
    const char *albumArtist;
    const char *genre;
    int year;
    int disc;
    int track;
};

const Track kTracks[] = {
    {"Radiohead/OK Computer/01.flac", "Airbag", "Radiohead", "OK Computer", "", "Rock", 1997, 1, 1},
    {"Radiohead/OK Computer/02.flac", "Paranoid Android", "Radiohead", "OK Computer", "", "Rock", 1997, 1, 2},
    {"Radiohead/OK Computer/03.flac", "Subterranean Homesick Alien", "Radiohead", "OK Computer", "", "Rock", 1997, 1, 3},
    {"Radiohead/Kid A/01.flac", "Everything in Its Right Place", "Radiohead", "Kid A", "", "Rock", 2000, 1, 1},
    {"Radiohead/Kid A/02.flac", "Kid A", "Radiohead", "Kid A", "", "Rock", 2000, 1, 2},
    {"Beatles/Abbey Road/01.flac", "Come Together", "The Beatles", "Abbey Road", "", "Rock", 1969, 1, 1},
    {"Beatles/Abbey Road/02.flac", "Something", "The Beatles", "Abbey Road", "", "Rock", 1969, 1, 2},
    {"Pink Floyd/The Wall/d2-01.flac", "Hey You", "Pink Floyd", "The Wall", "", "Rock", 1979, 2, 1},
    {"Pink Floyd/The Wall/d1-02.flac", "The Thin Ice", "Pink Floyd", "The Wall", "", "Rock", 1979, 1, 2},
    {"Pink Floyd/The Wall/d1-01.flac", "In the Flesh?", "Pink Floyd", "The Wall", "", "Rock", 1979, 1, 1},
    {"Now/01.mp3", "Song A", "Artist A", "Now 42", "Various Artists", "Pop", 1999, 1, 1},
    {"Now/02.mp3", "Song B", "Artist B", "Now 42", "Various Artists", "Pop", 1999, 1, 2},
};

QByteArray png(int w, int h, QColor color)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(color);
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return bytes;
}

} // namespace

QString buildFixtureLibrary(const QString &dir)
{
    const QString dbPath = QDir(dir).filePath(QStringLiteral("library.db"));
    QFile::remove(dbPath);

    // Folder art for OK Computer.
    const QString okDir = QDir(dir).filePath(QStringLiteral("Radiohead/OK Computer"));
    QDir().mkpath(okDir);
    const QString okCover = okDir + QStringLiteral("/cover.png");
    {
        QFile f(okCover);
        if (!f.open(QIODevice::WriteOnly) || f.write(png(300, 300, QColor(200, 40, 40))) <= 0) {
            qWarning("fixture: cannot write %s", qPrintable(okCover));
            return {};
        }
    }

    // A real audio file with embedded art for Kid A's first track.
    const QString kidA = QDir(dir).filePath(QStringLiteral("Radiohead/Kid A/01.flac"));
    QDir().mkpath(QFileInfo(kidA).absolutePath());
    if (const QString err = generateTone(kidA, QStringLiteral("flacenc"), 200); !err.isEmpty()) {
        qWarning("fixture: %s", qPrintable(err));
        return {};
    }
    {
        const QByteArray name = QFile::encodeName(kidA);
        TagLib::FileRef ref(name.constData());
        const QByteArray bytes = png(200, 100, QColor(40, 40, 200));
        TagLib::VariantMap pic;
        pic.insert("data", TagLib::ByteVector(bytes.constData(), static_cast<unsigned int>(bytes.size())));
        pic.insert("mimeType", TagLib::String("image/png"));
        pic.insert("pictureType", TagLib::String("Front Cover"));
        if (ref.isNull() || !ref.setComplexProperties("PICTURE", {pic}) || !ref.save()) {
            qWarning("fixture: cannot embed art in %s", qPrintable(kidA));
            return {};
        }
    }

    Database db;
    QString error;
    if (!db.open(dbPath, &error)) {
        qWarning("fixture: %s", qPrintable(error));
        return {};
    }
    for (const Track &t : kTracks) {
        TrackTags tags;
        tags.title = QString::fromUtf8(t.title);
        tags.artist = QString::fromUtf8(t.artist);
        tags.album = QString::fromUtf8(t.album);
        tags.albumArtist = QString::fromUtf8(t.albumArtist);
        tags.genre = QString::fromUtf8(t.genre);
        tags.year = t.year;
        tags.discNumber = t.disc;
        tags.trackNumber = t.track;
        tags.durationMs = 180000 + t.track * 1000;
        tags.codec = QStringLiteral("FLAC");
        const QString path = QDir(dir).filePath(QString::fromUtf8(t.path));
        tags.hasEmbeddedArt = path == kidA;
        if (!db.upsertTrack(path, 1, 1, tags, &error)) {
            qWarning("fixture: %s", qPrintable(error));
            return {};
        }
    }
    if (!db.addLibraryFolder(QDir(dir).canonicalPath(), &error)) { // so the app shows the browser
        qWarning("fixture: %s", qPrintable(error));
        return {};
    }
    if (!db.setFolderCover(okDir, okCover)) {
        qWarning("fixture: cannot set folder cover");
        return {};
    }
    return dbPath;
}

} // namespace mmm::test

// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include "artworksaver.h"
#include "database.h"
#include "libraryquery.h"
#include "tagreader.h"
#include "tagwriter.h"
#include "testmedia.h"

using mmm::ArtworkSaver;

namespace {

QByteArray fakeJpeg(char fill)
{
    return QByteArray("\xFF\xD8\xFF\xE0", 4) + QByteArray(1000, fill) + QByteArray("\xFF\xD9", 2);
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class TestArtworkSaver : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_source; // a generated, tagged FLAC
    QString m_root;
    mmm::Database m_db;
    qint64 m_folderId = 0;

    // Adds a track (a copy of m_source) at root/relPath with these tags to the library.
    QString addTrack(const QString &relPath, const QString &album, int disc = 1, int track = 1)
    {
        const QString path = m_root + QLatin1Char('/') + relPath;
        QDir().mkpath(QFileInfo(path).path());
        if (!QFile::copy(m_source, path))
            return {};
        // In the file too: saving art re-reads the tracks.
        mmm::TagChanges c;
        c.album = album;
        c.discNumber = disc;
        c.trackNumber = track;
        if (!mmm::TagWriter::write(path, c))
            return {};
        const auto tags = mmm::TagReader::read(path);
        if (!tags)
            return {};
        const QFileInfo fi(path);
        if (!m_db.upsertTrack(mmm::TrackFile{path, fi.lastModified().toMSecsSinceEpoch(), fi.size(), m_folderId}, *tags))
            return {};
        return path;
    }

    qint64 albumId(const QString &title)
    {
        for (const auto &a : mmm::LibraryQuery(m_db).albums()) {
            if (a.title == title)
                return a.id;
        }
        return 0;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_source = m_dir.filePath(QStringLiteral("source.flac"));
        const QString err = mmm::test::generateTone(m_source, QStringLiteral("flacenc"), 300,
                                                    QStringLiteral("title=\"Song\",artist=\"Artist\""));
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QDir().mkpath(m_dir.filePath(QStringLiteral("lib")));
        m_root = QFileInfo(m_dir.filePath(QStringLiteral("lib"))).canonicalFilePath();
        QVERIFY(m_db.open(QStringLiteral(":memory:")));
        const auto folder = m_db.addLibraryFolder(m_root);
        QVERIFY(folder);
        m_folderId = *folder;
    }

    void imageMimeType()
    {
        QCOMPARE(ArtworkSaver::imageMimeType(fakeJpeg('x')), QStringLiteral("image/jpeg"));
        QCOMPARE(ArtworkSaver::imageMimeType(QByteArray("\x89PNG\r\n\x1A\n....", 12)), QStringLiteral("image/png"));
        QCOMPARE(ArtworkSaver::imageMimeType(QByteArray("<html>busy</html>")), QString());
        QCOMPARE(ArtworkSaver::imageMimeType(QByteArray()), QString());
    }

    // One folder, one album: cover.jpg + embedded in every track + the library knows.
    void savesFolderFileAndEmbeds()
    {
        const QString a = addTrack(QStringLiteral("Solo/01.flac"), QStringLiteral("Solo"), 1, 1);
        const QString b = addTrack(QStringLiteral("Solo/02.flac"), QStringLiteral("Solo"), 1, 2);
        QVERIFY(!a.isEmpty() && !b.isEmpty());
        const qint64 album = albumId(QStringLiteral("Solo"));
        QVERIFY(album);
        QCOMPARE(mmm::LibraryQuery(m_db).albumsMissingArt().size(), 1);
        QVERIFY(m_db.setAlbumArtSkipped(album, true)); // saving clears an earlier skip

        const auto r = ArtworkSaver::save(m_db, album, fakeJpeg('s'));
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.warnings.isEmpty());
        const QString cover = m_root + QStringLiteral("/Solo/cover.jpg");
        QCOMPARE(r.coverFiles, QStringList{cover});
        QCOMPARE(readAll(cover), fakeJpeg('s'));
        QCOMPARE(r.edit.written, (QStringList{a, b}));
        QCOMPARE(r.edit.tracks.size(), 2);
        for (const QString &p : {a, b})
            QCOMPARE(mmm::TagReader::readCover(p)->data, fakeJpeg('s'));

        const auto target = mmm::LibraryQuery(m_db).artworkTarget(album);
        QVERIFY(target->hasArt);
        QVERIFY(!target->skipped);
        QCOMPARE(mmm::LibraryQuery(m_db).albums().front().coverPath, cover);
        for (const auto &t : r.edit.tracks)
            QVERIFY(t.hasEmbeddedArt);
        QVERIFY(mmm::LibraryQuery(m_db).albumsMissingArt(true).isEmpty());

        // Replacing: the file is overwritten, the tracks get the new picture.
        const auto again = ArtworkSaver::save(m_db, album, fakeJpeg('t'));
        QVERIFY(again.ok);
        QCOMPARE(readAll(cover), fakeJpeg('t'));
        QCOMPARE(mmm::TagReader::readPictures(a).size(), 1);
        QCOMPARE(mmm::TagReader::readCover(b)->data, fakeJpeg('t'));
    }

    // Multi-disc album in two folders: a cover file in each; the shared folder gets none.
    void foldersSharedWithOtherAlbumsGetNoFile()
    {
        const QString d1 = addTrack(QStringLiteral("Big/CD1/01.flac"), QStringLiteral("Big"), 1, 1);
        const QString d2 = addTrack(QStringLiteral("Big/CD2/01.flac"), QStringLiteral("Big"), 2, 1);
        const QString mixed = addTrack(QStringLiteral("Mixed/01.flac"), QStringLiteral("Big"), 3, 1);
        const QString other = addTrack(QStringLiteral("Mixed/02.flac"), QStringLiteral("Other"), 1, 1);
        QVERIFY(!d1.isEmpty() && !d2.isEmpty() && !mixed.isEmpty() && !other.isEmpty());
        const qint64 album = albumId(QStringLiteral("Big"));

        const auto r = ArtworkSaver::save(m_db, album, fakeJpeg('m'));
        QVERIFY(r.ok);
        QCOMPARE(r.coverFiles, (QStringList{m_root + QStringLiteral("/Big/CD1/cover.jpg"),
                                            m_root + QStringLiteral("/Big/CD2/cover.jpg")}));
        QVERIFY(!QFileInfo::exists(m_root + QStringLiteral("/Mixed/cover.jpg")));
        QCOMPARE(r.warnings.size(), 1);
        QVERIFY(r.warnings.front().contains(QStringLiteral("holds other albums")));
        QCOMPARE(r.edit.written.size(), 3); // every track of the album is embedded, wherever it is
        QCOMPARE(mmm::TagReader::readCover(mixed)->data, fakeJpeg('m'));
        QVERIFY(!mmm::TagReader::readCover(other)); // the other album is untouched
        QCOMPARE(mmm::LibraryQuery(m_db).artworkTarget(album)->hasArt, true);
    }

    void pngIsSavedAsCoverPng()
    {
        const QString t = addTrack(QStringLiteral("Png/01.flac"), QStringLiteral("Png"));
        QVERIFY(!t.isEmpty());
        const QByteArray png = QByteArray("\x89PNG\r\n\x1A\n", 8) + QByteArray(500, 'p');
        const auto r = ArtworkSaver::save(m_db, albumId(QStringLiteral("Png")), png);
        QVERIFY(r.ok);
        QCOMPARE(r.coverFiles, QStringList{m_root + QStringLiteral("/Png/cover.png")});
        QCOMPARE(mmm::TagReader::readCover(t)->mimeType, QStringLiteral("image/png"));
    }

    void rejectsBadInput()
    {
        const QString t = addTrack(QStringLiteral("Bad/01.flac"), QStringLiteral("Bad"));
        QVERIFY(!t.isEmpty());
        const qint64 album = albumId(QStringLiteral("Bad"));
        const QByteArray before = readAll(t);

        auto r = ArtworkSaver::save(m_db, album, QByteArray("<html>Service unavailable</html>"));
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QStringLiteral("isn't a JPEG or PNG")));
        QVERIFY(!QFileInfo::exists(m_root + QStringLiteral("/Bad/cover.jpg")));
        QCOMPARE(readAll(t), before);

        r = ArtworkSaver::save(m_db, 999999, fakeJpeg('x'));
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QStringLiteral("no longer in the library")));
    }

    void readOnlyFolderStillEmbedsWhereItCan()
    {
        const QString t = addTrack(QStringLiteral("Locked/01.flac"), QStringLiteral("Locked"));
        QVERIFY(!t.isEmpty());
        const QString dir = QFileInfo(t).path();
        QVERIFY(QFile::setPermissions(dir, QFile::ReadOwner | QFile::ExeOwner));
        const auto r = ArtworkSaver::save(m_db, albumId(QStringLiteral("Locked")), fakeJpeg('l'));
        QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        // Neither a cover file nor a tag edit (it needs a temp file in the folder) can be written.
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
        QVERIFY(r.coverFiles.isEmpty());
        QVERIFY(mmm::LibraryQuery(m_db).artworkTarget(albumId(QStringLiteral("Locked")))->hasArt == false);
    }
};

QTEST_GUILESS_MAIN(TestArtworkSaver)
#include "tst_artworksaver.moc"

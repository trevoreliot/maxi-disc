// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <taglib/fileref.h>
#include <taglib/id3v1tag.h>
#include <taglib/id3v2header.h>
#include <taglib/id3v2tag.h>
#include <taglib/mpegfile.h>
#include <taglib/tpropertymap.h>

#include <sys/stat.h>

#include "database.h"
#include "libraryquery.h"
#include "tagreader.h"
#include "tagwriter.h"
#include "testmedia.h"

using mmm::TagChanges;
using mmm::TagReader;
using mmm::TagWriter;

namespace {

struct Format
{
    const char *name;
    const char *file;
    const char *encoder;
};

// Every v1 playback format (PLAN.md scope), as in tst_tagreader.
constexpr Format kFormats[] = {
    {"flac", "t.flac", "flacenc"},
    {"mp3", "t.mp3", "lamemp3enc target=bitrate bitrate=320 cbr=true ! id3v2mux"},
    {"wav", "t.wav", "wavenc"},
    {"aac", "t-aac.m4a", "avenc_aac ! mp4mux"},
    {"alac", "t-alac.m4a", "avenc_alac ! mp4mux"},
    {"vorbis", "t.ogg", "vorbisenc ! oggmux"},
    {"opus", "t.opus", "opusenc ! oggmux"},
};

TagLib::String ts(const char *s) { return TagLib::String(s, TagLib::String::UTF8); }

// Base tags written with TagLib (GStreamer's mp4mux tags aren't readable by TagLib).
bool writeBaseTags(const QString &path)
{
    TagLib::PropertyMap props;
    props.replace("TITLE", ts("Old Title"));
    props.replace("ARTIST", ts("Old Artist"));
    props.replace("ALBUM", ts("Old Album"));
    props.replace("GENRE", ts("Jazz"));
    props.replace("DATE", ts("1997-09-22"));
    props.replace("TRACKNUMBER", ts("2/10"));
    props.replace("DISCNUMBER", ts("1/2"));
    props.replace("REPLAYGAIN_TRACK_GAIN", ts("-7.25 dB"));
    props.replace("MUSICBRAINZ_ALBUMID", ts("1f9b5d30-5a67-3b5d-9a2c-1f7f2f9e8f00"));
    const QByteArray name = QFile::encodeName(path);
    TagLib::FileRef ref(name.constData());
    if (ref.isNull())
        return false;
    ref.setProperties(props);
    return ref.save();
}

TagLib::PropertyMap rawProperties(const QString &path)
{
    const QByteArray name = QFile::encodeName(path);
    TagLib::FileRef ref(name.constData(), false);
    return ref.isNull() ? TagLib::PropertyMap() : ref.properties();
}

QString raw(const QString &path, const char *key)
{
    const TagLib::PropertyMap map = rawProperties(path);
    const auto it = map.find(key);
    return it == map.end() || it->second.isEmpty() ? QString()
                                                   : QString::fromStdString(it->second.front().to8Bit(true));
}

QStringList leftovers(const QString &dir)
{
    return QDir(dir).entryList({QStringLiteral(".mmm-edit-*")}, QDir::Files | QDir::Hidden);
}

// Not a real image: tags store the bytes as they are, which is all these tests check.
QByteArray fakeJpeg(char fill)
{
    return QByteArray("\xFF\xD8\xFF\xE0", 4) + QByteArray(2000, fill) + QByteArray("\xFF\xD9", 2);
}

mmm::EmbeddedPicture picture(const QByteArray &data, const char *type = "Front Cover")
{
    return mmm::EmbeddedPicture{data, QStringLiteral("image/jpeg"), QString::fromLatin1(type), QString()};
}

ino_t inodeOf(const QString &path)
{
    struct stat st{};
    return ::stat(QFile::encodeName(path).constData(), &st) == 0 ? st.st_ino : 0;
}

} // namespace

class TestTagWriter : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    int m_copies = 0;

    QString pathFor(const char *file) const { return m_dir.filePath(QString::fromLatin1(file)); }

    // A fresh copy of a generated file with the base tags, in its own folder.
    QString taggedCopy(const char *file)
    {
        const QString dir = m_dir.filePath(QStringLiteral("c%1").arg(++m_copies));
        QDir().mkpath(dir);
        const QString dst = dir + QLatin1Char('/') + QString::fromLatin1(file);
        if (!QFile::copy(pathFor(file), dst) || !writeBaseTags(dst))
            return {};
        return dst;
    }

    static void addFormatRows()
    {
        QTest::addColumn<int>("index");
        for (int i = 0; i < static_cast<int>(std::size(kFormats)); ++i)
            QTest::newRow(kFormats[i].name) << i;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        for (const Format &f : kFormats) {
            const QString err = mmm::test::generateTone(pathFor(f.file), QString::fromLatin1(f.encoder), 500);
            QVERIFY2(err.isEmpty(), qPrintable(QStringLiteral("%1: %2").arg(QLatin1String(f.name), err)));
        }
    }

    void fullEdit_data() { addFormatRows(); }
    void fullEdit()
    {
        QFETCH(int, index);
        const QString path = taggedCopy(kFormats[index].file);
        QVERIFY(!path.isEmpty());
        const auto before = TagReader::read(path);
        QVERIFY(before);

        TagChanges c;
        c.title = QStringLiteral("Jóga");
        c.artist = QStringLiteral("Björk");
        c.album = QStringLiteral("Homogenic");
        c.albumArtist = QStringLiteral("Björk");
        c.genre = QStringLiteral("Electronic");
        c.composer = QStringLiteral("Björk Guðmundsdóttir");
        c.comment = QStringLiteral("edited");
        c.year = 1998;
        c.trackNumber = 3;
        c.trackTotal = 12;
        c.discNumber = 2;
        c.discTotal = 3;
        c.compilation = true;
        QString error;
        QVERIFY2(TagWriter::write(path, c, &error), qPrintable(error));

        const auto t = TagReader::read(path, &error);
        QVERIFY2(t, qPrintable(error));
        QCOMPARE(t->title, QStringLiteral("Jóga"));
        QCOMPARE(t->artist, QStringLiteral("Björk"));
        QCOMPARE(t->album, QStringLiteral("Homogenic"));
        QCOMPARE(t->albumArtist, QStringLiteral("Björk"));
        QCOMPARE(t->genre, QStringLiteral("Electronic"));
        QCOMPARE(t->composer, QStringLiteral("Björk Guðmundsdóttir"));
        QCOMPARE(t->comment, QStringLiteral("edited"));
        QCOMPARE(t->year, 1998);
        QCOMPARE(t->trackNumber, 3);
        QCOMPARE(t->trackTotal, 12);
        QCOMPARE(t->discNumber, 2);
        QCOMPARE(t->discTotal, 3);
        QVERIFY(t->compilation);
        // Untouched tags and the audio survive.
        QCOMPARE(t->replayGainTrackGain, std::optional<double>(-7.25));
        QCOMPARE(t->musicBrainzAlbumId, QStringLiteral("1f9b5d30-5a67-3b5d-9a2c-1f7f2f9e8f00"));
        QCOMPARE(t->codec, before->codec);
        QCOMPARE(t->durationMs, before->durationMs);
        QCOMPARE(t->sampleRate, before->sampleRate);
        QVERIFY(leftovers(QFileInfo(path).path()).isEmpty());
    }

    // Only the fields that are set change; the rest stay as they were.
    void partialEditKeepsOtherTags_data() { addFormatRows(); }
    void partialEditKeepsOtherTags()
    {
        QFETCH(int, index);
        const QString path = taggedCopy(kFormats[index].file);
        QVERIFY(!path.isEmpty());

        TagChanges c;
        c.album = QStringLiteral("New Album");
        c.trackNumber = 5; // total stays 10
        QString error;
        QVERIFY2(TagWriter::write(path, c, &error), qPrintable(error));
        const auto t = TagReader::read(path);
        QVERIFY(t);
        QCOMPARE(t->album, QStringLiteral("New Album"));
        QCOMPARE(t->trackNumber, 5);
        QCOMPARE(t->trackTotal, 10);
        QCOMPARE(t->title, QStringLiteral("Old Title"));
        QCOMPARE(t->artist, QStringLiteral("Old Artist"));
        QCOMPARE(t->genre, QStringLiteral("Jazz"));
        QCOMPARE(t->year, 1997);
        QCOMPARE(t->discNumber, 1);
        QCOMPARE(t->discTotal, 2);
    }

    // Empty values remove tags.
    void clearRemovesTags_data() { addFormatRows(); }
    void clearRemovesTags()
    {
        QFETCH(int, index);
        const QString path = taggedCopy(kFormats[index].file);
        QVERIFY(!path.isEmpty());

        TagChanges c;
        c.genre = QString();
        c.artist = QStringLiteral("   ");
        c.year = 0;
        c.trackNumber = 0;
        c.trackTotal = 0;
        c.compilation = false;
        QString error;
        QVERIFY2(TagWriter::write(path, c, &error), qPrintable(error));
        const auto t = TagReader::read(path);
        QVERIFY(t);
        QCOMPARE(t->genre, QString());
        QCOMPARE(t->artist, QString());
        QCOMPARE(t->year, 0);
        QCOMPARE(t->trackNumber, 0);
        QCOMPARE(t->trackTotal, 0);
        QVERIFY(!t->compilation);
        QCOMPARE(t->title, QStringLiteral("Old Title"));
    }

    void vorbisCommentsUseSeparateTotals()
    {
        const QString path = taggedCopy("t.flac");
        TagChanges c;
        c.trackNumber = 4;
        c.discTotal = 3;
        QVERIFY(TagWriter::write(path, c));
        QCOMPARE(raw(path, "TRACKNUMBER"), QStringLiteral("4"));
        QCOMPARE(raw(path, "TRACKTOTAL"), QStringLiteral("10"));
        QCOMPARE(raw(path, "DISCNUMBER"), QStringLiteral("1"));
        QCOMPARE(raw(path, "DISCTOTAL"), QStringLiteral("3"));
    }

    void id3UsesCombinedNumbers()
    {
        const QString path = taggedCopy("t.mp3");
        TagChanges c;
        c.trackNumber = 4;
        QVERIFY(TagWriter::write(path, c));
        QCOMPARE(raw(path, "TRACKNUMBER"), QStringLiteral("4/10"));
    }

    // A full date with the right year is left alone; a new year replaces it.
    void yearKeepsMatchingFullDate()
    {
        const QString path = taggedCopy("t.flac");
        TagChanges c;
        c.year = 1997;
        c.title = QStringLiteral("x");
        QVERIFY(TagWriter::write(path, c));
        QCOMPARE(raw(path, "DATE"), QStringLiteral("1997-09-22"));
        c.year = 2001;
        QVERIFY(TagWriter::write(path, c));
        QCOMPARE(raw(path, "DATE"), QStringLiteral("2001"));
    }

    void id3v23StaysV23()
    {
        const QString path = taggedCopy("t.mp3");
        {
            TagLib::MPEG::File f(QFile::encodeName(path).constData());
            QVERIFY(f.save(TagLib::MPEG::File::ID3v2, TagLib::File::StripOthers, TagLib::ID3v2::v3));
        }
        TagChanges c;
        c.title = QStringLiteral("v2.3 still");
        QVERIFY(TagWriter::write(path, c));
        TagLib::MPEG::File f(QFile::encodeName(path).constData());
        QVERIFY(f.hasID3v2Tag());
        QCOMPARE(f.ID3v2Tag()->header()->majorVersion(), 3u);
        QVERIFY(!f.hasID3v1Tag()); // doesn't add tag types the file didn't have
        QCOMPARE(QString::fromStdString(f.ID3v2Tag()->title().to8Bit(true)), QStringLiteral("v2.3 still"));
    }

    // --- Cover art (M7) ---

    void frontCover_data() { addFormatRows(); }
    void frontCover()
    {
        QFETCH(int, index);
        const QString path = taggedCopy(kFormats[index].file);
        QVERIFY(!path.isEmpty());
        QVERIFY(!TagReader::read(path)->hasEmbeddedArt);

        TagChanges c;
        c.frontCover = picture(fakeJpeg('a'));
        QString error;
        QVERIFY2(TagWriter::write(path, c, &error), qPrintable(error));
        auto cover = TagReader::readCover(path);
        QVERIFY(cover);
        QCOMPARE(cover->data, fakeJpeg('a'));
        QCOMPARE(cover->mimeType, QStringLiteral("image/jpeg"));
        const auto tags = TagReader::read(path);
        QVERIFY(tags->hasEmbeddedArt);
        QCOMPARE(tags->title, QStringLiteral("Old Title")); // the text tags are untouched
        QCOMPARE(tags->trackNumber, 2);
        QCOMPARE(raw(path, "REPLAYGAIN_TRACK_GAIN").isEmpty(), false);

        // Saving again replaces the cover instead of adding a second one.
        c.frontCover = picture(fakeJpeg('b'));
        QVERIFY2(TagWriter::write(path, c, &error), qPrintable(error));
        const auto pictures = TagReader::readPictures(path);
        QCOMPARE(pictures.size(), 1);
        QCOMPARE(pictures.front().data, fakeJpeg('b'));
        QVERIFY(leftovers(QFileInfo(path).path()).isEmpty());
    }

    void frontCoverKeepsOtherPictures()
    {
        const QString path = taggedCopy("t.flac");
        {
            TagLib::FileRef ref(QFile::encodeName(path).constData());
            TagLib::VariantMap back;
            const QByteArray data = fakeJpeg('z');
            back.insert("data", TagLib::ByteVector(data.constData(), static_cast<unsigned int>(data.size())));
            back.insert("mimeType", TagLib::String("image/jpeg"));
            back.insert("pictureType", TagLib::String("Back Cover"));
            QVERIFY(ref.setComplexProperties("PICTURE", {back}));
            QVERIFY(ref.save());
        }
        TagChanges c;
        c.frontCover = picture(fakeJpeg('f'));
        QVERIFY(TagWriter::write(path, c));
        const auto pictures = TagReader::readPictures(path);
        QCOMPARE(pictures.size(), 2);
        QCOMPARE(pictures[0].pictureType, QStringLiteral("Front Cover"));
        QCOMPARE(pictures[0].data, fakeJpeg('f'));
        QCOMPARE(pictures[1].pictureType, QStringLiteral("Back Cover"));
        QCOMPARE(TagReader::readCover(path)->data, fakeJpeg('f'));
    }

    void frontCoverKeepsId3v23()
    {
        const QString path = taggedCopy("t.mp3");
        {
            TagLib::MPEG::File f(QFile::encodeName(path).constData());
            QVERIFY(f.save(TagLib::MPEG::File::ID3v2, TagLib::File::StripOthers, TagLib::ID3v2::v3));
        }
        TagChanges c;
        c.frontCover = picture(fakeJpeg('3'));
        QVERIFY(TagWriter::write(path, c));
        TagLib::MPEG::File f(QFile::encodeName(path).constData());
        QCOMPARE(f.ID3v2Tag()->header()->majorVersion(), 3u);
        QVERIFY(!f.hasID3v1Tag());
        QCOMPARE(TagReader::readCover(path)->data, fakeJpeg('3'));
    }

    void id3v1IsUpdatedToo()
    {
        const QString path = taggedCopy("t.mp3");
        {
            TagLib::MPEG::File f(QFile::encodeName(path).constData());
            f.ID3v1Tag(true)->setTitle("Old Title");
            QVERIFY(f.save(TagLib::MPEG::File::AllTags, TagLib::File::StripNone));
        }
        TagChanges c;
        c.title = QStringLiteral("Both");
        QVERIFY(TagWriter::write(path, c));
        TagLib::MPEG::File f(QFile::encodeName(path).constData());
        QVERIFY(f.hasID3v1Tag());
        QCOMPARE(QString::fromStdString(f.ID3v1Tag()->title().to8Bit(true)), QStringLiteral("Both"));
    }

    // The file is replaced, not rewritten: an open handle still reads the old bytes.
    void replacesAtomically()
    {
        const QString path = taggedCopy("t.flac");
        QFile open(path);
        QVERIFY(open.open(QIODevice::ReadOnly));
        const QByteArray original = open.peek(1 << 20);
        const ino_t inode = inodeOf(path);
        const auto perms = QFile::permissions(path);

        TagChanges c;
        c.title = QString(5000, QLatin1Char('x')); // bigger than the FLAC padding
        QVERIFY(TagWriter::write(path, c));
        QVERIFY(inodeOf(path) != inode);
        QCOMPARE(QFile::permissions(path), perms);
        QCOMPARE(open.readAll(), original);
        QCOMPARE(TagReader::read(path)->title, *c.title);
        QVERIFY(leftovers(QFileInfo(path).path()).isEmpty());
    }

    void emptyChangesDoNothing()
    {
        const QString path = taggedCopy("t.flac");
        const ino_t inode = inodeOf(path);
        QVERIFY(TagWriter::write(path, TagChanges{}));
        QCOMPARE(inodeOf(path), inode);
    }

    void failures()
    {
        TagChanges c;
        c.title = QStringLiteral("x");
        QString error;
        QVERIFY(!TagWriter::write(m_dir.filePath(QStringLiteral("missing.flac")), c, &error));
        QCOMPARE(error, QStringLiteral("file not found"));

        const QString junkDir = m_dir.filePath(QStringLiteral("junk"));
        QDir().mkpath(junkDir);
        QFile junk(junkDir + QStringLiteral("/junk.mp3"));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write(QByteArray(8192, 'x'));
        junk.close();
        QVERIFY(!TagWriter::write(junk.fileName(), c, &error));
        QVERIFY(leftovers(junkDir).isEmpty());

        const QString ro = taggedCopy("t.flac");
        QFile::setPermissions(ro, QFile::ReadOwner);
        QVERIFY(!TagWriter::write(ro, c, &error));
        QCOMPARE(error, QStringLiteral("the file is read-only"));
        QFile::setPermissions(ro, QFile::ReadOwner | QFile::WriteOwner);
        QCOMPARE(TagReader::read(ro)->title, QStringLiteral("Old Title"));
    }

    // editTracks: files and the library change together; the album keeps its folder art.
    void editTracksUpdatesLibrary()
    {
        const QString dir = m_dir.filePath(QStringLiteral("lib/Artist/Album"));
        QDir().mkpath(dir);
        const QString root = QFileInfo(m_dir.filePath(QStringLiteral("lib"))).canonicalFilePath();
        QStringList paths;
        for (int i = 1; i <= 2; ++i) {
            const QString p = QFileInfo(dir).canonicalFilePath() + QStringLiteral("/%1.flac").arg(i);
            QVERIFY(QFile::copy(pathFor("t.flac"), p));
            QVERIFY(writeBaseTags(p));
            paths << p;
        }
        const QString cover = QFileInfo(dir).canonicalFilePath() + QStringLiteral("/cover.jpg");
        {
            QFile f(cover);
            QVERIFY(f.open(QIODevice::WriteOnly));
        }

        mmm::Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        const auto folderId = db.addLibraryFolder(root);
        QVERIFY(folderId);
        for (const QString &p : paths) {
            const QFileInfo fi(p);
            QVERIFY(db.upsertTrack(mmm::TrackFile{p, fi.lastModified().toMSecsSinceEpoch(), fi.size(), *folderId},
                                   *TagReader::read(p)));
        }
        QVERIFY(db.setFolderCover(QFileInfo(dir).canonicalFilePath(), cover));

        TagChanges c;
        c.album = QStringLiteral("Renamed");
        c.year = 2005;
        const mmm::TagEditResult r =
            mmm::editTracks(db, paths + QStringList{root + QStringLiteral("/gone.flac")}, c);
        QCOMPARE(r.written, paths);
        QCOMPARE(r.failures.size(), 1);
        QVERIFY(r.failures.front().contains(QStringLiteral("gone.flac: file not found")));
        QCOMPARE(r.tracks.size(), 2);
        QCOMPARE(r.tracks.front().album, QStringLiteral("Renamed"));

        const QList<mmm::AlbumRow> albums = mmm::LibraryQuery(db).albums();
        QCOMPARE(albums.size(), 1); // the old album was pruned
        QCOMPARE(albums.front().title, QStringLiteral("Renamed"));
        QCOMPARE(albums.front().year, 2005);
        QCOMPARE(albums.front().coverPath, cover);
        QCOMPARE(albums.front().trackCount, 2);

        // The stored file state matches the new file, so a rescan sees it unchanged.
        const auto states = db.fileStatesUnder(root);
        for (const QString &p : paths) {
            const QFileInfo fi(p);
            QCOMPARE(states.value(p), (mmm::FileState{fi.lastModified().toMSecsSinceEpoch(), fi.size()}));
        }
        QSqlQuery q(db.connection());
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM tracks WHERE folder_id = %1").arg(*folderId)));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 2);
        QCOMPARE(db.search(QStringLiteral("renamed")).size(), 2);
    }
};

QTEST_GUILESS_MAIN(TestTagWriter)
#include "tst_tagwriter.moc"

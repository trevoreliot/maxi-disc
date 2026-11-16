// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDirIterator>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <taglib/fileref.h>
#include <taglib/tpropertymap.h>

#include "database.h"
#include "tagreader.h"
#include "testmedia.h"

using mmm::TagReader;
using mmm::TrackTags;

namespace {

constexpr int kToneMs = 1000;

struct Format
{
    const char *name;
    bool isMp4() const { return qstrcmp(name, "aac") == 0 || qstrcmp(name, "alac") == 0; }
    const char *file;
    const char *encoder;
    const char *codec;
    int sampleRate;
    int bitsPerSample;
};

// Every v1 playback format (PLAN.md scope).
constexpr Format kFormats[] = {
    {"flac", "t.flac", "flacenc", "FLAC", 44100, 16},
    {"mp3", "t.mp3", "lamemp3enc target=bitrate bitrate=320 cbr=true ! id3v2mux", "MP3", 44100, 0},
    {"wav", "t.wav", "wavenc", "WAV", 44100, 16},
    {"aac", "t-aac.m4a", "avenc_aac ! mp4mux", "AAC", 44100, 0},
    {"alac", "t-alac.m4a", "avenc_alac ! mp4mux", "ALAC", 44100, 16},
    {"vorbis", "t.ogg", "vorbisenc ! oggmux", "Vorbis", 44100, 0},
    {"opus", "t.opus", "opusenc ! oggmux", "Opus", 48000, 0},
};

// Written by GStreamer (not TagLib): an independent writer for the interop check.
const QString kGstTags = QStringLiteral(
    R"(title="Interop Title",artist="Interop Artist",album="Interop Album",genre="Jazz")");

// Fake JPEG/PNG payloads: TagLib stores bytes verbatim, so real images aren't needed.
const QByteArray kFrontCover = QByteArrayLiteral("\xFF\xD8\xFF\xE0 front-cover-bytes");
const QByteArray kBackCover = QByteArrayLiteral("\x89PNG back-cover-bytes");

TagLib::String ts(const char *s) { return TagLib::String(s, TagLib::String::UTF8); }

bool writeProperties(const QString &path, const TagLib::PropertyMap &props)
{
    const QByteArray name = QFile::encodeName(path);
    TagLib::FileRef ref(name.constData());
    if (ref.isNull())
        return false;
    ref.setProperties(props);
    return ref.save();
}

TagLib::VariantMap picture(const QByteArray &data, const char *mime, const char *type)
{
    TagLib::VariantMap m;
    m.insert("data", TagLib::ByteVector(data.constData(), static_cast<unsigned int>(data.size())));
    m.insert("mimeType", ts(mime));
    m.insert("pictureType", ts(type));
    m.insert("description", ts(""));
    return m;
}

bool writePictures(const QString &path, const TagLib::List<TagLib::VariantMap> &pics)
{
    const QByteArray name = QFile::encodeName(path);
    TagLib::FileRef ref(name.constData());
    if (ref.isNull() || !ref.setComplexProperties("PICTURE", pics))
        return false;
    return ref.save();
}

} // namespace

class TestTagReader : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString pathFor(const char *file) const { return m_dir.filePath(QString::fromLatin1(file)); }

    QString freshCopy(const char *file, const QString &suffix)
    {
        const QString src = pathFor(file);
        const QString dst = m_dir.filePath(suffix + QLatin1Char('-') + QString::fromLatin1(file));
        QFile::remove(dst);
        return QFile::copy(src, dst) ? dst : QString();
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
            const QString err = mmm::test::generateTone(pathFor(f.file), QString::fromLatin1(f.encoder),
                                                        kToneMs, kGstTags);
            QVERIFY2(err.isEmpty(), qPrintable(QStringLiteral("%1: %2").arg(QLatin1String(f.name), err)));
        }
    }

    // --- Parsing helpers ---

    void parseNumberPair_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("n");
        QTest::addColumn<int>("total");
        QTest::newRow("plain") << "3" << 3 << 0;
        QTest::newRow("pair") << "3/12" << 3 << 12;
        QTest::newRow("padded") << " 03 / 12 " << 3 << 12;
        QTest::newRow("empty") << "" << 0 << 0;
        QTest::newRow("garbage") << "A1" << 0 << 0;
        QTest::newRow("vinyl side") << "B2" << 0 << 0;
    }
    void parseNumberPair()
    {
        QFETCH(QString, text);
        QFETCH(int, n);
        QFETCH(int, total);
        QCOMPARE(TagReader::parseNumberPair(text), std::make_pair(n, total));
    }

    void parseYear_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("year");
        QTest::newRow("year") << "2019" << 2019;
        QTest::newRow("iso date") << "2019-05-03" << 2019;
        QTest::newRow("iso datetime") << "2019-05-03T10:00:00" << 2019;
        QTest::newRow("slash") << "1999/12" << 1999;
        QTest::newRow("empty") << "" << 0;
        QTest::newRow("two digits") << "99" << 0;
        QTest::newRow("words") << "Spring 2001" << 0;
    }
    void parseYear()
    {
        QFETCH(QString, text);
        QFETCH(int, year);
        QCOMPARE(TagReader::parseYear(text), year);
    }

    void parseGain_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<double>("value");
        QTest::newRow("db") << "-6.50 dB" << true << -6.5;
        QTest::newRow("plus") << "+1.2 dB" << true << 1.2;
        QTest::newRow("no unit") << "-3" << true << -3.0;
        QTest::newRow("lowercase") << "2.00 db" << true << 2.0;
        QTest::newRow("peak") << "0.987654" << true << 0.987654;
        QTest::newRow("empty") << "" << false << 0.0;
        QTest::newRow("garbage") << "loud" << false << 0.0;
    }
    void parseGain()
    {
        QFETCH(QString, text);
        QFETCH(bool, valid);
        QFETCH(double, value);
        const auto g = TagReader::parseGain(text);
        QCOMPARE(g.has_value(), valid);
        if (valid)
            QCOMPARE(*g, value);
    }

    // --- Reading real files ---

    void audioPropertiesAndInteropTags_data() { addFormatRows(); }
    void audioPropertiesAndInteropTags()
    {
        QFETCH(int, index);
        const Format &f = kFormats[index];
        QString error;
        const auto t = TagReader::read(pathFor(f.file), &error);
        QVERIFY2(t, qPrintable(error));

        QCOMPARE(t->codec, QString::fromLatin1(f.codec));
        QCOMPARE(t->sampleRate, f.sampleRate);
        QCOMPARE(t->channels, 2);
        QCOMPARE(t->bitsPerSample, f.bitsPerSample);
        QVERIFY2(qAbs(t->durationMs - kToneMs) <= 60, qPrintable(QString::number(t->durationMs)));
        QVERIFY(t->bitrateKbps > 0);
        QVERIFY(!t->hasEmbeddedArt);

        // GStreamer's mp4mux writes tags at track level (moov/trak/udta/meta/ilst), which
        // iTunes, Picard, ffmpeg and TagLib don't read (they use moov/udta/meta/ilst).
        // That's a GStreamer quirk, not ours; MP4 tags are covered by fullTagSet instead.
        if (f.isMp4())
            return;
        QCOMPARE(t->title, QStringLiteral("Interop Title"));
        QCOMPARE(t->artist, QStringLiteral("Interop Artist"));
        QCOMPARE(t->album, QStringLiteral("Interop Album"));
        QCOMPARE(t->genre, QStringLiteral("Jazz"));
    }

    void fullTagSet_data() { addFormatRows(); }
    void fullTagSet()
    {
        QFETCH(int, index);
        const Format &f = kFormats[index];
        const QString path = freshCopy(f.file, QStringLiteral("full"));
        QVERIFY(!path.isEmpty());

        TagLib::PropertyMap props;
        props.replace("TITLE", ts("Jóga"));
        props.replace("ARTIST", TagLib::StringList(ts("Björk")));
        props.replace("ALBUM", ts("Homogenic"));
        props.replace("ALBUMARTIST", ts("Björk"));
        props.replace("GENRE", ts("Electronic"));
        props.replace("COMPOSER", ts("Björk Guðmundsdóttir"));
        props.replace("DATE", ts("1997-09-22"));
        props.replace("TRACKNUMBER", ts("2/10"));
        props.replace("DISCNUMBER", ts("1/1"));
        props.replace("COMPILATION", ts("1"));
        props.replace("REPLAYGAIN_TRACK_GAIN", ts("-7.25 dB"));
        props.replace("REPLAYGAIN_TRACK_PEAK", ts("0.988"));
        props.replace("REPLAYGAIN_ALBUM_GAIN", ts("-8.10 dB"));
        props.replace("REPLAYGAIN_ALBUM_PEAK", ts("0.999"));
        props.replace("MUSICBRAINZ_ALBUMID", ts("1f9b5d30-5a67-3b5d-9a2c-1f7f2f9e8f00"));
        props.replace("MUSICBRAINZ_TRACKID", ts("2a8c6e41-6b78-4c6e-ab3d-2e8e3e0f9a11"));
        props.replace("MUSICBRAINZ_ALBUMARTISTID", ts("87c5dedd-371d-4a53-9f7f-80522fb7f3cb"));
        QVERIFY(writeProperties(path, props));

        QString error;
        const auto t = TagReader::read(path, &error);
        QVERIFY2(t, qPrintable(error));
        QCOMPARE(t->title, QStringLiteral("Jóga"));
        QCOMPARE(t->artist, QStringLiteral("Björk"));
        QCOMPARE(t->album, QStringLiteral("Homogenic"));
        QCOMPARE(t->albumArtist, QStringLiteral("Björk"));
        QCOMPARE(t->genre, QStringLiteral("Electronic"));
        QCOMPARE(t->composer, QStringLiteral("Björk Guðmundsdóttir"));
        QCOMPARE(t->year, 1997);
        QCOMPARE(t->trackNumber, 2);
        QCOMPARE(t->trackTotal, 10);
        QCOMPARE(t->discNumber, 1);
        QCOMPARE(t->discTotal, 1);
        QVERIFY(t->compilation);
        QCOMPARE(t->replayGainTrackGain, std::optional<double>(-7.25));
        QCOMPARE(t->replayGainTrackPeak, std::optional<double>(0.988));
        QCOMPARE(t->replayGainAlbumGain, std::optional<double>(-8.10));
        QCOMPARE(t->replayGainAlbumPeak, std::optional<double>(0.999));
        QCOMPARE(t->musicBrainzAlbumId, QStringLiteral("1f9b5d30-5a67-3b5d-9a2c-1f7f2f9e8f00"));
        QCOMPARE(t->musicBrainzTrackId, QStringLiteral("2a8c6e41-6b78-4c6e-ab3d-2e8e3e0f9a11"));
        QCOMPARE(t->musicBrainzAlbumArtistId, QStringLiteral("87c5dedd-371d-4a53-9f7f-80522fb7f3cb"));
        QCOMPARE(t->codec, QString::fromLatin1(f.codec)); // tagging must not change detection
    }

    void multipleArtistsJoined()
    {
        const QString path = freshCopy("t.flac", QStringLiteral("multi"));
        TagLib::PropertyMap props;
        TagLib::StringList artists;
        artists.append(ts("Daft Punk"));
        artists.append(ts("Pharrell Williams"));
        artists.append(ts("Daft Punk")); // duplicate dropped
        props.replace("ARTIST", artists);
        props.replace("TITLE", ts("Get Lucky"));
        QVERIFY(writeProperties(path, props));
        const auto t = TagReader::read(path);
        QVERIFY(t);
        QCOMPARE(t->artist, QStringLiteral("Daft Punk, Pharrell Williams"));
        QCOMPARE(t->effectiveAlbumArtist(), t->artist); // no ALBUMARTIST → artist
    }

    void embeddedPictures_data() { addFormatRows(); }
    void embeddedPictures()
    {
        QFETCH(int, index);
        const Format &f = kFormats[index];
        const QString path = freshCopy(f.file, QStringLiteral("art"));
        QVERIFY(!path.isEmpty());

        TagLib::List<TagLib::VariantMap> pics;
        pics.append(picture(kBackCover, "image/png", "Back Cover")); // first in file order
        pics.append(picture(kFrontCover, "image/jpeg", "Front Cover"));
        QVERIFY(writePictures(path, pics));

        const auto t = TagReader::read(path);
        QVERIFY(t);
        QVERIFY(t->hasEmbeddedArt);

        const auto all = TagReader::readPictures(path);
        QCOMPARE(all.size(), 2);

        const auto cover = TagReader::readCover(path);
        QVERIFY(cover);
        if (f.isMp4()) {
            // MP4 "covr" stores only format + bytes, no picture type: first picture wins.
            QCOMPARE(cover->data, kBackCover);
            return;
        }
        QCOMPARE(cover->data, kFrontCover); // front cover preferred over first picture
        QCOMPARE(cover->mimeType, QStringLiteral("image/jpeg"));
        QCOMPARE(cover->pictureType, QStringLiteral("Front Cover"));
    }

    void coverFallsBackToFirstPicture()
    {
        const QString path = freshCopy("t.mp3", QStringLiteral("noFront"));
        TagLib::List<TagLib::VariantMap> pics;
        pics.append(picture(kBackCover, "image/png", "Back Cover"));
        QVERIFY(writePictures(path, pics));
        const auto cover = TagReader::readCover(path);
        QVERIFY(cover);
        QCOMPARE(cover->data, kBackCover);
    }

    void noPictures()
    {
        QVERIFY(TagReader::readPictures(pathFor("t.flac")).isEmpty());
        QVERIFY(!TagReader::readCover(pathFor("t.flac")));
    }

    void unicodePath()
    {
        const QString path = m_dir.filePath(QStringLiteral("Sigur Rós – Hoppípolla ☃.flac"));
        QVERIFY(QFile::copy(pathFor("t.flac"), path));
        const auto t = TagReader::read(path);
        QVERIFY(t);
        QCOMPARE(t->codec, QStringLiteral("FLAC"));
        QVERIFY(!TagReader::readCover(path)); // picture API uses the same path encoding
    }

    void missingFile()
    {
        QString error;
        QVERIFY(!TagReader::read(m_dir.filePath(QStringLiteral("nope.flac")), &error));
        QCOMPARE(error, QStringLiteral("file not found"));
        QVERIFY(TagReader::readPictures(m_dir.filePath(QStringLiteral("nope.flac"))).isEmpty());
    }

    void notAudio()
    {
        const QString path = m_dir.filePath(QStringLiteral("fake.mp3"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(8192, 'x'));
        f.close();
        QString error;
        QVERIFY(!TagReader::read(path, &error));
        QCOMPARE(error, QStringLiteral("no audio stream found"));
    }

    // --- User test (M2): point MMM_TEST_MEDIA_DIR at a folder of your own music ---
    void userFiles()
    {
        const QString dir = qEnvironmentVariable("MMM_TEST_MEDIA_DIR");
        if (dir.isEmpty())
            QSKIP("set MMM_TEST_MEDIA_DIR=/path/to/some/music to run against your own files");

        const QStringList globs = {QStringLiteral("*.flac"), QStringLiteral("*.mp3"), QStringLiteral("*.wav"),
                                   QStringLiteral("*.m4a"),  QStringLiteral("*.ogg"), QStringLiteral("*.opus")};
        QDirIterator it(dir, globs, QDir::Files, QDirIterator::Subdirectories);

        mmm::Database db;
        QString error;
        QVERIFY2(db.open(QStringLiteral(":memory:"), &error), qPrintable(error));

        int count = 0;
        int withArt = 0;
        QStringList failures;
        while (it.hasNext()) {
            const QString path = it.next();
            const auto t = TagReader::read(path, &error);
            if (!t) {
                failures << path + QStringLiteral(": ") + error;
                continue;
            }
            if (t->codec == QLatin1String("Unknown") || t->durationMs <= 0) {
                failures << path + QStringLiteral(": codec=%1 duration=%2").arg(t->codec).arg(t->durationMs);
                continue;
            }
            if (t->hasEmbeddedArt) {
                if (!TagReader::readCover(path)) {
                    failures << path + QStringLiteral(": reports art but no cover could be read");
                    continue;
                }
                ++withArt;
            }
            const QFileInfo fi(path);
            const auto id = db.upsertTrack(path, fi.lastModified().toMSecsSinceEpoch(), fi.size(), *t, &error);
            if (!id) {
                failures << path + QStringLiteral(": DB: ") + error;
                continue;
            }
            const QString title = t->title.isEmpty() ? fi.completeBaseName() : t->title;
            if (!db.search(title, 100000).contains(*id))
                failures << path + QStringLiteral(": not found by searching its title \"%1\"").arg(title);
            ++count;
        }
        qInfo("user files: %d read OK (%d with embedded art), %lld failed", count, withArt,
              static_cast<long long>(failures.size()));
        for (const QString &f : std::as_const(failures))
            qWarning("%s", qPrintable(f));
        QVERIFY2(count > 0, "no audio files found in MMM_TEST_MEDIA_DIR");
        QVERIFY2(failures.isEmpty(), "some files failed; see warnings above");
    }
};

QTEST_GUILESS_MAIN(TestTagReader)
#include "tst_tagreader.moc"

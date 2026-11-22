// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "artshrinker.h"
#include "tagreader.h"
#include "tagwriter.h"
#include "testmedia.h"
#include "transcodecache.h"
#include "transcoder.h"

using namespace mmm;

namespace {

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class TestTranscoder : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_flac;      // tagged, with an 800 px embedded JPEG cover
    QByteArray m_cover;

    QString path(const QString &name) const { return m_dir.filePath(name); }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_flac = path(QStringLiteral("source.flac"));
        const QString err = test::generateTone(
            m_flac, QStringLiteral("flacenc"), 600,
            QStringLiteral("title=\"Hells Bells\",artist=\"AC/DC\",album=\"Back in Black\",track-number=(uint)1"));
        QVERIFY2(err.isEmpty(), qPrintable(err));
        m_cover = test::generateImage(800, 800, QStringLiteral("jpegenc"));
        QVERIFY(!m_cover.isEmpty());
        TagChanges c;
        c.frontCover = EmbeddedPicture{m_cover, QStringLiteral("image/jpeg"), QStringLiteral("Front Cover"), QString()};
        c.year = 1980;
        QString error;
        QVERIFY2(TagWriter::write(m_flac, c, &error), qPrintable(error));
    }

    void transcodesToMp3_data()
    {
        QTest::addColumn<int>("target");
        QTest::newRow("V0") << int(TranscodeTarget::Mp3V0);
        QTest::newRow("320 CBR") << int(TranscodeTarget::Mp3Cbr320);
    }
    void transcodesToMp3()
    {
        QFETCH(int, target);
        const QString out = path(QStringLiteral("plain-%1.mp3").arg(target));
        QString error;
        QVERIFY2(Transcoder::transcodeToMp3(m_flac, out, TranscodeTarget(target), nullptr, &error), qPrintable(error));
        const auto tags = TagReader::read(out, &error);
        QVERIFY2(tags, qPrintable(error));
        QCOMPARE(tags->codec, QStringLiteral("MP3"));
        QVERIFY(std::abs(tags->durationMs - 600) < 150);
        if (TranscodeTarget(target) == TranscodeTarget::Mp3Cbr320)
            QVERIFY2(tags->bitrateKbps >= 300, qPrintable(QString::number(tags->bitrateKbps))); // headers count in a 0.6 s file
    }

    void transcodeCopiesTagsAndArt_data()
    {
        QTest::addColumn<int>("art");
        QTest::addColumn<int>("coverSide"); // 0 = no cover
        QTest::newRow("keep") << int(ArtworkPolicy::Keep) << 800;
        QTest::newRow("shrink") << int(ArtworkPolicy::Shrink500) << 500;
        QTest::newRow("strip") << int(ArtworkPolicy::Strip) << 0;
    }
    void transcodeCopiesTagsAndArt()
    {
        QFETCH(int, art);
        QFETCH(int, coverSide);
        Transcoder::Request r;
        r.sourcePath = m_flac;
        r.destPath = path(QStringLiteral("t-%1.mp3").arg(art));
        r.transcode = true;
        r.art = ArtworkPolicy(art);
        QString error;
        QVERIFY2(Transcoder::prepare(r, nullptr, &error), qPrintable(error));
        const auto tags = TagReader::read(r.destPath, &error);
        QVERIFY2(tags, qPrintable(error));
        QCOMPARE(tags->codec, QStringLiteral("MP3"));
        QCOMPARE(tags->title, QStringLiteral("Hells Bells"));
        QCOMPARE(tags->artist, QStringLiteral("AC/DC"));
        QCOMPARE(tags->album, QStringLiteral("Back in Black"));
        QCOMPARE(tags->trackNumber, 1);
        QCOMPARE(tags->year, 1980);
        // ID3v2.3: what most portable players read.
        const QByteArray head = readAll(r.destPath).left(4);
        QCOMPARE(head, QByteArray("ID3\x03", 4));
        const auto cover = TagReader::readCover(r.destPath);
        if (coverSide == 0) {
            QVERIFY(!cover);
        } else {
            QVERIFY(cover);
            QCOMPARE(ArtShrinker::imageSize(cover->data), QSize(coverSide, coverSide));
            if (ArtworkPolicy(art) == ArtworkPolicy::Keep)
                QCOMPARE(cover->data, m_cover);
        }
    }

    void copyWithShrunkArt()
    {
        Transcoder::Request r;
        r.sourcePath = m_flac;
        r.destPath = path(QStringLiteral("copy.flac"));
        r.art = ArtworkPolicy::Shrink500;
        QString error;
        QVERIFY2(Transcoder::prepare(r, nullptr, &error), qPrintable(error));
        const auto tags = TagReader::read(r.destPath, &error);
        QVERIFY2(tags, qPrintable(error));
        QCOMPARE(tags->codec, QStringLiteral("FLAC"));
        QCOMPARE(tags->title, QStringLiteral("Hells Bells"));
        QCOMPARE(ArtShrinker::imageSize(TagReader::readCover(r.destPath)->data), QSize(500, 500));
        // The library file is untouched.
        QCOMPARE(TagReader::readCover(m_flac)->data, m_cover);
    }

    void folderArtBeatsEmbedded()
    {
        const QString folderArt = path(QStringLiteral("cover.png"));
        QFile f(folderArt);
        QVERIFY(f.open(QIODevice::WriteOnly));
        const QByteArray png = test::generateImage(700, 350);
        f.write(png);
        f.close();

        // Keep: the file's own art stays; folder art only fills in for a file without any.
        Transcoder::Request r;
        r.sourcePath = m_flac;
        r.transcode = true;
        r.artPath = folderArt;
        r.destPath = path(QStringLiteral("folder-keep.mp3"));
        QVERIFY(Transcoder::prepare(r, nullptr));
        auto cover = TagReader::readCover(r.destPath);
        QVERIFY(cover);
        QCOMPARE(cover->data, m_cover);
        const QString bare = path(QStringLiteral("bare.flac"));
        QVERIFY(test::generateTone(bare, QStringLiteral("flacenc"), 300).isEmpty());
        r.sourcePath = bare;
        r.destPath = path(QStringLiteral("folder-fill.mp3"));
        QVERIFY(Transcoder::prepare(r, nullptr));
        cover = TagReader::readCover(r.destPath);
        QVERIFY(cover);
        QCOMPARE(cover->data, png);
        QCOMPARE(cover->mimeType, QStringLiteral("image/png"));
        r.sourcePath = m_flac;

        r.art = ArtworkPolicy::Shrink500;
        r.destPath = path(QStringLiteral("folder-shrink.mp3"));
        QVERIFY(Transcoder::prepare(r, nullptr));
        cover = TagReader::readCover(r.destPath);
        QCOMPARE(ArtShrinker::imageSize(cover->data), QSize(500, 250));
        QCOMPARE(cover->mimeType, QStringLiteral("image/jpeg"));
    }

    void failuresLeaveNoFile()
    {
        std::atomic_bool cancel{true};
        Transcoder::Request r;
        r.sourcePath = m_flac;
        r.transcode = true;
        r.destPath = path(QStringLiteral("canceled.mp3"));
        QString error;
        QVERIFY(!Transcoder::prepare(r, &cancel, &error));
        QVERIFY(!QFile::exists(r.destPath));

        const QString junk = path(QStringLiteral("junk.flac"));
        QFile f(junk);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("this is not audio");
        f.close();
        r.sourcePath = junk;
        r.destPath = path(QStringLiteral("junk.mp3"));
        QVERIFY(!Transcoder::prepare(r, nullptr, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFile::exists(r.destPath));
    }

    void cacheKeysAndEntries()
    {
        const QString k = TranscodeCache::key(QStringLiteral("/a.flac"), 10, 20, QStringLiteral("v1|mp3_v0|keep|"));
        QCOMPARE(k, TranscodeCache::key(QStringLiteral("/a.flac"), 10, 20, QStringLiteral("v1|mp3_v0|keep|")));
        QVERIFY(k != TranscodeCache::key(QStringLiteral("/a.flac"), 10, 21, QStringLiteral("v1|mp3_v0|keep|")));
        QVERIFY(k != TranscodeCache::key(QStringLiteral("/a.flac"), 10, 20, QStringLiteral("v1|mp3_cbr_320|keep|")));

        TranscodeCache cache(path(QStringLiteral("cache")), 2500);
        QCOMPARE(cache.size(k, QStringLiteral("mp3")), qint64(-1));
        const QString temp = cache.tempPath(QStringLiteral("mp3"));
        QVERIFY(QFileInfo(temp).fileName().startsWith(QLatin1Char('.')));
        QFile f(temp);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(1000, 'x'));
        f.close();
        const QString entry = cache.insert(k, QStringLiteral("mp3"), temp);
        QCOMPARE(entry, cache.path(k, QStringLiteral("mp3")));
        QCOMPARE(cache.size(k, QStringLiteral("mp3")), qint64(1000));
        QVERIFY(!QFile::exists(temp));
    }

    void cacheEvictsLeastRecentlyUsed()
    {
        TranscodeCache cache(path(QStringLiteral("lru")), 2500);
        const QDateTime now = QDateTime::currentDateTime();
        QStringList entries;
        for (int i = 0; i < 3; ++i) {
            const QString temp = cache.tempPath(QStringLiteral("mp3"));
            QFile f(temp);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray(1000, 'x'));
            f.flush(); // or closing writes again and resets the time
            f.setFileTime(now.addSecs(-100 + i), QFileDevice::FileModificationTime); // 0 oldest
            f.close();
            entries << cache.insert(QStringLiteral("k%1").arg(i), QStringLiteral("mp3"), temp);
        }
        // The oldest was just used: the second-oldest goes.
        cache.touch(entries[0]);
        // A leftover preparation (interrupted run) goes too.
        const QString leftover = cache.tempPath(QStringLiteral("mp3"));
        QVERIFY(QFile(leftover).open(QIODevice::WriteOnly));
        QCOMPARE(cache.totalBytes(), qint64(3000));
        QCOMPARE(cache.evict(), 2);
        QVERIFY(QFile::exists(entries[0]));
        QVERIFY(!QFile::exists(entries[1]));
        QVERIFY(QFile::exists(entries[2]));
        QVERIFY(!QFile::exists(leftover));
        QCOMPARE(cache.totalBytes(), qint64(2000));
    }
};

QTEST_GUILESS_MAIN(TestTranscoder)
#include "tst_transcoder.moc"

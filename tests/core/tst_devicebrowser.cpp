// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "database.h"
#include "devicebrowser.h"
#include "testmedia.h"

using namespace mmm;

namespace {

TrackRow libTrack(const QString &path, const QString &artist, const QString &album, const QString &title,
                  qint64 durationMs = 200, const QString &albumArtist = {})
{
    TrackRow t;
    t.path = path;
    t.artist = artist;
    t.album = album;
    t.title = title;
    t.albumArtist = albumArtist;
    t.durationMs = durationMs;
    return t;
}

DeviceTrack deviceTrack(const QString &artist, const QString &album, const QString &title, qint64 durationMs = 200,
                        const QString &albumArtist = {})
{
    DeviceTrack t;
    t.relPath = QStringLiteral("Music/x.mp3");
    t.artist = artist;
    t.album = album;
    t.title = title;
    t.albumArtist = albumArtist;
    t.durationMs = durationMs;
    return t;
}

TrackTags tags(const QString &artist, const QString &album, const QString &title)
{
    TrackTags t;
    t.artist = artist;
    t.album = album;
    t.title = title;
    t.codec = QStringLiteral("FLAC");
    t.durationMs = 300;
    return t;
}

} // namespace

// The on-device browser's reader against a temp dir standing in for a mounted stick.
class TestDeviceBrowser : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    Database m_db;
    QString m_mount;
    DeviceBrowseRequest m_request;

    QString dev(const QString &rel) const { return QDir(m_mount).filePath(rel); }
    bool tone(const QString &rel, const QString &tagList, const QString &encoder = QStringLiteral("flacenc"))
    {
        QDir().mkpath(QFileInfo(dev(rel)).path());
        return test::generateTone(dev(rel), encoder, 300, tagList).isEmpty();
    }
    struct Run
    {
        std::optional<DeviceBrowseSummary> summary;
        QList<DeviceTrack> tracks;
        int total = -1;
        QString dir;
        bool fellBack = false;
    };
    Run browse(const DeviceBrowseRequest &r, const std::atomic_bool *cancel = nullptr)
    {
        Run run;
        QString error;
        run.summary = DeviceBrowser::browse(
            m_db, r, cancel,
            [&](int total, const QString &dir, bool fellBack) {
                run.total = total;
                run.dir = dir;
                run.fellBack = fellBack;
            },
            [&](const QList<DeviceTrack> &batch, int, int) { run.tracks += batch; }, &error);
        if (!error.isEmpty())
            qWarning() << error;
        return run;
    }
    static const DeviceTrack *find(const QList<DeviceTrack> &tracks, const QString &title)
    {
        for (const DeviceTrack &t : tracks) {
            if (t.title == title)
                return &t;
        }
        return nullptr;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(m_db.open(m_dir.filePath(QStringLiteral("library.db"))));
        // The library: Hells Bells (AC/DC) and a compilation track by Björk.
        QVERIFY(m_db.upsertTrack(QStringLiteral("/lib/hells.flac"), 1, 1,
                                 tags(QStringLiteral("AC/DC"), QStringLiteral("Back in Black"), QStringLiteral("Hells Bells"))));
        TrackTags comp = tags(QStringLiteral("Björk"), QStringLiteral("Now 42"), QStringLiteral("Army of Me"));
        comp.albumArtist = QStringLiteral("Various Artists");
        QVERIFY(m_db.upsertTrack(QStringLiteral("/lib/army.flac"), 1, 1, comp));

        m_mount = m_dir.filePath(QStringLiteral("stick"));
        // On the stick: the same Hells Bells (other case, extra spaces), Army of Me on Björk's own
        // album (a compilation mismatch), a song the library hasn't, an untagged file, a text file.
        QVERIFY(tone(QStringLiteral("Music/ACDC/01.flac"),
                     QStringLiteral("title=\"hells  bells\",artist=\"ac/dc\",album=\"back in black\",track-number=(uint)1")));
        QVERIFY(tone(QStringLiteral("Music/Bjork/Post/02.mp3"),
                     QStringLiteral("title=\"Army of Me\",artist=\"Björk\",album=\"Post\",track-number=(uint)2"),
                     QStringLiteral("lamemp3enc ! id3v2mux")));
        QVERIFY(tone(QStringLiteral("Music/Other/new.flac"), QStringLiteral("title=\"Brand New\",artist=\"Nobody\",album=\"Fresh\"")));
        QVERIFY(tone(QStringLiteral("Music/untagged.flac"), QString()));
        QFile txt(dev(QStringLiteral("Music/readme.txt")));
        QVERIFY(txt.open(QIODevice::WriteOnly));
        txt.write("not music");
        txt.close();
        QVERIFY(tone(QStringLiteral("Backup/old.flac"), QStringLiteral("title=\"Old\",artist=\"Someone\"")));

        m_request = DeviceBrowseRequest{QStringLiteral("uuid:B"), m_mount, QStringLiteral("Music"), false};
    }

    void matcher()
    {
        const LibraryMatcher m({libTrack(QStringLiteral("/lib/a.flac"), QStringLiteral("AC/DC"), QStringLiteral("Back in Black"),
                                         QStringLiteral("Hells Bells"), 312'000),
                                libTrack(QStringLiteral("/lib/b.flac"), QStringLiteral("Björk"), QStringLiteral("Now 42"),
                                         QStringLiteral("Army of Me"), 230'000, QStringLiteral("Various Artists"))});
        QVERIFY(m.matches(deviceTrack(QStringLiteral(" ac/dc "), QStringLiteral("BACK IN  BLACK"), QStringLiteral("hells bells"))));
        // Compilation: artist + title, length within 3 s.
        QVERIFY(m.matches(deviceTrack(QStringLiteral("Björk"), QStringLiteral("Post"), QStringLiteral("Army of Me"), 231'500)));
        QVERIFY(!m.matches(deviceTrack(QStringLiteral("Björk"), QStringLiteral("Post"), QStringLiteral("Army of Me"), 240'000)));
        QVERIFY(!m.matches(deviceTrack(QStringLiteral("Björk"), QStringLiteral("Post"), QStringLiteral("Army of Me"), 0)));
        QVERIFY(!m.matches(deviceTrack(QStringLiteral("AC/DC"), QStringLiteral("Back in Black"), QStringLiteral("Shoot to Thrill"))));
        // No album: artist + title only with a matching length (a live version isn't the studio one).
        const LibraryMatcher loose({libTrack(QStringLiteral("/lib/c.flac"), QStringLiteral("Queen"), QString(),
                                             QStringLiteral("Love of My Life"), 219'000)});
        QVERIFY(!loose.matches(deviceTrack(QStringLiteral("Queen"), QString(), QStringLiteral("Love of My Life"), 300'000)));
        QVERIFY(loose.matches(deviceTrack(QStringLiteral("Queen"), QString(), QStringLiteral("Love of My Life"), 220'000)));
        QVERIFY(!loose.matches(deviceTrack(QString(), QString(), QStringLiteral("Love of My Life"), 219'000)));
        // Untagged never matches; a synced file matches through its source.
        QVERIFY(!m.matches(deviceTrack(QString(), QString(), QString())));
        QVERIFY(m.matches(deviceTrack(QString(), QString(), QString()), QStringLiteral("/lib/a.flac")));
        QVERIFY(!m.matches(deviceTrack(QString(), QString(), QString()), QStringLiteral("/lib/gone.flac")));
    }

    void firstBrowseReadsAndCaches()
    {
        const Run run = browse(m_request);
        QVERIFY(run.summary);
        QCOMPARE(run.dir, QStringLiteral("Music"));
        QVERIFY(!run.fellBack);
        QCOMPARE(run.total, 4); // audio only, music folder only
        QCOMPARE(run.summary->tracks, 4);
        QCOMPARE(run.summary->tagReads, 4);
        QCOMPARE(run.summary->cacheHits, 0);
        QCOMPARE(run.tracks.size(), 4);
        QVERIFY(find(run.tracks, QStringLiteral("hells  bells"))->inLibrary);
        QVERIFY(find(run.tracks, QStringLiteral("Army of Me"))->inLibrary);
        QVERIFY(!find(run.tracks, QStringLiteral("Brand New"))->inLibrary);
        const DeviceTrack *untagged = find(run.tracks, QString());
        QVERIFY(untagged);
        QVERIFY(!untagged->inLibrary);
        QCOMPARE(untagged->codec, QStringLiteral("FLAC"));
        QCOMPARE(run.summary->notInLibrary, 2);
        QVERIFY(run.summary->bytes > 0);
        const DeviceTrack *army = find(run.tracks, QStringLiteral("Army of Me"));
        QCOMPARE(army->codec, QStringLiteral("MP3"));
        QCOMPARE(army->trackNumber, 2);
        QVERIFY(army->durationMs > 0);
        QCOMPARE(m_db.deviceTracks(m_request.deviceId).size(), 4);
    }

    void secondBrowseUsesTheCache()
    {
        const Run run = browse(m_request);
        QCOMPARE(run.summary->cacheHits, 4);
        QCOMPARE(run.summary->tagReads, 0);
        QCOMPARE(run.tracks.size(), 4);
        QVERIFY(find(run.tracks, QStringLiteral("hells  bells"))->inLibrary); // flags worked out again
    }

    void changedAndDeletedFiles()
    {
        QVERIFY(QFile::remove(dev(QStringLiteral("Music/Other/new.flac"))));
        QTest::qWait(20); // another mtime for sure
        QVERIFY(tone(QStringLiteral("Music/Other/new.flac"), QStringLiteral("title=\"Renewed\",artist=\"Nobody\",album=\"Fresh\"")));
        QVERIFY(QFile::remove(dev(QStringLiteral("Music/untagged.flac"))));
        const Run run = browse(m_request);
        QCOMPARE(run.summary->tracks, 3);
        QCOMPARE(run.summary->tagReads, 1);
        QCOMPARE(run.summary->cacheHits, 2);
        QVERIFY(find(run.tracks, QStringLiteral("Renewed")));
        QCOMPARE(m_db.deviceTracks(m_request.deviceId).size(), 3); // the deleted one is forgotten
    }

    void wholeDeviceAndMissingFolder()
    {
        DeviceBrowseRequest whole = m_request;
        whole.wholeDevice = true;
        Run run = browse(whole);
        QCOMPARE(run.dir, QString());
        QCOMPARE(run.summary->tracks, 4); // + Backup/old.flac
        QCOMPARE(m_db.deviceTracks(m_request.deviceId).size(), 4);
        // Back to the music folder: the Backup row stays cached (it wasn't looked at).
        run = browse(m_request);
        QCOMPARE(run.summary->tracks, 3);
        QCOMPARE(m_db.deviceTracks(m_request.deviceId).size(), 4);
        // A music folder that isn't there: the whole device.
        DeviceBrowseRequest missing = m_request;
        missing.musicDir = QStringLiteral("Songs");
        run = browse(missing);
        QVERIFY(run.fellBack);
        QCOMPARE(run.summary->tracks, 4);
        QCOMPARE(run.summary->cacheHits, 4);
        // No profile: the whole device too.
        DeviceBrowseRequest none = m_request;
        none.musicDir.clear();
        QCOMPARE(browse(none).summary->tracks, 4);
    }

    void syncedFilesAreMarked()
    {
        ManifestEntry e;
        e.deviceId = m_request.deviceId;
        e.relPath = QStringLiteral("Music/Other/new.flac");
        e.sourcePath = QStringLiteral("/lib/hells.flac"); // pretend it came from the library
        QVERIFY(m_db.putManifestEntry(e));
        const Run run = browse(m_request);
        const DeviceTrack *renewed = find(run.tracks, QStringLiteral("Renewed"));
        QVERIFY(renewed->synced);
        QVERIFY(renewed->inLibrary);
        QCOMPARE(run.summary->synced, 1);
        QVERIFY(m_db.removeManifestEntry(e.deviceId, e.relPath));
    }

    void unreadableDevice()
    {
        DeviceBrowseRequest gone = m_request;
        gone.mountPoint = m_dir.filePath(QStringLiteral("nowhere"));
        QString error;
        QVERIFY(!DeviceBrowser::browse(m_db, gone, nullptr, {}, {}, &error));
        QVERIFY(!error.isEmpty());
        std::atomic_bool cancel{true};
        QVERIFY(!browse(m_request, &cancel).summary);
    }

    void browsesOffThread()
    {
        DeviceBrowser browser(m_db.connection().databaseName());
        QSignalSpy finished(&browser, &DeviceBrowser::finished);
        QSignalSpy batches(&browser, &DeviceBrowser::batchReady);
        QSignalSpy discovered(&browser, &DeviceBrowser::discovered);
        // A second start replaces the first: only its results arrive.
        browser.start(m_request);
        DeviceBrowseRequest whole = m_request;
        whole.wholeDevice = true;
        browser.start(whole);
        QVERIFY(browser.isBusy());
        QVERIFY(finished.wait(10000));
        QTest::qWait(100);
        QCOMPARE(finished.count(), 1);
        QVERIFY(!browser.isBusy());
        const auto s = finished.at(0).at(0).value<DeviceBrowseSummary>();
        QCOMPARE(s.tracks, 4);
        QCOMPARE(discovered.count(), 1);
        int tracks = 0;
        for (const QList<QVariant> &b : std::as_const(batches))
            tracks += static_cast<int>(b.at(0).value<QList<DeviceTrack>>().size());
        QCOMPARE(tracks, 4);
    }
};

QTEST_GUILESS_MAIN(TestDeviceBrowser)
#include "tst_devicebrowser.moc"

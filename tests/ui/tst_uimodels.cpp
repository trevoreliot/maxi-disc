// SPDX-License-Identifier: GPL-3.0-or-later
#include <QAbstractItemModelTester>
#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QQuickImageResponse>
#include <QRegularExpression>
#include <QQuickTextureFactory>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include "artfinder.h"
#include "coverimageprovider.h"
#include "devices.h"
#include "fakenetmd.h"
#include "minidisc.h"
#include "ondevice.h"
#include "sync.h"
#include "profileeditor.h"
#include "fixturelibrary.h"
#include "library.h"
#include "librarymodels.h"
#include "tageditor.h"
#include "tagreader.h"
#include "testmedia.h"

namespace {

constexpr int kTimeoutMs = 10000;

QStringList column(const QAbstractItemModel &m, const QByteArray &roleName)
{
    const int role = m.roleNames().key(roleName, -1);
    QStringList out;
    for (int i = 0; i < m.rowCount(); ++i)
        out << m.data(m.index(i, 0), role).toString();
    return out;
}

int idOf(const QAbstractItemModel &m, const QByteArray &idRole, const QByteArray &nameRole, const QString &name)
{
    const int id = m.roleNames().key(idRole, -1);
    const int nm = m.roleNames().key(nameRole, -1);
    for (int i = 0; i < m.rowCount(); ++i) {
        if (m.data(m.index(i, 0), nm).toString() == name)
            return m.data(m.index(i, 0), id).toInt();
    }
    return 0;
}

int countFiles(const QString &dir)
{
    int n = 0;
    QDirIterator it(dir, {QStringLiteral("*.jpg")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        ++n;
    }
    return n;
}

} // namespace

class TestUiModels : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    std::unique_ptr<Library> m_library;
    std::unique_ptr<Devices> m_devices;
    std::unique_ptr<Sync> m_sync;
    std::unique_ptr<OnDevice> m_onDevice;
    std::shared_ptr<mmm::test::FakeRecorder> m_recorder = std::make_shared<mmm::test::FakeRecorder>();
    std::unique_ptr<MiniDisc> m_miniDisc;
    QString m_cacheDir;

    template<typename Model>
    std::unique_ptr<Model> loaded(std::function<void(Model &)> setup = {})
    {
        auto m = std::make_unique<Model>();
        if (setup)
            setup(*m);
        if (!QTest::qWaitFor([&] { return !m->isLoading() && m->rowCount() > 0; }, kTimeoutMs))
            qWarning("model did not load");
        return m;
    }

    QString coverUrlOf(const QString &albumTitle)
    {
        auto albums = loaded<AlbumListModel>();
        const int title = albums->roleNames().key("title");
        const int url = albums->roleNames().key("coverUrl");
        for (int i = 0; i < albums->rowCount(); ++i) {
            if (albums->data(albums->index(i, 0), title).toString() == albumTitle)
                return albums->data(albums->index(i, 0), url).toString();
        }
        return {};
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        const QString db = mmm::test::buildFixtureLibrary(m_dir.path());
        QVERIFY(!db.isEmpty());
        m_library = std::make_unique<Library>(Library::Options{.databasePath = db, .scanOnStart = false, .watch = false});
        QVERIFY(m_library->isReady());
        m_devices = std::make_unique<Devices>(Devices::Options{.databasePath = db, .monitor = false});
        m_sync = std::make_unique<Sync>(Sync::Options{.databasePath = db, .cacheDir = m_dir.filePath(QStringLiteral("transcode"))});
        m_onDevice = std::make_unique<OnDevice>(OnDevice::Options{.databasePath = db});
        m_miniDisc = std::make_unique<MiniDisc>(MiniDisc::Options{mmm::test::fakeNetMd(m_recorder), false, mmm::test::fakeEncoder(),
                                                                  m_dir.filePath(QStringLiteral("md-record"))});
        m_cacheDir = m_dir.filePath(QStringLiteral("covers"));
    }

    void cleanupTestCase()
    {
        m_miniDisc.reset();
        m_onDevice.reset();
        m_sync.reset();
        m_devices.reset();
        m_library.reset();
    }

    void libraryCounts()
    {
        QTRY_COMPARE_WITH_TIMEOUT(m_library->trackCount(), 12, kTimeoutMs);
        QCOMPARE(m_library->albumCount(), 5);
    }

    void artists()
    {
        auto m = loaded<ArtistListModel>();
        QAbstractItemModelTester tester(m.get(), QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(column(*m, "name"), (QStringList{QStringLiteral("The Beatles"), QStringLiteral("Pink Floyd"),
                                                  QStringLiteral("Radiohead"), QStringLiteral("Various Artists")}));
        QCOMPARE(m->count(), 4);
        QCOMPARE(m->data(m->index(2, 0), m->roleNames().key("albumCount")).toInt(), 2);

        const QVariantMap row = m->get(2);
        QCOMPARE(row.value(QStringLiteral("name")).toString(), QStringLiteral("Radiohead"));
        QCOMPARE(row.value(QStringLiteral("albumCount")).toInt(), 2);
        QVERIFY(row.value(QStringLiteral("artistId")).toInt() > 0);
        QVERIFY(m->get(-1).isEmpty());
        QVERIFY(m->get(4).isEmpty());
    }

    void genres()
    {
        auto m = loaded<GenreListModel>();
        QAbstractItemModelTester tester(m.get(), QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(column(*m, "name"), (QStringList{QStringLiteral("Pop"), QStringLiteral("Rock")}));
    }

    void albumsAndFilters()
    {
        auto artists = loaded<ArtistListModel>();
        const int radiohead = idOf(*artists, "artistId", "name", QStringLiteral("Radiohead"));
        QVERIFY(radiohead > 0);

        auto m = loaded<AlbumListModel>();
        QAbstractItemModelTester tester(m.get(), QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(m->count(), 5);

        QSignalSpy reset(m.get(), &QAbstractItemModel::modelReset);
        m->setArtistId(radiohead);
        QTRY_VERIFY_WITH_TIMEOUT(!reset.isEmpty() && !m->isLoading(), kTimeoutMs);
        QCOMPARE(column(*m, "title"), (QStringList{QStringLiteral("OK Computer"), QStringLiteral("Kid A")}));

        auto genres = loaded<GenreListModel>();
        m->setArtistId(0);
        m->setGenreId(idOf(*genres, "genreId", "name", QStringLiteral("Pop")));
        QTRY_COMPARE_WITH_TIMEOUT(column(*m, "title"), QStringList{QStringLiteral("Now 42")}, kTimeoutMs);
    }

    void coverUrls()
    {
        QVERIFY(coverUrlOf(QStringLiteral("OK Computer")).startsWith(QLatin1String("image://cover/")));
        QVERIFY(!coverUrlOf(QStringLiteral("Kid A")).isEmpty());
        QVERIFY(coverUrlOf(QStringLiteral("Abbey Road")).isEmpty()); // no art → placeholder, no request

        // The id round-trips folder cover + art track, including non-ASCII paths.
        mmm::AlbumRow row;
        row.id = 7;
        row.coverPath = QStringLiteral("/m/Sigur Rós/Ágætis byrjun/cover.jpg");
        row.artTrackPath = QStringLiteral("/m/Sigur Rós/Ágætis byrjun/01 Svefn-g-englar.flac");
        const QString id = AlbumListModel::coverUrl(row).mid(QStringLiteral("image://cover/").size());
        const auto [cover, track] = CoverLoader::decodeId(id);
        QCOMPARE(cover, row.coverPath);
        QCOMPARE(track, row.artTrackPath);
    }

    void albumTracks()
    {
        auto albums = loaded<AlbumListModel>();
        const int wall = idOf(*albums, "albumId", "title", QStringLiteral("The Wall"));
        auto m = loaded<TrackListModel>([wall](TrackListModel &t) { t.setAlbumId(wall); });
        QAbstractItemModelTester tester(m.get(), QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(column(*m, "title"), (QStringList{QStringLiteral("In the Flesh?"), QStringLiteral("The Thin Ice"),
                                                   QStringLiteral("Hey You")}));
        QCOMPARE(m->discCount(), 2);
        QCOMPARE(m->totalDurationMs(), 181000 + 182000 + 181000);
        QCOMPARE(column(*m, "duration").first(), QStringLiteral("3:01"));

        m->setAlbumId(0);
        QTRY_COMPARE_WITH_TIMEOUT(m->count(), 0, kTimeoutMs);
    }

    void searchDropsStaleResults()
    {
        auto m = std::make_unique<TrackListModel>();
        QAbstractItemModelTester tester(m.get(), QAbstractItemModelTester::FailureReportingMode::QtTest);
        // Typing fast: only the last query's results may land.
        m->setSearchText(QStringLiteral("r"));
        m->setSearchText(QStringLiteral("ra"));
        m->setSearchText(QStringLiteral("abbey"));
        QTRY_VERIFY_WITH_TIMEOUT(!m->isLoading() && m->count() > 0, kTimeoutMs);
        QTest::qWait(100); // any stale results would arrive here
        QCOMPARE(column(*m, "album"), (QStringList{QStringLiteral("Abbey Road"), QStringLiteral("Abbey Road")}));

        m->setSearchText(QStringLiteral("zzzz-no-match"));
        QTRY_VERIFY_WITH_TIMEOUT(!m->isLoading(), kTimeoutMs);
        QCOMPARE(m->count(), 0);

        // Search takes precedence over albumId; clearing it falls back to the album.
        auto albums = loaded<AlbumListModel>();
        m->setAlbumId(idOf(*albums, "albumId", "title", QStringLiteral("Kid A")));
        QTRY_VERIFY_WITH_TIMEOUT(!m->isLoading(), kTimeoutMs);
        QCOMPARE(m->count(), 0);
        m->setSearchText(QString());
        QTRY_COMPARE_WITH_TIMEOUT(m->count(), 2, kTimeoutMs);
    }

    void reloadWithSameRowsKeepsModel()
    {
        // A rescan that didn't change this view must not reset it (views would jump to the top).
        auto m = loaded<AlbumListModel>();
        QSignalSpy reset(m.get(), &QAbstractItemModel::modelReset);
        QSignalSpy changed(m.get(), &QAbstractItemModel::dataChanged);
        Q_EMIT m_library->changed();
        QTRY_VERIFY_WITH_TIMEOUT(!changed.isEmpty(), kTimeoutMs);
        QCOMPARE(reset.count(), 0);
        QCOMPARE(m->count(), 5);
    }

    void coverBuckets()
    {
        QCOMPARE(CoverLoader::bucketFor(QSize()), 256);
        QCOMPARE(CoverLoader::bucketFor(QSize(100, 100)), 128);
        QCOMPARE(CoverLoader::bucketFor(QSize(129, 50)), 256);
        QCOMPARE(CoverLoader::bucketFor(QSize(340, 340)), 512);
        QCOMPARE(CoverLoader::bucketFor(QSize(4000, 4000)), 1024);
    }

    void coverFromFolderArtIsCached()
    {
        const QString url = coverUrlOf(QStringLiteral("OK Computer"));
        const QString id = url.mid(QStringLiteral("image://cover/").size());
        QDir(m_cacheDir).removeRecursively();

        QImage img = CoverLoader::load(id, QSize(100, 100), m_cacheDir);
        QVERIFY(!img.isNull());
        QCOMPARE(img.size(), QSize(128, 128)); // 300×300 scaled into the 128 bucket
        QCOMPARE(img.pixelColor(10, 10).red() > 150, true);
        QCOMPARE(countFiles(m_cacheDir), 1);

        img = CoverLoader::load(id, QSize(120, 120), m_cacheDir); // same bucket → cache hit
        QCOMPARE(img.size(), QSize(128, 128));
        QCOMPARE(countFiles(m_cacheDir), 1);

        img = CoverLoader::load(id, QSize(1000, 1000), m_cacheDir); // never upscaled
        QCOMPARE(img.size(), QSize(300, 300));
        QCOMPARE(countFiles(m_cacheDir), 2);
    }

    void coverFromEmbeddedArt()
    {
        const QString id = coverUrlOf(QStringLiteral("Kid A")).mid(QStringLiteral("image://cover/").size());
        const QImage img = CoverLoader::load(id, QSize(100, 100), m_cacheDir);
        QVERIFY(!img.isNull());
        QCOMPARE(img.size(), QSize(128, 64)); // 200×100 keeps its aspect ratio
        QVERIFY(img.pixelColor(5, 5).blue() > 150);
    }

    void coverMissingSourceIsNull()
    {
        mmm::AlbumRow row;
        row.id = 1;
        row.coverPath = m_dir.filePath(QStringLiteral("nope/cover.jpg"));
        const QString id = AlbumListModel::coverUrl(row).mid(QStringLiteral("image://cover/").size());
        QVERIFY(CoverLoader::load(id, QSize(100, 100), m_cacheDir).isNull());
        QVERIFY(CoverLoader::load(QStringLiteral("garbage"), QSize(), m_cacheDir).isNull());
    }

    void asyncProvider()
    {
        CoverImageProvider provider(m_cacheDir);
        const QString id = coverUrlOf(QStringLiteral("OK Computer")).mid(QStringLiteral("image://cover/").size());

        std::unique_ptr<QQuickImageResponse> response(provider.requestImageResponse(id, QSize(200, 200)));
        QSignalSpy finished(response.get(), &QQuickImageResponse::finished);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kTimeoutMs);
        std::unique_ptr<QQuickTextureFactory> tf(response->textureFactory());
        QVERIFY(tf);
        QCOMPARE(tf->image().size(), QSize(256, 256));
        QVERIFY(response->errorString().isEmpty());

        // Cancelled and deleted at once (scrolling fast): no crash, no use-after-free.
        for (int i = 0; i < 20; ++i) {
            std::unique_ptr<QQuickImageResponse> r(provider.requestImageResponse(id, QSize(64 + i, 64)));
            QSignalSpy done(r.get(), &QQuickImageResponse::finished);
            r->cancel();
            QCOMPARE(done.count(), 1); // cancel() still reports finished
        }
        QTest::qWait(200); // let the orphaned runnables finish and their results be dropped
    }

    void tagEditorChanges()
    {
        QString error;
        const auto c = TagEditor::toChanges({{QStringLiteral("title"), QStringLiteral("T")},
                                             {QStringLiteral("year"), QStringLiteral(" 1999 ")},
                                             {QStringLiteral("trackTotal"), QString()},
                                             {QStringLiteral("compilation"), true}},
                                            &error);
        QVERIFY2(c, qPrintable(error));
        QCOMPARE(c->title, std::optional<QString>(QStringLiteral("T")));
        QCOMPARE(c->year, std::optional<int>(1999));
        QCOMPARE(c->trackTotal, std::optional<int>(0)); // "" clears
        QCOMPARE(c->compilation, std::optional<bool>(true));
        QVERIFY(!c->artist && !c->trackNumber);

        QVERIFY(!TagEditor::toChanges({{QStringLiteral("year"), QStringLiteral("199x")}}, &error));
        QCOMPARE(error, QStringLiteral("Year must be a number from 0 to 9999."));
        QVERIFY(!TagEditor::toChanges({{QStringLiteral("discNumber"), QStringLiteral("1000")}}, &error));
        QVERIFY(!TagEditor::toChanges({{QStringLiteral("bogus"), QStringLiteral("x")}}, &error));
    }

    // Values shared by all tracks, and which fields differ.
    void tagEditorMixedValues()
    {
        const QString dir = m_dir.filePath(QStringLiteral("loose"));
        QDir().mkpath(dir);
        const QString a = dir + QStringLiteral("/a.flac");
        const QString b = dir + QStringLiteral("/b.flac");
        QCOMPARE(mmm::test::generateTone(a, QStringLiteral("flacenc"), 300,
                                         QStringLiteral(R"(title="A",artist="X",album="Same")")), QString());
        QCOMPARE(mmm::test::generateTone(b, QStringLiteral("flacenc"), 300,
                                         QStringLiteral(R"(title="B",artist="Y",album="Same")")), QString());

        TagEditor editor;
        editor.setPaths({a, b, dir + QStringLiteral("/missing.flac")});
        QTRY_VERIFY_WITH_TIMEOUT(!editor.isLoading(), kTimeoutMs);
        QCOMPARE(editor.trackCount(), 3);
        QCOMPARE(editor.values().value(QStringLiteral("album")).toString(), QStringLiteral("Same"));
        QCOMPARE(editor.values().value(QStringLiteral("artist")).toString(), QString());
        QVERIFY(editor.mixedFields().contains(QStringLiteral("artist")));
        QVERIFY(editor.mixedFields().contains(QStringLiteral("title")));
        QVERIFY(!editor.mixedFields().contains(QStringLiteral("album")));
        QCOMPARE(editor.errorString(), QStringLiteral("1 of 3 files can't be read."));
    }

    // Saving through Library: file + library entry change, listeners hear about it.
    // (Kid A/01.flac is the fixture's one real file; its title is restored at the end.)
    void tagEditorSaves()
    {
        const QString kidA = m_dir.filePath(QStringLiteral("Radiohead/Kid A/01.flac"));
        const auto albumIdOf = [] {
            const QList<mmm::TrackRow> rows = Library::instance()
                ->runQuery([](const mmm::LibraryQuery &q) { return q.search(QStringLiteral("everything")); })
                .result();
            return rows.isEmpty() ? qint64(0) : rows.front().albumId;
        };
        const qint64 albumId = albumIdOf();
        QVERIFY(albumId > 0);

        TagEditor editor;
        editor.setPaths({kidA});
        QTRY_VERIFY_WITH_TIMEOUT(!editor.isLoading(), kTimeoutMs);
        QCOMPARE(editor.values().value(QStringLiteral("title")).toString(), QStringLiteral("Everything in Its Right Place"));
        QCOMPARE(editor.values().value(QStringLiteral("album")).toString(), QStringLiteral("Kid A"));
        QVERIFY(editor.mixedFields().isEmpty());

        QSignalSpy finished(&editor, &TagEditor::finished);
        QSignalSpy edited(m_library.get(), &Library::tracksEdited);
        QSignalSpy changed(m_library.get(), &Library::changed);
        QVERIFY(!editor.apply({{QStringLiteral("year"), QStringLiteral("abc")}}));
        QVERIFY(!editor.errorString().isEmpty());
        QVERIFY(editor.apply({{QStringLiteral("title"), QStringLiteral("Everything (edited)")},
                              {QStringLiteral("composer"), QStringLiteral("Yorke")}}));
        QVERIFY(editor.isSaving());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, kTimeoutMs);
        QVERIFY(finished.front().at(0).toBool());
        QCOMPARE(finished.front().at(2).toInt(), int(albumId)); // still in Kid A
        QVERIFY(!editor.isSaving());
        QCOMPARE(edited.count(), 1);
        const auto rows = edited.front().at(0).value<QList<mmm::TrackRow>>();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.front().title, QStringLiteral("Everything (edited)"));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(m_library->statusMessage(), QStringLiteral("Saved tags"));
        QCOMPARE(mmm::TagReader::read(kidA)->composer, QStringLiteral("Yorke"));

        // Nothing changed: finishes at once, writes nothing.
        QVERIFY(editor.apply({}));
        QCOMPARE(finished.count(), 2);
        QCOMPARE(edited.count(), 1);

        // A file that's gone: reported, and the dialog's editor keeps the error.
        editor.setPaths({kidA, m_dir.filePath(QStringLiteral("Radiohead/Kid A/02.flac"))});
        QTRY_VERIFY_WITH_TIMEOUT(!editor.isLoading(), kTimeoutMs);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^Could not save tags of 1 of 2")));
        QVERIFY(editor.apply({{QStringLiteral("composer"), QString()}}));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 3, kTimeoutMs);
        QVERIFY(!finished.back().at(0).toBool());
        QVERIFY(editor.errorString().contains(QStringLiteral("02.flac: file not found")));
        QVERIFY(m_library->errorString().startsWith(QStringLiteral("Could not save tags of 1 of 2 tracks.")));
        m_library->clearError();

        editor.setPaths({kidA});
        QTRY_VERIFY_WITH_TIMEOUT(!editor.isLoading(), kTimeoutMs);
        QVERIFY(editor.apply({{QStringLiteral("title"), QStringLiteral("Everything in Its Right Place")}}));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 4, kTimeoutMs);
        QCOMPARE(mmm::TagReader::read(kidA)->title, QStringLiteral("Everything in Its Right Place"));
        QCOMPARE(albumIdOf(), albumId);
    }

    // --- Devices + profiles (M8) ---

    void devicesModel()
    {
        Devices &d = *m_devices;
        QAbstractItemModelTester tester(&d, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(d.count(), 0);
        QCOMPARE(d.currentIndex(), -1);
        QVERIFY(d.current().isEmpty());

        QSignalSpy current(&d, &Devices::currentChanged);
        d.setTestDevices({
            QVariantMap{{QStringLiteral("id"), QStringLiteral("uuid:B")}, {QStringLiteral("name"), QStringLiteral("STICK")},
                        {QStringLiteral("totalBytes"), 32'000'000'000LL}},
            QVariantMap{{QStringLiteral("id"), QStringLiteral("uuid:A")}, {QStringLiteral("name"), QStringLiteral("DAP")},
                        {QStringLiteral("vendor"), QStringLiteral("FiiO")}, {QStringLiteral("model"), QStringLiteral("M3K")},
                        {QStringLiteral("fsType"), QStringLiteral("exfat")},
                        {QStringLiteral("mountPoint"), QStringLiteral("/run/media/me/DAP")},
                        {QStringLiteral("totalBytes"), 64'000'000'000LL}, {QStringLiteral("freeBytes"), 14'200'000'000LL}},
        });
        QCOMPARE(d.count(), 2);
        QCOMPARE(d.currentIndex(), 0); // the first is selected, ready to set up
        QVERIFY(current.count() >= 1);
        QCOMPARE(column(d, "name"), (QStringList{QStringLiteral("STICK"), QStringLiteral("DAP")}));
        QCOMPARE(column(d, "spaceText"), (QStringList{QStringLiteral("32.0 GB · not mounted"),
                                                      QStringLiteral("14.2 GB free of 64.0 GB")}));
        QCOMPARE(column(d, "details")[1], QStringLiteral("FiiO M3K · exfat"));
        QCOMPARE(d.current().value(QStringLiteral("hasProfile")).toBool(), false);
        QVERIFY(d.currentProfile().isEmpty());

        // A saved profile shows up in the row and the summary; the selection stays put.
        d.setCurrentIndex(1);
        mmm::DeviceProfile p = *mmm::DeviceProfiles::builtIn(QStringLiteral("generic-dap"));
        p.deviceId = QStringLiteral("uuid:A");
        p.name = QStringLiteral("Running DAP");
        QVERIFY(d.manager()->saveProfile(p));
        QCOMPARE(d.currentIndex(), 1);
        QCOMPARE(column(d, "profileName"), (QStringList{QString(), QStringLiteral("Running DAP")}));
        const QVariantMap summary = d.currentProfile();
        QCOMPARE(summary.value(QStringLiteral("name")).toString(), QStringLiteral("Running DAP"));
        QCOMPARE(summary.value(QStringLiteral("formats")).toString(), QStringLiteral("MP3"));
        QCOMPARE(summary.value(QStringLiteral("example")).toString(),
                 QStringLiteral("Music/AC_DC/Back in Black/2-04 Back in Black.mp3"));

        // Unplugging the selected device moves the selection to what's left.
        d.setTestDevices({QVariantMap{{QStringLiteral("id"), QStringLiteral("uuid:B")}, {QStringLiteral("name"), QStringLiteral("STICK")}}});
        QCOMPARE(d.count(), 1);
        QCOMPARE(d.currentIndex(), 0);
        d.toggleMountCurrent(); // no detection in tests: nothing happens, nothing breaks
        QVERIFY(d.manager()->deleteProfile(QStringLiteral("uuid:A")));
        d.setTestDevices({});
        QCOMPARE(d.currentIndex(), -1);
    }

    void profileEditor()
    {
        m_devices->setTestDevices({QVariantMap{{QStringLiteral("id"), QStringLiteral("uuid:E")},
                                               {QStringLiteral("name"), QStringLiteral("THUMB")}}});
        ProfileEditor e;
        QSignalSpy changed(&e, &ProfileEditor::changed);
        QVERIFY(e.load(QStringLiteral("uuid:E")));
        QVERIFY(e.isNew());
        QCOMPARE(e.deviceName(), QStringLiteral("THUMB"));
        QCOMPARE(e.name(), QStringLiteral("THUMB")); // named after the device
        QCOMPARE(e.presetId(), QStringLiteral("usb-flash"));
        QCOMPARE(e.example(), QStringLiteral("Music/AC_DC/Back in Black/2-04 Back in Black.flac"));
        QVERIFY(e.exampleNote().contains(QStringLiteral("copied")));
        QVERIFY(e.canSave());

        // Unticking FLAC: the example becomes an MP3.
        e.setCodecEnabled(QStringLiteral("FLAC"), false);
        QVERIFY(!e.playableCodecs().contains(QStringLiteral("FLAC")));
        QCOMPARE(e.example(), QStringLiteral("Music/AC_DC/Back in Black/2-04 Back in Black.mp3"));
        QVERIFY(e.exampleNote().contains(QStringLiteral("MP3 V0")));
        e.setTranscodeTarget(QStringLiteral("mp3_cbr_320"));
        QVERIFY(e.exampleNote().contains(QStringLiteral("320")));
        e.setTranscodeTarget(QStringLiteral("bogus")); // ignored
        QCOMPARE(e.transcodeTarget(), QStringLiteral("mp3_cbr_320"));

        // A preset replaces the settings but keeps the name.
        e.setName(QStringLiteral("Car"));
        e.applyPreset(QStringLiteral("generic-dap"));
        QCOMPARE(e.name(), QStringLiteral("Car"));
        QCOMPARE(e.playableCodecs(), QStringList{QStringLiteral("MP3")});
        QCOMPARE(e.artworkPolicy(), QStringLiteral("shrink_500"));
        QCOMPARE(e.transcodeTarget(), QStringLiteral("mp3_v0"));

        // A broken layout can't be saved.
        e.setPathLayout(QStringLiteral("{albumartist}/{album"));
        QVERIFY(!e.layoutError().isEmpty());
        QVERIFY(!e.canSave());
        QVERIFY(!e.save());
        QVERIFY(!e.errorString().isEmpty());
        e.setPathLayout(QStringLiteral("{albumartist} - {album}/{track:02} {title}"));
        e.setMusicDir(QStringLiteral("/Car Music/"));
        QCOMPARE(e.example(), QStringLiteral("Car Music/AC_DC - Back in Black/04 Back in Black.mp3"));
        QVERIFY(changed.count() > 5);

        QSignalSpy saved(&e, &ProfileEditor::saved);
        QVERIFY(e.save());
        QCOMPARE(saved.count(), 1);
        QVERIFY(!e.isNew());
        QCOMPARE(m_devices->current().value(QStringLiteral("profileName")).toString(), QStringLiteral("Car"));

        // Reloading shows the saved profile; removing forgets it.
        ProfileEditor again;
        again.load(QStringLiteral("uuid:E"));
        QVERIFY(!again.isNew());
        QCOMPARE(again.name(), QStringLiteral("Car"));
        QCOMPARE(again.musicDir(), QStringLiteral("Car Music"));
        QVERIFY(again.remove());
        QCOMPARE(m_devices->current().value(QStringLiteral("hasProfile")).toBool(), false);
        m_devices->setTestDevices({});
    }

    void importSeveralPlaylists()
    {
        const QStringList paths = m_library->runQuery([](const mmm::LibraryQuery &q) {
            QStringList out;
            for (const mmm::AlbumRow &a : q.albums())
                for (const mmm::TrackRow &t : q.albumTracks(a.id))
                    out << t.path;
            return out;
        }).result();
        QVERIFY(paths.size() >= 3);
        auto writeList = [&](const QString &name, const QByteArray &body) {
            const QString file = m_dir.filePath(name);
            QFile f(file);
            if (!f.open(QIODevice::WriteOnly) || f.write(body) != body.size())
                return QUrl();
            return QUrl::fromLocalFile(file);
        };
        const QUrl one = writeList(QStringLiteral("One.m3u"), (paths[0] + QLatin1Char('\n') + paths[1] + QLatin1Char('\n')).toUtf8());
        const QUrl two = writeList(QStringLiteral("Two.m3u8"), (paths[2] + QStringLiteral("\n/nowhere/x.mp3\n")).toUtf8());
        const int id = m_library->importPlaylists({one, two, QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("missing.m3u")))});
        QVERIFY(id > 0);
        QCOMPARE(m_library->statusMessage(), QStringLiteral("Imported 2 playlists, 3 tracks (1 of 4 not in the library)"));
        QVERIFY(m_library->errorString().contains(QStringLiteral("missing.m3u")));
        m_library->clearError();
        const QList<mmm::PlaylistRow> lists = m_library->runQuery([](const mmm::LibraryQuery &q) { return q.playlists(); }).result();
        int imported = 0;
        for (const mmm::PlaylistRow &p : lists) {
            if (p.name == QStringLiteral("One") || p.name == QStringLiteral("Two")) {
                ++imported;
                if (p.name == QStringLiteral("Two"))
                    QCOMPARE(p.id, qint64(id)); // the last one imported
                QVERIFY(m_library->deletePlaylist(static_cast<int>(p.id)));
            }
        }
        QCOMPARE(imported, 2);
    }

    // --- On-device browser (M9b) ---
    void onDeviceModelGroupsAndFolds()
    {
        auto track = [](const char *rel, const char *artist, const char *album, const char *title, int no, qint64 size,
                        bool inLibrary = true, const char *albumArtist = "") {
            mmm::DeviceTrack t;
            t.relPath = QString::fromUtf8(rel);
            t.artist = QString::fromUtf8(artist);
            t.album = QString::fromUtf8(album);
            t.title = QString::fromUtf8(title);
            t.albumArtist = QString::fromUtf8(albumArtist);
            t.trackNumber = no;
            t.size = size;
            t.inLibrary = inLibrary;
            return t;
        };
        OnDeviceModel m;
        QAbstractItemModelTester tester(&m, QAbstractItemModelTester::FailureReportingMode::QtTest);
        m.beginBrowse();
        m.addTracks({track("Music/b/2.mp3", "The Beatles", "Abbey Road", "Something", 2, 100),
                     track("Music/x.mp3", "", "", "", 0, 50, false),
                     track("Music/b/1.mp3", "The Beatles", "Abbey Road", "Come Together", 1, 100),
                     track("Music/n/1.mp3", "Björk", "Now 42", "Army of Me", 1, 10, false, "Various Artists"),
                     track("Music/a/1.mp3", "ABBA", "Gold", "SOS", 1, 30)});
        m.endBrowse();
        auto label = [&](int row) { return m.data(m.index(row), OnDeviceModel::LabelRole).toString(); };
        auto kind = [&](int row) { return m.data(m.index(row), OnDeviceModel::KindRole).toInt(); };
        // ABBA, The Beatles (as "Beatles"), Various Artists, then Unknown Artist.
        const QStringList expected{QStringLiteral("ABBA"), QStringLiteral("Gold"), QStringLiteral("SOS"),
                                   QStringLiteral("The Beatles"), QStringLiteral("Abbey Road"), QStringLiteral("Come Together"),
                                   QStringLiteral("Something"), QStringLiteral("Various Artists"), QStringLiteral("Now 42"),
                                   QStringLiteral("Army of Me"), QStringLiteral("Unknown Artist"), QStringLiteral("Unknown Album"),
                                   QStringLiteral("x")};
        QStringList labels;
        for (int i = 0; i < m.rowCount(); ++i)
            labels << label(i);
        QCOMPARE(labels, expected);
        QCOMPARE(kind(0), int(OnDeviceModel::ArtistRow));
        QCOMPARE(kind(1), int(OnDeviceModel::AlbumRow));
        QCOMPARE(kind(2), int(OnDeviceModel::TrackRow));
        QCOMPARE(m.data(m.index(9), OnDeviceModel::DetailRole).toString(), QStringLiteral("Björk")); // compilation artist
        QCOMPARE(m.data(m.index(3), OnDeviceModel::TrackCountRole).toInt(), 2);
        QCOMPARE(m.data(m.index(7), OnDeviceModel::NotInLibraryCountRole).toInt(), 1);
        QCOMPARE(m.trackCount(), 5);
        QCOMPARE(m.notInLibraryCount(), 2);
        QCOMPARE(m.totalBytes(), qint64(290));

        // Folding the Beatles hides their album and tracks; the cursor stays on the header.
        m.setCurrentIndex(3);
        m.toggle(3);
        QCOMPARE(m.rowCount(), expected.size() - 3);
        QCOMPARE(m.currentIndex(), 3);
        QVERIFY(!m.data(m.index(3), OnDeviceModel::ExpandedRole).toBool());
        QCOMPARE(m.trackCount(), 5); // still counted
        m.toggle(3);
        QCOMPARE(m.rowCount(), expected.size());

        // New results move rows: the cursor follows its row.
        m.setCurrentIndex(5); // Come Together
        m.addTracks({track("Music/0.mp3", "AC/DC", "Back in Black", "Hells Bells", 1, 20)});
        m.rebuild();
        QCOMPARE(label(m.currentIndex()), QStringLiteral("Come Together"));
        m.move(-100);
        QCOMPARE(m.currentIndex(), 0);
        m.move(1000);
        QCOMPARE(m.currentIndex(), m.rowCount() - 1);

        // A browse that no longer sees a file drops it.
        m.beginBrowse();
        m.addTracks({track("Music/a/1.mp3", "ABBA", "Gold", "SOS", 1, 30)});
        m.endBrowse();
        QCOMPARE(m.trackCount(), 1);
        m.clear();
        QCOMPARE(m.rowCount(), 0);
        QCOMPARE(m.currentIndex(), -1);
    }

    void onDeviceBrowsesTheSelectedDevice()
    {
        // The fixture folder stands in for a stick (no profile: the whole device).
        m_devices->setTestDevices({QVariantMap{{QStringLiteral("id"), QStringLiteral("uuid:ONDEV")},
                                               {QStringLiteral("name"), QStringLiteral("ONDEV")},
                                               {QStringLiteral("mountPoint"), m_dir.path()}}});
        OnDevice &od = *m_onDevice;
        QVERIFY(!od.isBrowsing()); // not shown: not read
        od.setActive(true);
        QVERIFY(od.isBrowsing());
        QTRY_VERIFY_WITH_TIMEOUT(!od.isBrowsing(), 10000);
        QVERIFY(od.errorString().isEmpty());
        QCOMPARE(od.scannedDir(), QString());
        QVERIFY(od.model()->trackCount() >= 1); // Kid A/01.flac at least
        // Kid A/01.flac is a library file; other tests leave files the library hasn't ("loose/").
        bool kidA = false;
        for (int i = 0; i < od.model()->rowCount(); ++i) {
            const QModelIndex x = od.model()->index(i);
            const QString rel = x.data(OnDeviceModel::RelPathRole).toString();
            if (x.data(OnDeviceModel::KindRole).toInt() != OnDeviceModel::TrackRow)
                continue;
            if (rel.endsWith(QStringLiteral("Kid A/01.flac"))) {
                kidA = true;
                QVERIFY(x.data(OnDeviceModel::InLibraryRole).toBool());
            } else if (rel.startsWith(QStringLiteral("loose/"))) {
                QVERIFY(!x.data(OnDeviceModel::InLibraryRole).toBool());
            }
        }
        QVERIFY(kidA);
        QCOMPARE(od.done(), od.total());

        // Unmounted: nothing to show.
        m_devices->setTestDevices({QVariantMap{{QStringLiteral("id"), QStringLiteral("uuid:ONDEV")},
                                               {QStringLiteral("name"), QStringLiteral("ONDEV")}}});
        QCOMPARE(od.model()->trackCount(), 0);
        QVERIFY(!od.isBrowsing());
        od.setActive(false);
        m_devices->setTestDevices({});
    }

    // --- NetMD MiniDisc (M10): the in-memory recorder ---
    void miniDiscFollowsTheSelectedDevice()
    {
        m_devices->setTestDevices({QVariantMap{{QStringLiteral("id"), QStringLiteral("uuid:STICK")},
                                               {QStringLiteral("name"), QStringLiteral("STICK")}}});
        MiniDisc &md = *m_miniDisc;
        md.setTestDevices({QVariantMap{{QStringLiteral("id"), QStringLiteral("netmd:test")},
                                       {QStringLiteral("name"), QStringLiteral("Sony MZ-NE410")}}});
        QCOMPARE(m_devices->count(), 2); // listed after the USB device
        QVERIFY(!md.isSelected());
        m_devices->setCurrentIndex(1);
        QCOMPARE(m_devices->current().value(QStringLiteral("netMd")).toBool(), true);
        QCOMPARE(m_devices->current().value(QStringLiteral("details")).toString(), QStringLiteral("MiniDisc recorder (NetMD)"));
        QVERIFY(md.isSelected());
        QCOMPARE(md.deviceName(), QStringLiteral("Sony MZ-NE410"));
        QTRY_VERIFY(md.hasDisc());
        QCOMPARE(md.discTitle(), QStringLiteral("Scratch"));
        QCOMPARE(md.trackCount(), 3);
        QCOMPARE(md.groupCount(), 1);
        QCOMPARE(md.statusText(), QStringLiteral("3 tracks"));
        QVERIFY(md.capacityText().contains(QStringLiteral("free of")));
        QVERIFY(md.usedFraction() > 0.0);

        MdTrackModel &t = *md.tracks();
        QCOMPARE(t.currentIndex(), 0);
        QCOMPARE(t.data(t.index(0), MdTrackModel::GroupTitleRole).toString(), QStringLiteral("Side A"));
        QCOMPARE(t.data(t.index(1), MdTrackModel::InGroupRole).toBool(), true);
        QCOMPARE(t.data(t.index(2), MdTrackModel::InGroupRole).toBool(), false);
        QCOMPARE(t.data(t.index(1), MdTrackModel::ModeRole).toString(), QStringLiteral("LP2"));
        QCOMPARE(t.data(t.index(2), MdTrackModel::ModeRole).toString(), QStringLiteral("LP4 mono"));
        QCOMPARE(t.data(t.index(2), MdTrackModel::DurationTextRole).toString(), QStringLiteral("1:02:05"));
        QCOMPARE(t.data(t.index(2), MdTrackModel::ProtectedRole).toBool(), true);

        // What a title becomes; renaming writes the clean one.
        const QVariantMap p = md.preview(QStringLiteral("Café “Live”"), false);
        QCOMPARE(p.value(QStringLiteral("text")).toString(), QStringLiteral("Cafe \"Live\""));
        QVERIFY(p.value(QStringLiteral("changed")).toBool());
        t.setCurrentIndex(1);
        md.renameTrack(1, QStringLiteral("Café “Live”"));
        QVERIFY(md.isBusy());
        QTRY_VERIFY(!md.isBusy());
        QCOMPARE(t.titleAt(1), QStringLiteral("Cafe \"Live\""));
        QCOMPARE(t.currentIndex(), 1); // a rename keeps the cursor
        md.renameDisc(QStringLiteral("Mixtape"));
        QTRY_VERIFY(!md.isBusy());
        QCOMPARE(md.discTitle(), QStringLiteral("Mixtape"));
        QCOMPARE(md.groupCount(), 1);
        md.renameDisc(QStringLiteral("Scratch")); // put it back (shared fake)
        QTRY_VERIFY(!md.isBusy());

        // Back to the USB device: the recorder is let go.
        m_devices->setCurrentIndex(0);
        QVERIFY(!md.isSelected());
        QVERIFY(!md.hasDisc());
        md.setTestDevices({});
        m_devices->setTestDevices({});
        QCOMPARE(m_devices->count(), 0);
    }

    void miniDiscRecordsTheQueue()
    {
        m_devices->setTestDevices({});
        MiniDisc &md = *m_miniDisc;
        md.setTestDevices({QVariantMap{{QStringLiteral("id"), QStringLiteral("netmd:rec")}}});
        m_devices->setCurrentIndex(0);
        QTRY_VERIFY(md.hasDisc());
        const int before = md.trackCount();
        const qint64 kidA = m_library->runQuery([](const mmm::LibraryQuery &q) {
            for (const mmm::AlbumRow &a : q.albums())
                if (a.title == QStringLiteral("Kid A"))
                    return a.id;
            return qint64(0);
        }).result();

        QVERIFY(!md.recordView());
        md.addItem(QStringLiteral("album"), static_cast<int>(kidA), QStringLiteral("Kid A"));
        QTRY_COMPARE(md.queue()->count(), 2);
        QVERIFY(md.recordView());
        md.addItem(QStringLiteral("album"), static_cast<int>(kidA), QStringLiteral("Kid A")); // already queued
        QTest::qWait(100);
        QCOMPARE(md.queue()->count(), 2);
        // Only Kid A/01.flac exists in the fixture: the plan refuses until 02 goes.
        QVERIFY(md.plan().value(QStringLiteral("error")).toString().contains(QStringLiteral("not found")));
        QVERIFY(!md.record());
        md.removeQueued(1);
        QVERIFY2(md.plan().value(QStringLiteral("canRecord")).toBool(), qPrintable(md.plan().value(QStringLiteral("error")).toString()));
        QCOMPARE(md.queue()->data(md.queue()->index(0), MdQueueModel::StateRole).toString(), QStringLiteral("pending"));

        QSignalSpy recording(&md, &MiniDisc::recordingChanged);
        QVERIFY(md.record());
        QVERIFY(md.isRecording());
        QCOMPARE(md.statusText(), QStringLiteral("Recording 1 of 1"));
        QTRY_VERIFY(!md.isRecording());
        QCOMPARE(md.resultText(), QStringLiteral("Recorded 1 track."));
        QCOMPARE(md.queue()->data(md.queue()->index(0), MdQueueModel::StateRole).toString(), QStringLiteral("done"));
        QCOMPARE(md.trackCount(), before + 1);
        QVERIFY(!recording.isEmpty());
        // Done tracks don't record again.
        QVERIFY(!md.plan().value(QStringLiteral("canRecord")).toBool());
        md.clearQueue();
        QCOMPARE(md.queue()->count(), 0);
        QVERIFY(md.resultText().isEmpty());
        md.setTestDevices({});
    }

    // --- USB sync (M9): selection → plan → run against a temp dir standing in for the stick ---
    void syncController()
    {
        const QString mount = m_dir.filePath(QStringLiteral("stick"));
        QVERIFY(QDir().mkpath(mount));
        m_devices->setTestDevices({QVariantMap{{QStringLiteral("id"), QStringLiteral("uuid:SYNC")},
                                               {QStringLiteral("name"), QStringLiteral("SYNC")},
                                               {QStringLiteral("mountPoint"), mount}}});
        Sync &s = *m_sync;
        QCOMPARE(s.deviceId(), QStringLiteral("uuid:SYNC"));
        QCOMPARE(s.state(), Sync::Idle);
        QVERIFY(!s.canPlan()); // not set up yet
        QCOMPARE(s.selection()->count(), 0);

        mmm::DeviceProfile p = *mmm::DeviceProfiles::builtIn(QStringLiteral("usb-flash"));
        p.deviceId = QStringLiteral("uuid:SYNC");
        p.name = QStringLiteral("Sync stick");
        QSignalSpy canPlan(&s, &Sync::canPlanChanged);
        QVERIFY(m_devices->manager()->saveProfile(p));
        QVERIFY(s.canPlan());
        QCOMPARE(canPlan.count(), 1);

        const qint64 kidA = m_library->runQuery([](const mmm::LibraryQuery &q) {
            for (const mmm::AlbumRow &a : q.albums())
                if (a.title == QStringLiteral("Kid A"))
                    return a.id;
            return qint64(0);
        }).result();
        QVERIFY(kidA > 0);
        QVERIFY(!s.addItem(QStringLiteral("bogus"), 1, QStringLiteral("x")));
        QVERIFY(s.addItem(QStringLiteral("album"), static_cast<int>(kidA), QStringLiteral("Kid A – Radiohead")));
        QCOMPARE(s.selection()->count(), 1);
        QTRY_COMPARE(s.tracks()->count(), 2);
        QTRY_COMPARE(s.selection()->data(s.selection()->index(0), SyncSelectionModel::TrackCountRole).toInt(), 2);
        QCOMPARE(s.selection()->data(s.selection()->index(0), SyncSelectionModel::KindRole).toString(), QStringLiteral("Album"));
        QCOMPARE(s.tracks()->data(s.tracks()->index(0), SyncTrackModel::TargetRole).toString(), QStringLiteral("FLAC"));

        // Plan: only Kid A/01.flac exists on disk; the other is reported, not copied.
        s.plan();
        QCOMPARE(s.state(), Sync::Planning);
        QVERIFY(!s.canPlan());
        QTRY_COMPARE(s.state(), Sync::Ready);
        QVariantMap summary = s.planSummary();
        QCOMPARE(summary.value(QStringLiteral("adds")).toInt(), 1);
        QCOMPARE(summary.value(QStringLiteral("warnings")).toStringList().size(), 1);
        QCOMPARE(s.planActions()->count(), 1);
        QCOMPARE(s.planActions()->data(s.planActions()->index(0), SyncPlanModel::KindRole).toString(), QStringLiteral("add"));
        QVERIFY(s.canSync());

        QSignalSpy progress(&s, &Sync::progressChanged);
        s.run();
        QCOMPARE(s.state(), Sync::Syncing);
        QTRY_COMPARE_WITH_TIMEOUT(s.state(), Sync::Done, 20000);
        QVERIFY(!progress.isEmpty());
        const QVariantMap result = s.result();
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(result.value(QStringLiteral("added")).toInt(), 1);
        QDirIterator it(mount, {QStringLiteral("*.flac")}, QDir::Files, QDirIterator::Subdirectories);
        QVERIFY(it.hasNext());
        QVERIFY(it.next().startsWith(mount + QStringLiteral("/Music/Radiohead/Kid A/")));

        // Again: nothing to do.
        s.dismiss();
        QCOMPARE(s.state(), Sync::Idle);
        s.plan();
        QTRY_COMPARE(s.state(), Sync::Ready);
        QVERIFY(!s.planSummary().value(QStringLiteral("hasWork")).toBool());
        QVERIFY(!s.canSync());

        // A change while planning: the plan that comes back is dropped, not shown.
        s.dismiss();
        s.plan();
        QCOMPARE(s.state(), Sync::Planning);
        p.name = QStringLiteral("Sync stick 2");
        QVERIFY(m_devices->manager()->saveProfile(p));
        QTRY_VERIFY(!m_sync->job()->isBusy());
        QCOMPARE(s.state(), Sync::Idle);
        QVERIFY(s.planSummary().isEmpty());
        QTRY_VERIFY(s.canPlan());
        s.plan();
        QTRY_COMPARE(s.state(), Sync::Ready);

        // Changing the selection drops the shown plan.
        s.removeAt(0);
        QCOMPARE(s.state(), Sync::Idle);
        QCOMPARE(s.selection()->count(), 0);
        QTRY_COMPARE(s.tracks()->count(), 0);

        QVERIFY(m_devices->manager()->deleteProfile(QStringLiteral("uuid:SYNC")));
        QVERIFY(!s.canPlan());
        m_devices->setTestDevices({});
        QCOMPARE(s.deviceId(), QString());
    }

    // --- Artwork finder (M7) ---

    void trackCoverUrls()
    {
        auto kidA = loaded<TrackListModel>([&](TrackListModel &m) {
            m.setAlbumId(idOf(*loaded<AlbumListModel>(), "albumId", "title", QStringLiteral("Kid A")));
        });
        const QStringList urls = column(*kidA, "coverUrl");
        QCOMPARE(urls.size(), 2);
        QVERIFY(urls[0].startsWith(QStringLiteral("image://cover/"))); // 01.flac has embedded art
        QCOMPARE(urls[1], QString());                                  // 02.flac: none, no folder art

        // An album whose art changed in place gets new URLs (QML reloads instead of caching).
        const QString before = CoverLoader::url(42, QStringLiteral("/a/cover.jpg"), {});
        CoverLoader::bumpRevision(42);
        const QString after = CoverLoader::url(42, QStringLiteral("/a/cover.jpg"), {});
        QVERIFY(before != after);
        QCOMPARE(CoverLoader::decodeId(after.mid(QStringLiteral("image://cover/").size())).first, QStringLiteral("/a/cover.jpg"));
    }

    void artFinderListsMissingAlbumsOffline()
    {
        QTRY_COMPARE_WITH_TIMEOUT(m_library->missingArtCount(), 3, kTimeoutMs); // Abbey Road, The Wall, Now 42
        ArtFinder finder;
        QAbstractItemModelTester tester(&finder, QAbstractItemModelTester::FailureReportingMode::QtTest);
        // Never the real services: a closed local port (ctest sets this too).
        finder.finder().musicBrainz().setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:9")));
        finder.finder().musicBrainz().fetcher().setOptions({1, 0, 0, 5000, 10});
        finder.finder().coverArt().setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:9")));
        finder.openMissing();
        QTRY_COMPARE_WITH_TIMEOUT(finder.count(), 3, kTimeoutMs);
        QCOMPARE(column(finder, "title"), (QStringList{QStringLiteral("Abbey Road"), QStringLiteral("The Wall"),
                                                       QStringLiteral("Now 42")}));
        QCOMPARE(finder.currentIndex(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(finder.searchedCount(), 3, kTimeoutMs);
        QCOMPARE(finder.current().value(QStringLiteral("status")).toInt(), int(ArtFinder::Failed));
        QVERIFY(finder.current().value(QStringLiteral("error")).toString().contains(QStringLiteral("MusicBrainz")));
        QVERIFY(finder.candidates().isEmpty());
        QVERIFY(!finder.accept(0));

        // Skip remembers (the library count drops); retry un-skips.
        finder.skip();
        QCOMPARE(finder.skippedCount(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(m_library->skippedArtCount(), 1, kTimeoutMs);
        QCOMPARE(m_library->missingArtCount(), 2);
        finder.retry();
        QTRY_COMPARE_WITH_TIMEOUT(m_library->skippedArtCount(), 0, kTimeoutMs);
        QTRY_COMPARE_WITH_TIMEOUT(finder.searchedCount(), 3, kTimeoutMs);

        finder.close();
        QCOMPARE(finder.count(), 0);
        QCOMPARE(finder.currentIndex(), -1);
    }

    // Accepting a cover: downloaded (file:// here), saved as cover.jpg + embedded, and the
    // library and views follow. Kid A has embedded art, so this is "replace".
    void artFinderSavesCover()
    {
        const QString kidADir = m_dir.filePath(QStringLiteral("Radiohead/Kid A"));
        const QString image = m_dir.filePath(QStringLiteral("candidate.jpg"));
        QByteArray jpeg;
        {
            QImage img(64, 64, QImage::Format_RGB32);
            img.fill(QColor(10, 120, 200));
            QBuffer buf(&jpeg);
            buf.open(QIODevice::WriteOnly);
            QVERIFY(img.save(&buf, "JPEG"));
            QFile f(image);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(jpeg);
        }
        const int kidAId = idOf(*loaded<AlbumListModel>(), "albumId", "title", QStringLiteral("Kid A"));
        ArtFinder finder;
        finder.setTestData({QVariantMap{
            {QStringLiteral("albumId"), kidAId},
            {QStringLiteral("title"), QStringLiteral("Kid A")},
            {QStringLiteral("candidates"),
             QVariantList{QVariantMap{{QStringLiteral("title"), QStringLiteral("Kid A")},
                                      {QStringLiteral("saveUrl"), QUrl::fromLocalFile(image)}}}}}});
        QCOMPARE(finder.candidates().size(), 1);

        QSignalSpy saved(&finder, &ArtFinder::saved);
        QSignalSpy edited(m_library.get(), &Library::tracksEdited);
        QSignalSpy changed(m_library.get(), &Library::changed);
        const QVariantList data = {QVariantMap{
            {QStringLiteral("albumId"), kidAId},
            {QStringLiteral("title"), QStringLiteral("Kid A")},
            {QStringLiteral("candidates"),
             QVariantList{QVariantMap{{QStringLiteral("title"), QStringLiteral("Kid A")},
                                      {QStringLiteral("saveUrl"), QUrl::fromLocalFile(image)}}}}}};
        QVERIFY(finder.accept(0));
        QCOMPARE(finder.current().value(QStringLiteral("status")).toInt(), int(ArtFinder::Saving));
        QVERIFY(!finder.accept(0)); // already saving
        // Closed and reopened while saving: still shown as saving, no second save.
        finder.close();
        finder.setTestData(data);
        QCOMPARE(finder.current().value(QStringLiteral("status")).toInt(), int(ArtFinder::Saving));
        QVERIFY(!finder.accept(0));
        QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 1, kTimeoutMs);
        QVERIFY2(saved.front().at(1).toBool(), qPrintable(saved.front().at(2).toString()));

        QFile cover(kidADir + QStringLiteral("/cover.jpg"));
        QVERIFY(cover.open(QIODevice::ReadOnly));
        QCOMPARE(cover.readAll(), jpeg);
        QCOMPARE(mmm::TagReader::readCover(kidADir + QStringLiteral("/01.flac"))->data, jpeg);
        QCOMPARE(finder.current().value(QStringLiteral("status")).toInt(), int(ArtFinder::Saved));
        // 02.flac isn't on disk: a warning, not a failure.
        QVERIFY(finder.current().value(QStringLiteral("error")).toString().contains(QStringLiteral("02.flac")));
        QVERIFY(finder.current().value(QStringLiteral("coverUrl")).toString().startsWith(QStringLiteral("image://cover/")));
        QCOMPARE(edited.count(), 1);
        QCOMPARE(changed.count(), 1);
        QVERIFY(m_library->statusMessage().startsWith(QStringLiteral("Saved the cover")));
        QVERIFY(coverUrlOf(QStringLiteral("Kid A")).contains(QStringLiteral("image://cover/%1.").arg(kidAId))); // revision bumped
        QCOMPARE(CoverLoader::decodeId(coverUrlOf(QStringLiteral("Kid A")).mid(14)).first, cover.fileName());

        // A download that fails leaves the album open with the reason.
        finder.setTestData({QVariantMap{
            {QStringLiteral("albumId"), kidAId},
            {QStringLiteral("candidates"),
             QVariantList{QVariantMap{{QStringLiteral("saveUrl"), QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("gone.jpg")))}}}}}});
        QVERIFY(finder.accept(0));
        QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 2, kTimeoutMs);
        QVERIFY(!saved.back().at(1).toBool());
        QCOMPARE(finder.current().value(QStringLiteral("status")).toInt(), int(ArtFinder::Found));
        QVERIFY(finder.current().value(QStringLiteral("error")).toString().startsWith(QStringLiteral("Couldn't download")));
    }
};

QTEST_MAIN(TestUiModels)
#include "tst_uimodels.moc"

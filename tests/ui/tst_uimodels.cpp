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

#include "artfinder.h"
#include "coverimageprovider.h"
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
        m_cacheDir = m_dir.filePath(QStringLiteral("covers"));
    }

    void cleanupTestCase() { m_library.reset(); }

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

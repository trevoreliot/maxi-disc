// SPDX-License-Identifier: GPL-3.0-or-later
#include <QAbstractItemModelTester>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "coverimageprovider.h"
#include "fixturelibrary.h"
#include "library.h"
#include "librarymodels.h"

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
};

QTEST_MAIN(TestUiModels)
#include "tst_uimodels.moc"

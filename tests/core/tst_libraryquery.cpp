// SPDX-License-Identifier: GPL-3.0-or-later
#include <QElapsedTimer>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

#include "database.h"
#include "libraryquery.h"

using mmm::Database;
using mmm::LibraryQuery;
using mmm::TrackTags;

namespace {

struct T
{
    const char *path;
    const char *title;
    const char *artist;
    const char *album;
    const char *albumArtist;
    const char *genre;
    int year;
    int disc;
    int track;
    bool art;
};

// A small library with sorting traps: "The " prefixes, numeric titles, multi-disc,
// a compilation, case differences, and a track without an album.
const T kTracks[] = {
    {"/m/beatles/ar/01.flac", "Come Together", "The Beatles", "Abbey Road", "", "Rock", 1969, 1, 1, true},
    {"/m/beatles/ar/02.flac", "Something", "The Beatles", "Abbey Road", "", "Rock", 1969, 1, 2, false},
    {"/m/beatles/rv/01.flac", "Taxman", "The Beatles", "Revolver", "", "Rock", 1966, 1, 1, false},
    {"/m/bjork/h/01.flac", "Hunter", "Björk", "Homogenic", "", "Electronic", 1997, 1, 1, false},
    {"/m/aphex/saw/01.flac", "Xtal", "Aphex Twin", "Selected Ambient Works 85-92", "", "Electronic", 1992, 1, 1, false},
    {"/m/aphex/vol2/01.flac", "#3", "Aphex Twin", "Album 10", "", "Electronic", 1994, 1, 1, false},
    {"/m/aphex/vol1/01.flac", "Ageispolis", "Aphex Twin", "Album 2", "", "Electronic", 1994, 1, 1, false},
    // Multi-disc, inserted out of order.
    {"/m/pf/wall/d2t1.flac", "Hey You", "Pink Floyd", "The Wall", "", "Rock", 1979, 2, 1, false},
    {"/m/pf/wall/d1t2.flac", "The Thin Ice", "Pink Floyd", "The Wall", "", "Rock", 1979, 1, 2, false},
    {"/m/pf/wall/d1t1.flac", "In the Flesh?", "Pink Floyd", "The Wall", "", "Rock", 1979, 1, 1, false},
    // Compilation with a shared album artist.
    {"/m/comp/01.mp3", "Song A", "Artist A", "Now 42", "Various Artists", "Pop", 1999, 1, 1, false},
    {"/m/comp/02.mp3", "Song B", "Artist B", "Now 42", "Various Artists", "Pop", 1999, 1, 2, false},
    // No album: searchable, but not in the album browser.
    {"/m/loose/demo.wav", "Demo", "Someone", "", "", "", 0, 0, 0, false},
};

TrackTags tagsFor(const T &t)
{
    TrackTags g;
    g.title = QString::fromUtf8(t.title);
    g.artist = QString::fromUtf8(t.artist);
    g.album = QString::fromUtf8(t.album);
    g.albumArtist = QString::fromUtf8(t.albumArtist);
    g.genre = QString::fromUtf8(t.genre);
    g.year = t.year;
    g.discNumber = t.disc;
    g.trackNumber = t.track;
    g.durationMs = 200000;
    g.codec = QStringLiteral("FLAC");
    g.hasEmbeddedArt = t.art;
    return g;
}

QStringList names(const QList<mmm::ArtistRow> &rows)
{
    QStringList out;
    for (const auto &r : rows)
        out << r.name;
    return out;
}

QStringList titles(const QList<mmm::AlbumRow> &rows)
{
    QStringList out;
    for (const auto &r : rows)
        out << r.title;
    return out;
}

QStringList titles(const QList<mmm::TrackRow> &rows)
{
    QStringList out;
    for (const auto &r : rows)
        out << r.title;
    return out;
}

} // namespace

class TestLibraryQuery : public QObject
{
    Q_OBJECT

private:
    Database m_db;

    qint64 artistId(const QString &name)
    {
        QSqlQuery q(m_db.connection());
        q.prepare(QStringLiteral("SELECT id FROM artists WHERE name = ?"));
        q.addBindValue(name);
        return q.exec() && q.next() ? q.value(0).toLongLong() : 0;
    }
    qint64 albumId(const QString &title)
    {
        QSqlQuery q(m_db.connection());
        q.prepare(QStringLiteral("SELECT id FROM albums WHERE title = ?"));
        q.addBindValue(title);
        return q.exec() && q.next() ? q.value(0).toLongLong() : 0;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_db.open(QStringLiteral(":memory:")));
        for (const T &t : kTracks)
            QVERIFY(m_db.upsertTrack(QString::fromUtf8(t.path), 1, 1, tagsFor(t)));
        QSqlQuery q(m_db.connection());
        QVERIFY(q.exec(QStringLiteral("UPDATE albums SET cover_path = '/m/bjork/h/cover.jpg' WHERE title = 'Homogenic'")));
    }

    void sortName()
    {
        QCOMPARE(LibraryQuery::sortName(QStringLiteral("The Beatles")), QStringLiteral("Beatles"));
        QCOMPARE(LibraryQuery::sortName(QStringLiteral("the the")), QStringLiteral("the"));
        QCOMPARE(LibraryQuery::sortName(QStringLiteral("The")), QStringLiteral("The"));
        QCOMPARE(LibraryQuery::sortName(QStringLiteral("Theatre of Tragedy")), QStringLiteral("Theatre of Tragedy"));
    }

    void counts()
    {
        const auto c = LibraryQuery(m_db).counts();
        QCOMPARE(c.tracks, 13);
        QCOMPARE(c.albums, 8);
        QCOMPARE(c.artists, 5);
    }

    void albumArtists()
    {
        const auto rows = LibraryQuery(m_db).albumArtists();
        // "The Beatles" sorts under B; Björk after Beatles; only album artists (no "Artist A", "Someone").
        QCOMPARE(names(rows), (QStringList{QStringLiteral("Aphex Twin"), QStringLiteral("The Beatles"),
                                           QStringLiteral("Björk"), QStringLiteral("Pink Floyd"),
                                           QStringLiteral("Various Artists")}));
        QCOMPARE(rows[0].albumCount, 3);
        QCOMPARE(rows[1].albumCount, 2);
    }

    void genres()
    {
        const auto rows = LibraryQuery(m_db).genres();
        QStringList n;
        for (const auto &r : rows)
            n << r.name;
        QCOMPARE(n, (QStringList{QStringLiteral("Electronic"), QStringLiteral("Pop"), QStringLiteral("Rock")}));
        QCOMPARE(rows[0].albumCount, 4); // Homogenic + 3 Aphex albums
        QCOMPARE(rows[2].albumCount, 3);
    }

    void allAlbums()
    {
        const auto rows = LibraryQuery(m_db).albums();
        // By artist sort name, then year, then title (numeric-aware: "Album 2" < "Album 10").
        QCOMPARE(titles(rows), (QStringList{
                                   QStringLiteral("Selected Ambient Works 85-92"), // Aphex 1992
                                   QStringLiteral("Album 2"),                      // Aphex 1994
                                   QStringLiteral("Album 10"),                     // Aphex 1994
                                   QStringLiteral("Revolver"),                     // Beatles 1966
                                   QStringLiteral("Abbey Road"),                   // Beatles 1969
                                   QStringLiteral("Homogenic"),
                                   QStringLiteral("The Wall"),
                                   QStringLiteral("Now 42"),
                               }));
        const auto &abbey = rows[4];
        QCOMPARE(abbey.artist, QStringLiteral("The Beatles"));
        QCOMPARE(abbey.year, 1969);
        QCOMPARE(abbey.trackCount, 2);
        QCOMPARE(abbey.durationMs, 400000);
        QCOMPARE(abbey.artTrackPath, QStringLiteral("/m/beatles/ar/01.flac"));
        QVERIFY(abbey.coverPath.isEmpty());
        QCOMPARE(rows[5].coverPath, QStringLiteral("/m/bjork/h/cover.jpg"));
        QVERIFY(rows[5].artTrackPath.isEmpty());
        QVERIFY(rows[7].compilation == false); // album artist given explicitly, not via the flag
    }

    void albumsByArtistAndGenre()
    {
        const LibraryQuery q(m_db);
        QCOMPARE(titles(q.albums(artistId(QStringLiteral("The Beatles")))),
                 (QStringList{QStringLiteral("Revolver"), QStringLiteral("Abbey Road")})); // chronological
        QCOMPARE(titles(q.albums(0, 0)).size(), 8);

        QSqlQuery g(m_db.connection());
        QVERIFY(g.exec(QStringLiteral("SELECT id FROM genres WHERE name = 'Electronic'")) && g.next());
        const qint64 electronic = g.value(0).toLongLong();
        QCOMPARE(q.albums(0, electronic).size(), 4);
        QCOMPARE(q.albums(artistId(QStringLiteral("Aphex Twin")), electronic).size(), 3);
        QCOMPARE(q.albums(artistId(QStringLiteral("The Beatles")), electronic).size(), 0);
        QVERIFY(q.albums(999999).isEmpty());
    }

    void albumTracksInDiscOrder()
    {
        const auto rows = LibraryQuery(m_db).albumTracks(albumId(QStringLiteral("The Wall")));
        QCOMPARE(titles(rows), (QStringList{QStringLiteral("In the Flesh?"), QStringLiteral("The Thin Ice"),
                                            QStringLiteral("Hey You")}));
        QCOMPARE(rows[2].discNumber, 2);
        QCOMPARE(rows[2].trackNumber, 1);
        QCOMPARE(rows[0].album, QStringLiteral("The Wall"));
        QCOMPARE(rows[0].albumArtist, QStringLiteral("Pink Floyd"));
        QCOMPARE(rows[0].path, QStringLiteral("/m/pf/wall/d1t1.flac"));
        QCOMPARE(rows[0].durationMs, 200000);

        const auto comp = LibraryQuery(m_db).albumTracks(albumId(QStringLiteral("Now 42")));
        QCOMPARE(comp[0].artist, QStringLiteral("Artist A"));
        QCOMPARE(comp[0].albumArtist, QStringLiteral("Various Artists"));
    }

    void tracksByIdsKeepsOrder()
    {
        const LibraryQuery q(m_db);
        const auto all = q.albumTracks(albumId(QStringLiteral("The Wall")));
        const QList<qint64> ids{all[2].id, 999999, all[0].id};
        const auto rows = q.tracksByIds(ids);
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[0].id, all[2].id);
        QCOMPARE(rows[1].id, all[0].id);
        QVERIFY(q.tracksByIds({}).isEmpty());
    }

    void search()
    {
        const LibraryQuery q(m_db);
        QCOMPARE(titles(q.search(QStringLiteral("beatles some"))), QStringList{QStringLiteral("Something")});
        QCOMPARE(titles(q.search(QStringLiteral("demo"))), QStringList{QStringLiteral("Demo")}); // albumless
        const auto loose = q.search(QStringLiteral("demo"));
        QVERIFY(loose[0].album.isEmpty());
        QCOMPARE(loose[0].albumId, 0);
        QVERIFY(q.search(QString()).isEmpty());
    }

    void artFieldsOnTrackRows()
    {
        const LibraryQuery q(m_db);
        const auto homogenic = q.albumTracks(albumId(QStringLiteral("Homogenic")));
        QCOMPARE(homogenic[0].coverPath, QStringLiteral("/m/bjork/h/cover.jpg"));
        QCOMPARE(homogenic[0].artSourcePath(), QStringLiteral("/m/bjork/h/cover.jpg")); // folder art first
        const auto abbey = q.albumTracks(albumId(QStringLiteral("Abbey Road")));
        QVERIFY(abbey[0].hasEmbeddedArt);
        QCOMPARE(abbey[0].artSourcePath(), abbey[0].path); // falls back to the track's own art
        QVERIFY(abbey[1].artSourcePath().isEmpty());
    }

    // --- Artwork finder (M7) ---

    void albumsMissingArt()
    {
        const LibraryQuery q(m_db);
        // Abbey Road has a track with embedded art, Homogenic has folder art.
        const QStringList expected = {QStringLiteral("Selected Ambient Works 85-92"), QStringLiteral("Album 2"),
                                      QStringLiteral("Album 10"), QStringLiteral("Revolver"),
                                      QStringLiteral("The Wall"), QStringLiteral("Now 42")};
        QList<mmm::ArtworkTarget> missing = q.albumsMissingArt();
        QStringList got;
        for (const auto &t : missing)
            got << t.title;
        QCOMPARE(got, expected); // sorted like the album grid: artist sort name, year, title
        QCOMPARE(q.missingArtCounts(), (std::pair<int, int>{6, 0}));

        const auto wall = std::find_if(missing.cbegin(), missing.cend(),
                                       [](const auto &t) { return t.title == QLatin1String("The Wall"); });
        QCOMPARE(wall->artist, QStringLiteral("Pink Floyd"));
        QCOMPARE(wall->trackCount, 3);
        QCOMPARE(wall->year, 1979);
        QCOMPARE(wall->folder, QStringLiteral("/m/pf/wall"));
        QVERIFY(!wall->hasArt);
        QVERIFY(!wall->skipped);

        // Skipping hides an album unless asked for; setting a cover clears the skip.
        const qint64 revolver = albumId(QStringLiteral("Revolver"));
        QVERIFY(m_db.setAlbumArtSkipped(revolver, true));
        QCOMPARE(q.albumsMissingArt().size(), 5);
        QCOMPARE(q.missingArtCounts(), (std::pair<int, int>{5, 1}));
        const auto withSkipped = q.albumsMissingArt(true);
        QCOMPARE(withSkipped.size(), 6);
        QVERIFY(std::any_of(withSkipped.cbegin(), withSkipped.cend(),
                            [&](const auto &t) { return t.albumId == revolver && t.skipped; }));

        QVERIFY(m_db.setAlbumCover(revolver, QStringLiteral("/m/beatles/rv/cover.jpg")));
        QCOMPARE(q.missingArtCounts(), (std::pair<int, int>{5, 0}));
        const auto target = q.artworkTarget(revolver);
        QVERIFY(target);
        QVERIFY(target->hasArt);
        QVERIFY(!target->skipped);
        QCOMPARE(target->title, QStringLiteral("Revolver"));
        QVERIFY(!q.artworkTarget(999999));
        QVERIFY(!m_db.setAlbumCover(999999, QString()));

        QVERIFY(m_db.setAlbumCover(revolver, QString())); // put it back
        QCOMPARE(q.missingArtCounts(), (std::pair<int, int>{6, 0}));
    }

    void albumFolders()
    {
        const LibraryQuery q(m_db);
        const auto wall = q.albumFolders(albumId(QStringLiteral("The Wall")));
        QCOMPARE(wall.size(), 1);
        QCOMPARE(wall[0].path, QStringLiteral("/m/pf/wall"));
        QVERIFY(!wall[0].shared);

        // A folder shared with another album, and an album spread over two folders.
        QVERIFY(m_db.upsertTrack(QStringLiteral("/m/pf/wall/bonus.flac"), 1, 1,
                                 tagsFor(T{"", "Bonus", "Pink Floyd", "Animals", "", "Rock", 1977, 1, 1, false})));
        QVERIFY(m_db.upsertTrack(QStringLiteral("/m/pf/animals-d2/01.flac"), 1, 1,
                                 tagsFor(T{"", "Dogs", "Pink Floyd", "Animals", "", "Rock", 1977, 2, 1, false})));
        // Subfolders don't count as shared: /m/pf/wall/sub/x.flac isn't directly in /m/pf/wall.
        QVERIFY(m_db.upsertTrack(QStringLiteral("/m/pf/wall/sub/x.flac"), 1, 1,
                                 tagsFor(T{"", "X", "Someone", "Other", "", "", 0, 0, 0, false})));
        const auto animals = q.albumFolders(albumId(QStringLiteral("Animals")));
        QCOMPARE(animals.size(), 2);
        QCOMPARE(animals[0].path, QStringLiteral("/m/pf/wall")); // disc 1 first
        QVERIFY(animals[0].shared);
        QCOMPARE(animals[1].path, QStringLiteral("/m/pf/animals-d2"));
        QVERIFY(!animals[1].shared);
        const auto other = q.albumFolders(albumId(QStringLiteral("Other")));
        QCOMPARE(other.size(), 1);
        QVERIFY(!other[0].shared);

        for (const char *p : {"/m/pf/wall/bonus.flac", "/m/pf/animals-d2/01.flac", "/m/pf/wall/sub/x.flac"})
            QVERIFY(m_db.removeTrack(QString::fromLatin1(p)));
        m_db.pruneOrphans();
    }

    void playlists()
    {
        const QString missing = QStringLiteral("/m/not-in-library.flac");
        const auto road = m_db.createPlaylist(QStringLiteral("Road trip"),
                                              {QStringLiteral("/m/pf/wall/d2t1.flac"), missing,
                                               QStringLiteral("/m/beatles/ar/01.flac"), QStringLiteral("/m/pf/wall/d2t1.flac")});
        const auto empty = m_db.createPlaylist(QStringLiteral("Already empty"));
        QVERIFY(road && empty);

        const LibraryQuery q(m_db);
        const auto lists = q.playlists();
        QCOMPARE(lists.size(), 2);
        QCOMPARE(lists[0].name, QStringLiteral("Already empty")); // by name
        QCOMPARE(lists[1].itemCount, 4);
        QCOMPARE(lists[1].availableCount, 3); // the missing file doesn't count

        QCOMPARE(titles(q.playlistTracks(*road)), (QStringList{QStringLiteral("Hey You"), QStringLiteral("Come Together"),
                                                             QStringLiteral("Hey You")}));
        QVERIFY(q.playlistTracks(*empty).isEmpty());

        QCOMPARE(titles(q.tracksByPaths({QStringLiteral("/m/beatles/rv/01.flac"), missing, QStringLiteral("/m/bjork/h/01.flac")})),
                 (QStringList{QStringLiteral("Taxman"), QStringLiteral("Hunter")}));

        QVERIFY(m_db.deletePlaylist(*road));
        QVERIFY(m_db.deletePlaylist(*empty));
    }

    void performanceAt100kTracks()
    {
        // PLAN: browse/search < 100 ms on a 100k-track library. Generate synthetic rows in SQL
        // with recursive CTEs: one statement per table, fast even under ASan.
        QTemporaryDir dir;
        Database db;
        QVERIFY(db.open(dir.filePath(QStringLiteral("big.db"))));
        constexpr int kArtistCount = 3000, kAlbumCount = 10000, kTrackCount = 100000, kGenreCount = 40;
        QElapsedTimer t;
        t.start();
        {
            QSqlDatabase c = db.connection();
            QVERIFY(c.transaction());
            const QString seq = QStringLiteral("WITH RECURSIVE seq(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM seq WHERE i < %1) ");
            const QStringList statements = {
                seq.arg(kArtistCount) + QStringLiteral("INSERT INTO artists (id, name) SELECT i, 'Artist ' || i FROM seq"),
                seq.arg(kGenreCount) + QStringLiteral("INSERT INTO genres (id, name) SELECT i, 'Genre ' || i FROM seq"),
                seq.arg(kAlbumCount)
                    + QStringLiteral("INSERT INTO albums (id, title, artist_id, year) "
                                     "SELECT i, 'Album ' || i, 1 + i % %1, 1960 + i % 60 FROM seq").arg(kArtistCount),
                seq.arg(kTrackCount)
                    + QStringLiteral("INSERT INTO tracks (id, path, file_mtime, file_size, title, artist_id, album_id, "
                                     "genre_id, track_no, disc_no, duration_ms, codec, has_embedded_art) "
                                     "SELECT i, '/lib/' || (1 + i % %1) || '/' || i || '.flac', 1, 1, "
                                     "'Track ' || i || ' word' || (i % 997), 1 + (1 + i % %1) % %2, 1 + i % %1, "
                                     "1 + i % %3, 1 + i / %1, 1, 200000, 'FLAC', i % 3 = 0 FROM seq")
                          .arg(kAlbumCount).arg(kArtistCount).arg(kGenreCount),
                QStringLiteral("INSERT INTO tracks_fts (rowid, title, artist, album, album_artist, genre, composer) "
                               "SELECT t.id, t.title, ar.name, al.title, ar.name, g.name, '' FROM tracks t "
                               "JOIN artists ar ON ar.id = t.artist_id JOIN albums al ON al.id = t.album_id "
                               "JOIN genres g ON g.id = t.genre_id"),
            };
            for (const QString &sql : statements) {
                QSqlQuery q(c);
                QVERIFY2(q.exec(sql), qPrintable(sql.left(120)));
            }
            QVERIFY(c.commit());
        }
        const qint64 loadMs = t.elapsed();

        const LibraryQuery q(db);
        auto timeIt = [](auto fn) {
            QElapsedTimer e;
            e.start();
            fn();
            return e.elapsed();
        };
        qsizetype n = 0;
        const qint64 artistsMs = timeIt([&] { n = q.albumArtists().size(); });
        QCOMPARE(n, kArtistCount);
        const qint64 albumsMs = timeIt([&] { n = q.albums().size(); });
        QCOMPARE(n, kAlbumCount);
        const qint64 artistAlbumsMs = timeIt([&] { n = q.albums(42).size(); });
        QVERIFY(n > 0);
        const qint64 genreAlbumsMs = timeIt([&] { n = q.albums(0, 7).size(); });
        QVERIFY(n > 0);
        const qint64 tracksMs = timeIt([&] { n = q.albumTracks(123).size(); });
        QCOMPARE(n, kTrackCount / kAlbumCount);
        const qint64 searchMs = timeIt([&] { n = q.search(QStringLiteral("word42"), 500).size(); });
        QVERIFY(n > 0);
        const qint64 prefixMs = timeIt([&] { n = q.search(QStringLiteral("ar"), 500).size(); });
        QCOMPARE(n, 500);

        {
            // Half the albums lose their art: the finder's list is then big.
            QSqlQuery strip(db.connection());
            QVERIFY(strip.exec(QStringLiteral("UPDATE tracks SET has_embedded_art = 0 WHERE album_id % 2 = 0")));
        }
        const qint64 missingArtMs = timeIt([&] { n = q.albumsMissingArt().size(); });
        QCOMPARE(n, kAlbumCount / 2);
        const qint64 missingCountMs = timeIt([&] { n = q.missingArtCounts().first; });
        QCOMPARE(n, kAlbumCount / 2);

        qInfo("100k library (load %lld ms): artists %lld ms, all albums %lld ms, artist's albums %lld ms, "
              "genre's albums %lld ms, album tracks %lld ms, search %lld ms, 2-letter prefix search %lld ms, "
              "albums missing art %lld ms (count %lld ms)",
              loadMs, artistsMs, albumsMs, artistAlbumsMs, genreAlbumsMs, tracksMs, searchMs, prefixMs,
              missingArtMs, missingCountMs);
        // Loose bounds: this also runs under ASan in debug. Release timings are what the plan targets.
        QVERIFY(albumsMs < 2000);
        QVERIFY(searchMs < 500);
        QVERIFY(tracksMs < 100);
        QVERIFY(missingArtMs < 2000);
        QVERIFY(missingCountMs < 500);
    }
};

QTEST_GUILESS_MAIN(TestLibraryQuery)
#include "tst_libraryquery.moc"

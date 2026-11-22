// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include "database.h"

using mmm::Database;
using mmm::TrackTags;

namespace {

TrackTags makeTags(const QString &title, const QString &artist, const QString &album,
                   const QString &albumArtist = {}, const QString &genre = {})
{
    TrackTags t;
    t.title = title;
    t.artist = artist;
    t.album = album;
    t.albumArtist = albumArtist;
    t.genre = genre;
    t.codec = QStringLiteral("FLAC");
    t.durationMs = 180000;
    return t;
}

QVariant scalar(const Database &db, const QString &sql, const QVariantList &binds = {})
{
    QSqlQuery q(db.connection());
    q.prepare(sql);
    for (const auto &b : binds)
        q.addBindValue(b);
    if (!q.exec() || !q.next())
        return {};
    return q.value(0);
}

} // namespace

class TestDatabase : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString dbPath(const char *name) const { return m_dir.filePath(QString::fromLatin1(name)); }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    // --- Schema & migrations ---

    void freshDatabaseMigratesToLatest()
    {
        Database db;
        QString error;
        QVERIFY2(db.open(dbPath("fresh.db"), &error), qPrintable(error));
        QCOMPARE(db.schemaVersion(), Database::latestSchemaVersion());
        QVERIFY(Database::latestSchemaVersion() >= 1);

        for (const char *table : {"library_folders", "artists", "albums", "genres", "tracks", "tracks_fts"}) {
            QVERIFY2(scalar(db, QStringLiteral("SELECT count(*) FROM sqlite_master WHERE name = ?"),
                            {QString::fromLatin1(table)}).toInt() == 1,
                     table);
        }
        QCOMPARE(scalar(db, QStringLiteral("PRAGMA journal_mode")).toString(), QStringLiteral("wal"));
        QCOMPARE(scalar(db, QStringLiteral("PRAGMA foreign_keys")).toInt(), 1);
    }

    void fts5IsAvailable()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        QCOMPARE(scalar(db, QStringLiteral("SELECT sqlite_compileoption_used('ENABLE_FTS5')")).toInt(), 1);
    }

    void reopenIsIdempotent()
    {
        const QString path = dbPath("reopen.db");
        {
            Database db;
            QVERIFY(db.open(path));
            QVERIFY(db.upsertTrack(QStringLiteral("/m/a.flac"), 1, 2, makeTags(QStringLiteral("A"), {}, {})));
        }
        Database db;
        QString error;
        QVERIFY2(db.open(path, &error), qPrintable(error));
        QCOMPARE(db.schemaVersion(), Database::latestSchemaVersion());
        QVERIFY(db.trackIdByPath(QStringLiteral("/m/a.flac")));
    }

    void newerSchemaIsRefused()
    {
        const QString path = dbPath("future.db");
        {
            Database db;
            QVERIFY(db.open(path));
            QSqlQuery q(db.connection());
            QVERIFY(q.exec(QStringLiteral("PRAGMA user_version = %1").arg(Database::latestSchemaVersion() + 1)));
        }
        Database db;
        QString error;
        QVERIFY(!db.open(path, &error));
        QVERIFY2(error.contains(QLatin1String("newer")), qPrintable(error));
        QVERIFY(!db.isOpen());
    }

    void unopenableFileFails()
    {
        Database db;
        QString error;
        QVERIFY(!db.open(m_dir.filePath(QStringLiteral("no/such/dir/x.db")), &error));
        QVERIFY(!error.isEmpty());
    }

    void manyInstancesCoexist()
    {
        // Each instance owns a distinct Qt SQL connection name.
        Database a, b;
        QVERIFY(a.open(dbPath("shared.db")));
        QVERIFY(b.open(dbPath("shared.db")));
        QVERIFY(a.upsertTrack(QStringLiteral("/m/shared.flac"), 1, 1, makeTags(QStringLiteral("S"), {}, {})));
        QVERIFY(b.trackIdByPath(QStringLiteral("/m/shared.flac"))); // WAL: visible to the other connection
    }

    // --- Tracks ---

    void upsertStoresEverything()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        TrackTags t = makeTags(QStringLiteral("Paranoid Android"), QStringLiteral("Radiohead"),
                               QStringLiteral("OK Computer"), {}, QStringLiteral("Rock"));
        t.year = 1997;
        t.trackNumber = 2;
        t.trackTotal = 12;
        t.discNumber = 1;
        t.sampleRate = 44100;
        t.bitsPerSample = 16;
        t.channels = 2;
        t.bitrateKbps = 900;
        t.replayGainTrackGain = -9.5;
        t.musicBrainzAlbumId = QStringLiteral("mbid-album");
        t.hasEmbeddedArt = true;

        QString error;
        const auto id = db.upsertTrack(QStringLiteral("/m/ok/02.flac"), 1700000000, 12345, t, &error);
        QVERIFY2(id, qPrintable(error));

        const QString sql = QStringLiteral(R"(
            SELECT t.title, ar.name, al.title, aa.name, g.name, t.year, t.track_no, t.track_total,
                   t.disc_no, t.disc_total, t.rg_track_gain, t.rg_album_gain, al.mb_album_id,
                   t.has_embedded_art, t.file_mtime, t.file_size, t.codec, al.year
            FROM tracks t
            JOIN artists ar ON ar.id = t.artist_id
            JOIN albums al ON al.id = t.album_id
            JOIN artists aa ON aa.id = al.artist_id
            JOIN genres g ON g.id = t.genre_id
            WHERE t.id = )") + QString::number(*id);
        QSqlQuery q(db.connection());
        QVERIFY(q.exec(sql));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(), QStringLiteral("Paranoid Android"));
        QCOMPARE(q.value(1).toString(), QStringLiteral("Radiohead"));
        QCOMPARE(q.value(2).toString(), QStringLiteral("OK Computer"));
        QCOMPARE(q.value(3).toString(), QStringLiteral("Radiohead")); // album artist falls back to artist
        QCOMPARE(q.value(4).toString(), QStringLiteral("Rock"));
        QCOMPARE(q.value(5).toInt(), 1997);
        QCOMPARE(q.value(6).toInt(), 2);
        QCOMPARE(q.value(7).toInt(), 12);
        QCOMPARE(q.value(8).toInt(), 1);
        QVERIFY(q.value(9).isNull());  // 0 → NULL
        QCOMPARE(q.value(10).toDouble(), -9.5);
        QVERIFY(q.value(11).isNull()); // nullopt → NULL
        QCOMPARE(q.value(12).toString(), QStringLiteral("mbid-album"));
        QCOMPARE(q.value(13).toInt(), 1);
        QCOMPARE(q.value(14).toLongLong(), 1700000000);
        QCOMPARE(q.value(15).toLongLong(), 12345);
        QCOMPARE(q.value(16).toString(), QStringLiteral("FLAC"));
        QCOMPARE(q.value(17).toInt(), 1997);
    }

    void upsertUpdatesInPlace()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        const QString path = QStringLiteral("/m/song.mp3");
        const auto id1 = db.upsertTrack(path, 1, 100, makeTags(QStringLiteral("Old Title"), QStringLiteral("X"), {}));
        const auto id2 = db.upsertTrack(path, 2, 200, makeTags(QStringLiteral("New Title"), QStringLiteral("X"), {}));
        QVERIFY(id1 && id2);
        QCOMPARE(*id1, *id2);
        QCOMPARE(scalar(db, QStringLiteral("SELECT count(*) FROM tracks")).toInt(), 1);
        QCOMPARE(scalar(db, QStringLiteral("SELECT title FROM tracks")).toString(), QStringLiteral("New Title"));
        QCOMPARE(scalar(db, QStringLiteral("SELECT file_mtime FROM tracks")).toInt(), 2);
        QCOMPARE(scalar(db, QStringLiteral("SELECT count(*) FROM tracks_fts")).toInt(), 1);
        QVERIFY(db.search(QStringLiteral("old")).isEmpty());
        QCOMPARE(db.search(QStringLiteral("new")), QList<qint64>{*id1});
    }

    void albumGrouping()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        // Same album artist + title (different case) → one album; artist rows are case-insensitive too.
        QVERIFY(db.upsertTrack(QStringLiteral("/m/1.flac"), 1, 1,
                               makeTags(QStringLiteral("One"), QStringLiteral("Pink Floyd"), QStringLiteral("Animals"))));
        QVERIFY(db.upsertTrack(QStringLiteral("/m/2.flac"), 1, 1,
                               makeTags(QStringLiteral("Two"), QStringLiteral("pink floyd"), QStringLiteral("ANIMALS"))));
        QCOMPARE(scalar(db, QStringLiteral("SELECT count(*) FROM albums")).toInt(), 1);
        QCOMPARE(scalar(db, QStringLiteral("SELECT count(*) FROM artists")).toInt(), 1);

        // Same title, different album artist → separate albums.
        QVERIFY(db.upsertTrack(QStringLiteral("/m/3.flac"), 1, 1,
                               makeTags(QStringLiteral("Three"), QStringLiteral("Someone"), QStringLiteral("Animals"))));
        QCOMPARE(scalar(db, QStringLiteral("SELECT count(*) FROM albums")).toInt(), 2);

        // Track artists differ but album artist is shared → one album.
        QVERIFY(db.upsertTrack(QStringLiteral("/m/c1.flac"), 1, 1,
                               makeTags(QStringLiteral("C1"), QStringLiteral("A"), QStringLiteral("Mix"), QStringLiteral("DJ"))));
        QVERIFY(db.upsertTrack(QStringLiteral("/m/c2.flac"), 1, 1,
                               makeTags(QStringLiteral("C2"), QStringLiteral("B"), QStringLiteral("Mix"), QStringLiteral("DJ"))));
        QCOMPARE(scalar(db, QStringLiteral("SELECT count(DISTINCT album_id) FROM tracks WHERE path LIKE '/m/c%'")).toInt(), 1);
    }

    void compilationWithoutAlbumArtistIsVariousArtists()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        for (const char *artist : {"Artist A", "Artist B"}) {
            TrackTags t = makeTags(QStringLiteral("Song"), QString::fromLatin1(artist), QStringLiteral("Now 42"));
            t.compilation = true;
            QVERIFY(db.upsertTrack(QStringLiteral("/m/now/%1.mp3").arg(QLatin1String(artist)), 1, 1, t));
        }
        QCOMPARE(scalar(db, QStringLiteral("SELECT count(*) FROM albums")).toInt(), 1);
        QCOMPARE(scalar(db, QStringLiteral("SELECT a.name FROM albums al JOIN artists a ON a.id = al.artist_id"))
                     .toString(),
                 QStringLiteral("Various Artists"));
        QCOMPARE(scalar(db, QStringLiteral("SELECT compilation FROM albums")).toInt(), 1);
    }

    void missingTagsBecomeNulls()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        TrackTags empty;
        empty.codec = QStringLiteral("WAV");
        const auto id = db.upsertTrack(QStringLiteral("/m/rips/Track 07.wav"), 1, 1, empty);
        QVERIFY(id);
        QCOMPARE(scalar(db, QStringLiteral("SELECT title FROM tracks")).toString(), QStringLiteral("Track 07"));
        QVERIFY(scalar(db, QStringLiteral("SELECT artist_id FROM tracks")).isNull());
        QVERIFY(scalar(db, QStringLiteral("SELECT album_id FROM tracks")).isNull());
        QVERIFY(scalar(db, QStringLiteral("SELECT year FROM tracks")).isNull());
        QCOMPARE(scalar(db, QStringLiteral("SELECT count(*) FROM artists")).toInt(), 0);
        QCOMPARE(db.search(QStringLiteral("track 07")), QList<qint64>{*id}); // filename title is searchable
    }

    void removeTrack()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        const QString path = QStringLiteral("/m/gone.flac");
        QVERIFY(db.upsertTrack(path, 1, 1, makeTags(QStringLiteral("Vanishing"), QStringLiteral("X"), {})));
        QVERIFY(db.removeTrack(path));
        QVERIFY(!db.trackIdByPath(path));
        QVERIFY(db.search(QStringLiteral("vanishing")).isEmpty());
        QCOMPARE(scalar(db, QStringLiteral("SELECT count(*) FROM tracks_fts")).toInt(), 0);
        QVERIFY(!db.removeTrack(path)); // already gone
    }

    void outerTransactionBatchesAndRollsBack()
    {
        Database db;
        QVERIFY(db.open(dbPath("batch.db")));
        QSqlDatabase c = db.connection();

        QVERIFY(c.transaction());
        for (int i = 0; i < 200; ++i)
            QVERIFY(db.upsertTrack(QStringLiteral("/m/batch/%1.flac").arg(i), 1, 1,
                                   makeTags(QStringLiteral("Batch %1").arg(i), QStringLiteral("B"), QStringLiteral("Bulk"))));
        QVERIFY(c.commit());
        QCOMPARE(scalar(db, QStringLiteral("SELECT count(*) FROM tracks")).toInt(), 200);

        QVERIFY(c.transaction());
        QVERIFY(db.upsertTrack(QStringLiteral("/m/batch/rolled-back.flac"), 1, 1,
                               makeTags(QStringLiteral("Nope"), {}, {})));
        QVERIFY(c.rollback());
        QVERIFY(!db.trackIdByPath(QStringLiteral("/m/batch/rolled-back.flac")));
        QVERIFY(db.search(QStringLiteral("nope")).isEmpty());
    }

    // --- Search ---

    void search()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        const auto abbey = db.upsertTrack(QStringLiteral("/m/1.flac"), 1, 1,
                                          makeTags(QStringLiteral("Come Together"), QStringLiteral("The Beatles"),
                                                   QStringLiteral("Abbey Road"), {}, QStringLiteral("Rock")));
        const auto bjork = db.upsertTrack(QStringLiteral("/m/2.flac"), 1, 1,
                                          makeTags(QStringLiteral("Jóga"), QStringLiteral("Björk"),
                                                   QStringLiteral("Homogenic"), {}, QStringLiteral("Electronic")));
        const auto road = db.upsertTrack(QStringLiteral("/m/3.flac"), 1, 1,
                                         makeTags(QStringLiteral("Road to Nowhere"), QStringLiteral("Talking Heads"),
                                                  QStringLiteral("Little Creatures")));
        QVERIFY(abbey && bjork && road);

        QCOMPARE(db.search(QStringLiteral("beat")), QList<qint64>{*abbey});           // prefix
        QCOMPARE(db.search(QStringLiteral("beat abb")), QList<qint64>{*abbey});       // words across columns
        QCOMPARE(db.search(QStringLiteral("bjork")), QList<qint64>{*bjork});          // diacritics folded
        QCOMPARE(db.search(QStringLiteral("JÓGA")), QList<qint64>{*bjork});           // case-insensitive
        QCOMPARE(db.search(QStringLiteral("electronic")), QList<qint64>{*bjork});     // genre indexed
        QCOMPARE(db.search(QStringLiteral("road")).size(), 2);                        // title + album hits
        QVERIFY(db.search(QStringLiteral("zeppelin")).isEmpty());
        QVERIFY(db.search(QStringLiteral("   ")).isEmpty());
        QCOMPARE(db.search(QStringLiteral("road"), 1).size(), 1);                     // limit
    }

    void searchInputIsSanitised_data()
    {
        QTest::addColumn<QString>("input");
        QTest::newRow("quote") << "rock \"n roll";
        QTest::newRow("operators") << "AND OR NOT NEAR";
        QTest::newRow("column filter") << "title:foo";
        QTest::newRow("parens") << "(a OR b";
        QTest::newRow("star") << "*";
        QTest::newRow("caret") << "^start";
        QTest::newRow("minus") << "-foo";
        QTest::newRow("sql") << "'; DROP TABLE tracks; --";
    }
    void searchInputIsSanitised()
    {
        QFETCH(QString, input);
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        QVERIFY(db.upsertTrack(QStringLiteral("/m/x.flac"), 1, 1, makeTags(QStringLiteral("X"), {}, {})));
        // A malformed MATCH expression would make SQLite error and Database log "search failed".
        QTest::failOnWarning(QRegularExpression(QStringLiteral("search failed")));
        db.search(input);
        QCOMPARE(scalar(db, QStringLiteral("SELECT count(*) FROM tracks")).toInt(), 1);
    }

    // --- Migrations & playlists (M5) ---

    void migratesV1DatabaseToV2()
    {
        const QString path = dbPath("v1.db");
        {
            Database db;
            QVERIFY(db.open(path));
            QVERIFY(db.upsertTrack(QStringLiteral("/m/keep.flac"), 1, 1, makeTags(QStringLiteral("Keep me"), {}, {})));
            // Turn it back into a v1 database: as written by M2–M4.
            QSqlQuery q(db.connection());
            QVERIFY(q.exec(QStringLiteral("DROP TABLE playlist_items")));
            QVERIFY(q.exec(QStringLiteral("DROP TABLE playlists")));
            QVERIFY(q.exec(QStringLiteral("ALTER TABLE albums DROP COLUMN art_skipped_at")));
            QVERIFY(q.exec(QStringLiteral("DROP TABLE device_profiles")));
            QVERIFY(q.exec(QStringLiteral("PRAGMA user_version = 1")));
        }
        Database db;
        QString error;
        QVERIFY2(db.open(path, &error), qPrintable(error));
        QCOMPARE(db.schemaVersion(), Database::latestSchemaVersion());
        QVERIFY(db.trackIdByPath(QStringLiteral("/m/keep.flac"))); // data kept
        QVERIFY(db.createPlaylist(QStringLiteral("Works")));       // new tables usable
    }

    void migratesV2DatabaseToV3()
    {
        const QString path = dbPath("v2.db");
        {
            Database db;
            QVERIFY(db.open(path));
            QVERIFY(db.upsertTrack(QStringLiteral("/m/a/1.flac"), 1, 1,
                                   makeTags(QStringLiteral("One"), QStringLiteral("A"), QStringLiteral("Album"))));
            // As written by M5–M6: no art_skipped_at column.
            QSqlQuery q(db.connection());
            QVERIFY(q.exec(QStringLiteral("ALTER TABLE albums DROP COLUMN art_skipped_at")));
            QVERIFY(q.exec(QStringLiteral("DROP TABLE device_profiles")));
            QVERIFY(q.exec(QStringLiteral("PRAGMA user_version = 2")));
        }
        Database db;
        QString error;
        QVERIFY2(db.open(path, &error), qPrintable(error));
        QCOMPARE(db.schemaVersion(), Database::latestSchemaVersion());
        QCOMPARE(Database::latestSchemaVersion(), 4);
        const qint64 album = scalar(db, QStringLiteral("SELECT id FROM albums")).toLongLong();
        QVERIFY(scalar(db, QStringLiteral("SELECT art_skipped_at FROM albums")).isNull());
        QVERIFY(db.setAlbumArtSkipped(album, true));
        QVERIFY(scalar(db, QStringLiteral("SELECT art_skipped_at FROM albums")).toLongLong() > 0);
        QVERIFY(db.setAlbumCover(album, QStringLiteral("/m/a/cover.jpg")));
        QVERIFY(scalar(db, QStringLiteral("SELECT art_skipped_at FROM albums")).isNull()); // a cover clears it
        QCOMPARE(scalar(db, QStringLiteral("SELECT cover_path FROM albums")).toString(), QStringLiteral("/m/a/cover.jpg"));
        QVERIFY(db.setAlbumCover(album, QString()));
        QVERIFY(scalar(db, QStringLiteral("SELECT cover_path FROM albums")).isNull());
        QVERIFY(!db.setAlbumArtSkipped(album + 100, true)); // no such album
    }

    void playlists()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        const QStringList paths{QStringLiteral("/m/a.flac"), QStringLiteral("/m/b.flac"), QStringLiteral("/m/a.flac")};
        QString error;
        const auto id = db.createPlaylist(QStringLiteral("  Road trip  "), paths, &error);
        QVERIFY2(id, qPrintable(error));
        QCOMPARE(db.playlistPaths(*id), paths); // order and duplicates kept
        QCOMPARE(scalar(db, QStringLiteral("SELECT name FROM playlists")).toString(), QStringLiteral("Road trip"));

        QVERIFY(db.appendToPlaylist(*id, {QStringLiteral("/m/c.flac")}));
        QCOMPARE(db.playlistPaths(*id).size(), 4);
        QVERIFY(db.setPlaylistPaths(*id, {QStringLiteral("/m/z.flac")}));
        QCOMPARE(db.playlistPaths(*id), QStringList{QStringLiteral("/m/z.flac")});

        QVERIFY(db.renamePlaylist(*id, QStringLiteral("Renamed")));
        QVERIFY(!db.renamePlaylist(*id, QStringLiteral("   "))); // empty names refused
        QVERIFY(!db.renamePlaylist(999, QStringLiteral("x")));

        const auto unnamed = db.createPlaylist(QString());
        QVERIFY(unnamed);
        QCOMPARE(scalar(db, QStringLiteral("SELECT name FROM playlists WHERE id = %1").arg(*unnamed)).toString(),
                 QStringLiteral("Untitled playlist"));

        QVERIFY(db.deletePlaylist(*id));
        QVERIFY(db.playlistPaths(*id).isEmpty()); // items cascade
        QCOMPARE(scalar(db, QStringLiteral("SELECT count(*) FROM playlist_items")).toInt(), 0);
        QVERIFY(!db.deletePlaylist(*id));
    }

    void playlistSurvivesTrackReadd()
    {
        // Paths, not ids: a rescan that removes and re-adds a file keeps it in playlists.
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        const QString path = QStringLiteral("/m/song.flac");
        QVERIFY(db.upsertTrack(path, 1, 1, makeTags(QStringLiteral("Song"), {}, {})));
        const auto pl = db.createPlaylist(QStringLiteral("P"), {path});
        QVERIFY(db.removeTrack(path));
        QVERIFY(db.upsertTrack(path, 2, 1, makeTags(QStringLiteral("Song"), {}, {})));
        QCOMPARE(db.playlistPaths(*pl), QStringList{path});
    }

    void toFtsQuery()
    {
        QCOMPARE(Database::toFtsQuery(QStringLiteral("beat abb")), QStringLiteral("\"beat\"* \"abb\"*"));
        QCOMPARE(Database::toFtsQuery(QStringLiteral("a\"b")), QStringLiteral("\"a\"\"b\"*"));
        QCOMPARE(Database::toFtsQuery(QStringLiteral("  ")), QString());
    }
};

QTEST_GUILESS_MAIN(TestDatabase)
#include "tst_database.moc"

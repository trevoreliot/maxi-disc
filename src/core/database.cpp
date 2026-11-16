// SPDX-License-Identifier: GPL-3.0-or-later
#include "database.h"

#include <QFileInfo>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <atomic>

Q_LOGGING_CATEGORY(lcDb, "mmm.db")

namespace mmm {

namespace {

// ---------------------------------------------------------------------------
// Schema migrations. Append new steps; never edit a released one.
// Step N (1-based) upgrades user_version N-1 → N. Each runs in one transaction.
// ---------------------------------------------------------------------------
constexpr const char *kNow = "(CAST(strftime('%s','now') AS INTEGER))";

QStringList migrationV1()
{
    const QString now = QString::fromLatin1(kNow);
    return {
        QStringLiteral(R"(CREATE TABLE library_folders (
            id        INTEGER PRIMARY KEY,
            path      TEXT NOT NULL UNIQUE,
            added_at  INTEGER NOT NULL DEFAULT %1))").arg(now),

        QStringLiteral(R"(CREATE TABLE artists (
            id            INTEGER PRIMARY KEY,
            name          TEXT NOT NULL UNIQUE COLLATE NOCASE,
            mb_artist_id  TEXT))"),

        QStringLiteral(R"(CREATE TABLE genres (
            id    INTEGER PRIMARY KEY,
            name  TEXT NOT NULL UNIQUE COLLATE NOCASE))"),

        QStringLiteral(R"(CREATE TABLE albums (
            id           INTEGER PRIMARY KEY,
            title        TEXT NOT NULL COLLATE NOCASE,
            artist_id    INTEGER NOT NULL REFERENCES artists(id),
            year         INTEGER,
            compilation  INTEGER NOT NULL DEFAULT 0,
            mb_album_id  TEXT,
            cover_path   TEXT,
            UNIQUE (artist_id, title)))"),

        QStringLiteral(R"(CREATE TABLE tracks (
            id                INTEGER PRIMARY KEY,
            path              TEXT NOT NULL UNIQUE,
            folder_id         INTEGER REFERENCES library_folders(id) ON DELETE CASCADE,
            file_mtime        INTEGER NOT NULL,
            file_size         INTEGER NOT NULL,
            title             TEXT NOT NULL,
            artist_id         INTEGER REFERENCES artists(id),
            album_id          INTEGER REFERENCES albums(id),
            genre_id          INTEGER REFERENCES genres(id),
            composer          TEXT,
            comment           TEXT,
            year              INTEGER,
            track_no          INTEGER,
            track_total       INTEGER,
            disc_no           INTEGER,
            disc_total        INTEGER,
            duration_ms       INTEGER NOT NULL DEFAULT 0,
            bitrate_kbps      INTEGER,
            sample_rate       INTEGER,
            channels          INTEGER,
            bits_per_sample   INTEGER,
            codec             TEXT NOT NULL,
            rg_track_gain     REAL,
            rg_track_peak     REAL,
            rg_album_gain     REAL,
            rg_album_peak     REAL,
            mb_track_id       TEXT,
            has_embedded_art  INTEGER NOT NULL DEFAULT 0,
            added_at          INTEGER NOT NULL DEFAULT %1,
            updated_at        INTEGER NOT NULL DEFAULT %1))").arg(now),

        QStringLiteral("CREATE INDEX tracks_album  ON tracks(album_id, disc_no, track_no)"),
        QStringLiteral("CREATE INDEX tracks_artist ON tracks(artist_id)"),
        QStringLiteral("CREATE INDEX tracks_genre  ON tracks(genre_id)"),
        QStringLiteral("CREATE INDEX tracks_folder ON tracks(folder_id)"),
        QStringLiteral("CREATE INDEX albums_artist ON albums(artist_id)"),

        // rowid = tracks.id. Maintained by upsertTrack/removeTrack.
        QStringLiteral(R"(CREATE VIRTUAL TABLE tracks_fts USING fts5(
            title, artist, album, album_artist, genre, composer,
            tokenize = 'unicode61 remove_diacritics 2',
            prefix = '2 3'))"),
    };
}

using Migration = QStringList (*)();
constexpr Migration kMigrations[] = {
    &migrationV1,
};

QVariant nullIfEmpty(const QString &s) { return s.isEmpty() ? QVariant() : QVariant(s); }
QVariant nullIfZero(qint64 v) { return v == 0 ? QVariant() : QVariant(v); }
QVariant nullIfNone(const std::optional<double> &v) { return v ? QVariant(*v) : QVariant(); }

QString describe(const QSqlQuery &q)
{
    return q.lastError().text() + QStringLiteral(" [") + q.lastQuery().simplified() + QLatin1Char(']');
}

// Runs a prepared query that returns a single id (e.g. "... RETURNING id").
std::optional<qint64> execReturningId(QSqlQuery &q, QString *error)
{
    if (!q.exec() || !q.next()) {
        if (error)
            *error = describe(q);
        return std::nullopt;
    }
    const qint64 id = q.value(0).toLongLong();
    // Leave no statement active: SQLite refuses to RELEASE a savepoint while one is.
    q.finish();
    return id;
}

std::optional<qint64> upsertNamed(QSqlDatabase &db, const QString &table, const QString &name,
                                  QString *error)
{
    // DO UPDATE (no-op) instead of DO NOTHING so RETURNING yields the existing row.
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO %1(name) VALUES (?) "
                             "ON CONFLICT(name) DO UPDATE SET name = %1.name RETURNING id")
                  .arg(table));
    q.addBindValue(name);
    return execReturningId(q, error);
}

std::atomic<int> s_connectionCounter{0};

} // namespace

Database::Database()
    : m_connectionName(QStringLiteral("mmm-db-%1").arg(s_connectionCounter.fetch_add(1)))
{
}

Database::~Database()
{
    close();
}

int Database::latestSchemaVersion()
{
    return static_cast<int>(std::size(kMigrations));
}

bool Database::isOpen() const
{
    return m_db.isOpen();
}

void Database::close()
{
    if (!m_db.isValid())
        return;
    m_db.close();
    m_db = QSqlDatabase(); // drop our handle before removing the connection
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool Database::exec(const QString &sql, QString *error)
{
    QSqlQuery q(m_db);
    if (q.exec(sql))
        return true;
    if (error)
        *error = describe(q);
    return false;
}

bool Database::open(const QString &path, QString *error)
{
    close();
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(path);
    m_db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    if (!m_db.open()) {
        if (error)
            *error = m_db.lastError().text();
        close();
        return false;
    }

    const bool inMemory = path == QLatin1String(":memory:");
    const bool ok = (inMemory || exec(QStringLiteral("PRAGMA journal_mode = WAL"), error))
        && exec(QStringLiteral("PRAGMA synchronous = NORMAL"), error)
        && exec(QStringLiteral("PRAGMA foreign_keys = ON"), error)
        && migrate(error);
    if (!ok) {
        qCWarning(lcDb) << "failed to open" << path << ":" << (error ? *error : QString());
        close();
    }
    return ok;
}

int Database::schemaVersion() const
{
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("PRAGMA user_version")) && q.next())
        return q.value(0).toInt();
    return -1;
}

bool Database::migrate(QString *error)
{
    const int current = schemaVersion();
    const int latest = latestSchemaVersion();
    if (current < 0) {
        if (error)
            *error = QStringLiteral("cannot read schema version");
        return false;
    }
    if (current > latest) {
        if (error)
            *error = QStringLiteral("database schema v%1 is newer than this build supports (v%2)")
                         .arg(current)
                         .arg(latest);
        return false;
    }

    for (int version = current + 1; version <= latest; ++version) {
        qCInfo(lcDb) << "migrating schema to v" << version;
        if (!exec(QStringLiteral("BEGIN IMMEDIATE"), error))
            return false;
        bool ok = true;
        for (const QString &stmt : kMigrations[version - 1]()) {
            if (!(ok = exec(stmt, error)))
                break;
        }
        // PRAGMA can't take bound parameters; version is our own integer.
        ok = ok && exec(QStringLiteral("PRAGMA user_version = %1").arg(version), error);
        ok = ok && exec(QStringLiteral("COMMIT"), error);
        if (!ok) {
            exec(QStringLiteral("ROLLBACK"), nullptr);
            return false;
        }
    }
    return true;
}

std::optional<qint64> Database::upsertTrack(const QString &path, qint64 fileMtime, qint64 fileSize,
                                            const TrackTags &tags, QString *error)
{
    if (!exec(QStringLiteral("SAVEPOINT upsert_track"), error))
        return std::nullopt;

    auto fail = [&]() -> std::optional<qint64> {
        exec(QStringLiteral("ROLLBACK TO upsert_track"), nullptr);
        exec(QStringLiteral("RELEASE upsert_track"), nullptr);
        return std::nullopt;
    };

    const QString title = tags.title.isEmpty() ? QFileInfo(path).completeBaseName() : tags.title;

    std::optional<qint64> artistId;
    if (!tags.artist.isEmpty() && !(artistId = upsertNamed(m_db, QStringLiteral("artists"), tags.artist, error)))
        return fail();

    std::optional<qint64> genreId;
    if (!tags.genre.isEmpty() && !(genreId = upsertNamed(m_db, QStringLiteral("genres"), tags.genre, error)))
        return fail();

    std::optional<qint64> albumId;
    if (!tags.album.isEmpty()) {
        QString albumArtist = tags.effectiveAlbumArtist();
        if (albumArtist.isEmpty() || (tags.compilation && tags.albumArtist.isEmpty()))
            albumArtist = QStringLiteral("Various Artists");
        const auto albumArtistId = upsertNamed(m_db, QStringLiteral("artists"), albumArtist, error);
        if (!albumArtistId)
            return fail();

        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(R"(
            INSERT INTO albums (title, artist_id, year, compilation, mb_album_id)
            VALUES (:title, :artist, :year, :comp, :mbid)
            ON CONFLICT (artist_id, title) DO UPDATE SET
                year        = COALESCE(albums.year, excluded.year),
                compilation = MAX(albums.compilation, excluded.compilation),
                mb_album_id = COALESCE(albums.mb_album_id, excluded.mb_album_id)
            RETURNING id)"));
        q.bindValue(QStringLiteral(":title"), tags.album);
        q.bindValue(QStringLiteral(":artist"), *albumArtistId);
        q.bindValue(QStringLiteral(":year"), nullIfZero(tags.year));
        q.bindValue(QStringLiteral(":comp"), tags.compilation ? 1 : 0);
        q.bindValue(QStringLiteral(":mbid"), nullIfEmpty(tags.musicBrainzAlbumId));
        if (!(albumId = execReturningId(q, error)))
            return fail();
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        INSERT INTO tracks (
            path, file_mtime, file_size, title, artist_id, album_id, genre_id, composer, comment,
            year, track_no, track_total, disc_no, disc_total,
            duration_ms, bitrate_kbps, sample_rate, channels, bits_per_sample, codec,
            rg_track_gain, rg_track_peak, rg_album_gain, rg_album_peak, mb_track_id, has_embedded_art)
        VALUES (
            :path, :mtime, :size, :title, :artist, :album, :genre, :composer, :comment,
            :year, :track, :trackTotal, :disc, :discTotal,
            :duration, :bitrate, :rate, :channels, :bits, :codec,
            :rgTG, :rgTP, :rgAG, :rgAP, :mbTrack, :art)
        ON CONFLICT (path) DO UPDATE SET
            file_mtime = excluded.file_mtime, file_size = excluded.file_size,
            title = excluded.title, artist_id = excluded.artist_id, album_id = excluded.album_id,
            genre_id = excluded.genre_id, composer = excluded.composer, comment = excluded.comment,
            year = excluded.year, track_no = excluded.track_no, track_total = excluded.track_total,
            disc_no = excluded.disc_no, disc_total = excluded.disc_total,
            duration_ms = excluded.duration_ms, bitrate_kbps = excluded.bitrate_kbps,
            sample_rate = excluded.sample_rate, channels = excluded.channels,
            bits_per_sample = excluded.bits_per_sample, codec = excluded.codec,
            rg_track_gain = excluded.rg_track_gain, rg_track_peak = excluded.rg_track_peak,
            rg_album_gain = excluded.rg_album_gain, rg_album_peak = excluded.rg_album_peak,
            mb_track_id = excluded.mb_track_id, has_embedded_art = excluded.has_embedded_art,
            updated_at = %1
        RETURNING id)").arg(QString::fromLatin1(kNow)));
    q.bindValue(QStringLiteral(":path"), path);
    q.bindValue(QStringLiteral(":mtime"), fileMtime);
    q.bindValue(QStringLiteral(":size"), fileSize);
    q.bindValue(QStringLiteral(":title"), title);
    q.bindValue(QStringLiteral(":artist"), artistId ? QVariant(*artistId) : QVariant());
    q.bindValue(QStringLiteral(":album"), albumId ? QVariant(*albumId) : QVariant());
    q.bindValue(QStringLiteral(":genre"), genreId ? QVariant(*genreId) : QVariant());
    q.bindValue(QStringLiteral(":composer"), nullIfEmpty(tags.composer));
    q.bindValue(QStringLiteral(":comment"), nullIfEmpty(tags.comment));
    q.bindValue(QStringLiteral(":year"), nullIfZero(tags.year));
    q.bindValue(QStringLiteral(":track"), nullIfZero(tags.trackNumber));
    q.bindValue(QStringLiteral(":trackTotal"), nullIfZero(tags.trackTotal));
    q.bindValue(QStringLiteral(":disc"), nullIfZero(tags.discNumber));
    q.bindValue(QStringLiteral(":discTotal"), nullIfZero(tags.discTotal));
    q.bindValue(QStringLiteral(":duration"), tags.durationMs);
    q.bindValue(QStringLiteral(":bitrate"), nullIfZero(tags.bitrateKbps));
    q.bindValue(QStringLiteral(":rate"), nullIfZero(tags.sampleRate));
    q.bindValue(QStringLiteral(":channels"), nullIfZero(tags.channels));
    q.bindValue(QStringLiteral(":bits"), nullIfZero(tags.bitsPerSample));
    q.bindValue(QStringLiteral(":codec"), tags.codec.isEmpty() ? QStringLiteral("Unknown") : tags.codec);
    q.bindValue(QStringLiteral(":rgTG"), nullIfNone(tags.replayGainTrackGain));
    q.bindValue(QStringLiteral(":rgTP"), nullIfNone(tags.replayGainTrackPeak));
    q.bindValue(QStringLiteral(":rgAG"), nullIfNone(tags.replayGainAlbumGain));
    q.bindValue(QStringLiteral(":rgAP"), nullIfNone(tags.replayGainAlbumPeak));
    q.bindValue(QStringLiteral(":mbTrack"), nullIfEmpty(tags.musicBrainzTrackId));
    q.bindValue(QStringLiteral(":art"), tags.hasEmbeddedArt ? 1 : 0);
    const auto trackId = execReturningId(q, error);
    if (!trackId)
        return fail();

    QSqlQuery del(m_db);
    del.prepare(QStringLiteral("DELETE FROM tracks_fts WHERE rowid = ?"));
    del.addBindValue(*trackId);
    QSqlQuery fts(m_db);
    fts.prepare(QStringLiteral("INSERT INTO tracks_fts (rowid, title, artist, album, album_artist, genre, composer) "
                               "VALUES (?, ?, ?, ?, ?, ?, ?)"));
    fts.addBindValue(*trackId);
    fts.addBindValue(title);
    fts.addBindValue(tags.artist);
    fts.addBindValue(tags.album);
    fts.addBindValue(tags.effectiveAlbumArtist());
    fts.addBindValue(tags.genre);
    fts.addBindValue(tags.composer);
    if (!del.exec() || !fts.exec()) {
        if (error)
            *error = describe(del.lastError().isValid() ? del : fts);
        return fail();
    }

    if (!exec(QStringLiteral("RELEASE upsert_track"), error))
        return fail();
    return trackId;
}

bool Database::removeTrack(const QString &path)
{
    const auto id = trackIdByPath(path);
    if (!id)
        return false;
    if (!exec(QStringLiteral("SAVEPOINT remove_track"), nullptr))
        return false;
    QSqlQuery fts(m_db);
    fts.prepare(QStringLiteral("DELETE FROM tracks_fts WHERE rowid = ?"));
    fts.addBindValue(*id);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM tracks WHERE id = ?"));
    q.addBindValue(*id);
    if (!fts.exec() || !q.exec()) {
        exec(QStringLiteral("ROLLBACK TO remove_track"), nullptr);
        exec(QStringLiteral("RELEASE remove_track"), nullptr);
        return false;
    }
    return exec(QStringLiteral("RELEASE remove_track"), nullptr);
}

std::optional<qint64> Database::trackIdByPath(const QString &path) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id FROM tracks WHERE path = ?"));
    q.addBindValue(path);
    if (q.exec() && q.next())
        return q.value(0).toLongLong();
    return std::nullopt;
}

QString Database::toFtsQuery(const QString &text)
{
    // Each whitespace-separated word becomes a quoted prefix term: "word"*.
    // Quoting neutralises FTS5 syntax (AND/OR/NEAR, column filters, parentheses).
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    QStringList terms;
    for (QString word : text.split(ws, Qt::SkipEmptyParts)) {
        word.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        terms << QLatin1Char('"') + word + QStringLiteral("\"*");
    }
    return terms.join(QLatin1Char(' '));
}

QList<qint64> Database::search(const QString &text, int limit) const
{
    QList<qint64> ids;
    const QString match = toFtsQuery(text);
    if (match.isEmpty())
        return ids;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT rowid FROM tracks_fts WHERE tracks_fts MATCH ? ORDER BY rank LIMIT ?"));
    q.addBindValue(match);
    q.addBindValue(limit);
    if (!q.exec()) {
        qCWarning(lcDb) << "search failed:" << describe(q);
        return ids;
    }
    while (q.next())
        ids << q.value(0).toLongLong();
    return ids;
}

} // namespace mmm

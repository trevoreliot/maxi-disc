// SPDX-License-Identifier: GPL-3.0-or-later
#include "libraryquery.h"

#include "database.h"

#include <QCollator>
#include <QHash>
#include <QLoggingCategory>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>

Q_LOGGING_CATEGORY(lcQuery, "mmm.query")

namespace mmm {

namespace {

const QString kTrackSelect = QStringLiteral(R"(
    SELECT t.id, t.path, t.title, ar.name, al.title, t.album_id, aa.name,
           t.track_no, t.disc_no, t.year, t.duration_ms, t.codec, al.cover_path, t.has_embedded_art
    FROM tracks t
    LEFT JOIN artists ar ON ar.id = t.artist_id
    LEFT JOIN albums  al ON al.id = t.album_id
    LEFT JOIN artists aa ON aa.id = al.artist_id)");

TrackRow trackFrom(const QSqlQuery &q)
{
    TrackRow r;
    r.id = q.value(0).toLongLong();
    r.path = q.value(1).toString();
    r.title = q.value(2).toString();
    r.artist = q.value(3).toString();
    r.album = q.value(4).toString();
    r.albumId = q.value(5).toLongLong();
    r.albumArtist = q.value(6).toString();
    r.trackNumber = q.value(7).toInt();
    r.discNumber = q.value(8).toInt();
    r.year = q.value(9).toInt();
    r.durationMs = q.value(10).toLongLong();
    r.codec = q.value(11).toString();
    r.coverPath = q.value(12).toString();
    r.hasEmbeddedArt = q.value(13).toInt() != 0;
    return r;
}

bool run(QSqlQuery &q)
{
    if (q.exec())
        return true;
    qCWarning(lcQuery) << q.lastError().text() << q.lastQuery().simplified();
    return false;
}

QCollator makeCollator()
{
    QCollator c;
    c.setCaseSensitivity(Qt::CaseInsensitive);
    c.setNumericMode(true); // "Album 2" before "Album 10"
    c.setIgnorePunctuation(false);
    return c;
}

// Sorts by precomputed collation keys: much faster than comparing strings pairwise.
template<typename T, typename KeyFn, typename TieFn>
void sortBy(QList<T> &rows, KeyFn key, TieFn tieBreak)
{
    const QCollator collator = makeCollator();
    QList<std::pair<QCollatorSortKey, qsizetype>> keyed;
    keyed.reserve(rows.size());
    for (qsizetype i = 0; i < rows.size(); ++i)
        keyed.emplace_back(collator.sortKey(key(rows[i])), i);
    std::stable_sort(keyed.begin(), keyed.end(), [&](const auto &a, const auto &b) {
        const int c = a.first.compare(b.first);
        return c != 0 ? c < 0 : tieBreak(rows[a.second], rows[b.second]);
    });
    QList<T> out;
    out.reserve(rows.size());
    for (const auto &k : keyed)
        out << std::move(rows[k.second]);
    rows = std::move(out);
}

} // namespace

QString LibraryQuery::sortName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.size() > 4 && trimmed.startsWith(QLatin1String("The "), Qt::CaseInsensitive))
        return trimmed.mid(4);
    return trimmed;
}

LibraryCounts LibraryQuery::counts() const
{
    LibraryCounts c;
    QSqlQuery q(m_db.connection());
    q.prepare(QStringLiteral("SELECT (SELECT count(*) FROM tracks), (SELECT count(*) FROM albums), "
                             "(SELECT count(DISTINCT artist_id) FROM albums)"));
    if (run(q) && q.next()) {
        c.tracks = q.value(0).toInt();
        c.albums = q.value(1).toInt();
        c.artists = q.value(2).toInt();
    }
    return c;
}

QList<ArtistRow> LibraryQuery::albumArtists() const
{
    QList<ArtistRow> rows;
    QSqlQuery q(m_db.connection());
    q.setForwardOnly(true);
    q.prepare(QStringLiteral("SELECT ar.id, ar.name, count(al.id) FROM artists ar "
                             "JOIN albums al ON al.artist_id = ar.id GROUP BY ar.id"));
    if (!run(q))
        return rows;
    while (q.next())
        rows << ArtistRow{q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toInt()};
    sortBy(rows, [](const ArtistRow &r) { return sortName(r.name); },
           [](const ArtistRow &a, const ArtistRow &b) { return a.id < b.id; });
    return rows;
}

QList<GenreRow> LibraryQuery::genres() const
{
    QList<GenreRow> rows;
    QSqlQuery q(m_db.connection());
    q.setForwardOnly(true);
    q.prepare(QStringLiteral("SELECT g.id, g.name, count(DISTINCT t.album_id) FROM genres g "
                             "JOIN tracks t ON t.genre_id = g.id WHERE t.album_id IS NOT NULL GROUP BY g.id"));
    if (!run(q))
        return rows;
    while (q.next())
        rows << GenreRow{q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toInt()};
    sortBy(rows, [](const GenreRow &r) { return r.name; },
           [](const GenreRow &a, const GenreRow &b) { return a.id < b.id; });
    return rows;
}

QList<AlbumRow> LibraryQuery::albums(qint64 artistId, qint64 genreId) const
{
    QString sql = QStringLiteral(R"(
        SELECT al.id, al.title, ar.id, ar.name, al.year, al.compilation, al.cover_path,
               count(t.id), coalesce(sum(t.duration_ms), 0),
               (SELECT e.path FROM tracks e WHERE e.album_id = al.id AND e.has_embedded_art = 1
                ORDER BY e.disc_no, e.track_no LIMIT 1)
        FROM albums al
        JOIN artists ar ON ar.id = al.artist_id
        LEFT JOIN tracks t ON t.album_id = al.id
        WHERE 1)");
    QVariantList binds;
    if (artistId > 0) {
        sql += QStringLiteral(" AND al.artist_id = ?");
        binds << artistId;
    }
    if (genreId > 0) {
        // IN (subquery) is evaluated once; a correlated EXISTS rescanned the genre per album (6 s at 100k).
        sql += QStringLiteral(" AND al.id IN (SELECT album_id FROM tracks WHERE genre_id = ?)");
        binds << genreId;
    }
    sql += QStringLiteral(" GROUP BY al.id");

    QList<AlbumRow> rows;
    QSqlQuery q(m_db.connection());
    q.setForwardOnly(true);
    q.prepare(sql);
    for (const QVariant &b : std::as_const(binds))
        q.addBindValue(b);
    if (!run(q))
        return rows;
    while (q.next()) {
        AlbumRow r;
        r.id = q.value(0).toLongLong();
        r.title = q.value(1).toString();
        r.artistId = q.value(2).toLongLong();
        r.artist = q.value(3).toString();
        r.year = q.value(4).toInt();
        r.compilation = q.value(5).toInt() != 0;
        r.coverPath = q.value(6).toString();
        r.trackCount = q.value(7).toInt();
        r.durationMs = q.value(8).toLongLong();
        r.artTrackPath = q.value(9).toString();
        rows << std::move(r);
    }

    auto byYearThenId = [](const AlbumRow &a, const AlbumRow &b) {
        return a.year != b.year ? a.year < b.year : a.id < b.id;
    };
    if (artistId > 0) {
        // One artist: chronological (unknown year first), then by title.
        sortBy(rows, [](const AlbumRow &r) { return QStringLiteral("%1").arg(r.year, 4, 10, QLatin1Char('0')) + r.title; },
               [](const AlbumRow &a, const AlbumRow &b) { return a.id < b.id; });
    } else {
        // Artist sort name, then year, then title. '\x01' keeps "Abc" + "Def" apart from "AbcD" + "ef".
        sortBy(rows,
               [](const AlbumRow &r) {
                   return sortName(r.artist) + QChar(1) + QStringLiteral("%1").arg(r.year, 4, 10, QLatin1Char('0'))
                       + r.title;
               },
               byYearThenId);
    }
    return rows;
}

QList<TrackRow> LibraryQuery::albumTracks(qint64 albumId) const
{
    QList<TrackRow> rows;
    QSqlQuery q(m_db.connection());
    q.setForwardOnly(true);
    q.prepare(kTrackSelect + QStringLiteral(" WHERE t.album_id = ? "
                                            "ORDER BY coalesce(t.disc_no, 0), coalesce(t.track_no, 0), t.title"));
    q.addBindValue(albumId);
    if (!run(q))
        return rows;
    while (q.next())
        rows << trackFrom(q);
    return rows;
}

QList<TrackRow> LibraryQuery::tracksByIds(const QList<qint64> &ids) const
{
    QList<TrackRow> rows;
    if (ids.isEmpty())
        return rows;

    QHash<qint64, TrackRow> byId;
    byId.reserve(ids.size());
    constexpr qsizetype kBatch = 500; // well under SQLite's bound-parameter limit
    for (qsizetype start = 0; start < ids.size(); start += kBatch) {
        const QList<qint64> batch = ids.mid(start, kBatch);
        QStringList marks;
        marks.fill(QStringLiteral("?"), batch.size());
        QSqlQuery q(m_db.connection());
        q.setForwardOnly(true);
        q.prepare(kTrackSelect + QStringLiteral(" WHERE t.id IN (%1)").arg(marks.join(QLatin1Char(','))));
        for (const qint64 id : batch)
            q.addBindValue(id);
        if (!run(q))
            return rows;
        while (q.next()) {
            TrackRow r = trackFrom(q);
            byId.insert(r.id, std::move(r));
        }
    }
    rows.reserve(ids.size());
    for (const qint64 id : ids) {
        const auto it = byId.constFind(id);
        if (it != byId.cend())
            rows << *it;
    }
    return rows;
}

QList<PlaylistRow> LibraryQuery::playlists() const
{
    QList<PlaylistRow> rows;
    QSqlQuery q(m_db.connection());
    q.setForwardOnly(true);
    q.prepare(QStringLiteral(R"(
        SELECT p.id, p.name, count(i.path), count(t.id)
        FROM playlists p
        LEFT JOIN playlist_items i ON i.playlist_id = p.id
        LEFT JOIN tracks t ON t.path = i.path
        GROUP BY p.id)"));
    if (!run(q))
        return rows;
    while (q.next())
        rows << PlaylistRow{q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toInt(), q.value(3).toInt()};
    sortBy(rows, [](const PlaylistRow &r) { return r.name; },
           [](const PlaylistRow &a, const PlaylistRow &b) { return a.id < b.id; });
    return rows;
}

QList<TrackRow> LibraryQuery::playlistTracks(qint64 playlistId) const
{
    QList<TrackRow> rows;
    QSqlQuery q(m_db.connection());
    q.setForwardOnly(true);
    q.prepare(kTrackSelect + QStringLiteral(" JOIN playlist_items i ON i.path = t.path "
                                            "WHERE i.playlist_id = ? ORDER BY i.position"));
    q.addBindValue(playlistId);
    if (!run(q))
        return rows;
    while (q.next())
        rows << trackFrom(q);
    return rows;
}

QList<TrackRow> LibraryQuery::tracksByPaths(const QStringList &paths) const
{
    QList<TrackRow> rows;
    QSqlQuery q(m_db.connection());
    q.prepare(kTrackSelect + QStringLiteral(" WHERE t.path = ?"));
    for (const QString &path : paths) {
        q.addBindValue(path);
        if (run(q) && q.next())
            rows << trackFrom(q);
        q.finish();
    }
    return rows;
}

QList<TrackRow> LibraryQuery::search(const QString &text, int limit) const
{
    return tracksByIds(m_db.search(text, limit));
}

} // namespace mmm

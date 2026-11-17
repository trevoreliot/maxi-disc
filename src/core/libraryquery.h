// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>

namespace mmm {

class Database;

struct ArtistRow
{
    qint64 id = 0;
    QString name;
    int albumCount = 0;
};

struct GenreRow
{
    qint64 id = 0;
    QString name;
    int albumCount = 0;
};

struct AlbumRow
{
    qint64 id = 0;
    QString title;
    qint64 artistId = 0;
    QString artist;      // album artist
    int year = 0;
    bool compilation = false;
    int trackCount = 0;
    qint64 durationMs = 0;
    QString coverPath;   // folder art, empty if none
    QString artTrackPath; // first track with embedded art, empty if none
};

struct TrackRow
{
    qint64 id = 0;
    QString path;
    QString title;
    QString artist;
    QString album;
    qint64 albumId = 0;
    QString albumArtist;
    int trackNumber = 0;
    int discNumber = 0;
    int year = 0;
    qint64 durationMs = 0;
    QString codec;
};

struct LibraryCounts
{
    int tracks = 0;
    int albums = 0;
    int artists = 0; // album artists
};

// Read-only queries behind the library browser. Results are sorted for display.
// Uses the given Database (so call it on that Database's thread).
class LibraryQuery
{
public:
    explicit LibraryQuery(const Database &db) : m_db(db) {}

    LibraryCounts counts() const;

    // Artists that have at least one album (album artists), by sort name.
    QList<ArtistRow> albumArtists() const;
    // Genres used by at least one album track, by name.
    QList<GenreRow> genres() const;
    // Albums, optionally only one album artist's and/or ones with a track in a genre (0 = any).
    // Unfiltered: by artist sort name, then year, then title. By artist: by year, then title.
    QList<AlbumRow> albums(qint64 artistId = 0, qint64 genreId = 0) const;
    // An album's tracks by disc, track number, then title.
    QList<TrackRow> albumTracks(qint64 albumId) const;
    // Rows for these track ids, in the given order (missing ids are skipped).
    QList<TrackRow> tracksByIds(const QList<qint64> &ids) const;
    // Full-text search (see Database::search), best match first.
    QList<TrackRow> search(const QString &text, int limit = 500) const;

    // Name used for sorting: leading "The " dropped ("The Beatles" sorts under B).
    static QString sortName(const QString &name);

private:
    const Database &m_db;
};

} // namespace mmm

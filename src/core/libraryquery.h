// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>

#include <optional>
#include <utility>

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
    QString coverPath;          // the album's folder art, if any
    bool hasEmbeddedArt = false;

    // Folder art if there is some, else this track's own embedded art (empty if neither).
    QString artSourcePath() const { return !coverPath.isEmpty() ? coverPath : (hasEmbeddedArt ? path : QString()); }
};

struct PlaylistRow
{
    qint64 id = 0;
    QString name;
    int itemCount = 0;      // entries in the playlist
    int availableCount = 0; // entries whose file is in the library
};

// An album as the artwork finder needs it.
struct ArtworkTarget
{
    qint64 albumId = 0;
    QString title;
    QString artist;      // album artist
    int year = 0;
    bool compilation = false;
    int trackCount = 0;
    QString mbAlbumId;   // MusicBrainz release id from the tags, if any
    bool skipped = false; // the user skipped it in the artwork finder before
    bool hasArt = false;  // folder art or a track with embedded art
    QString folder;       // folder of its first track (for display)
};

// A folder holding tracks of an album. `shared`: tracks of other albums are in it too.
struct AlbumFolder
{
    QString path;
    bool shared = false;
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
    // Tracks of the albums whose album artist this is (what the artist browser lists),
    // album by album in albums(artistId) order.
    QList<TrackRow> artistTracks(qint64 artistId) const;
    // Tracks tagged with this genre, by album artist, album, disc, track.
    QList<TrackRow> genreTracks(qint64 genreId) const;
    // Playlists by name.
    QList<PlaylistRow> playlists() const;
    // A playlist's tracks in order; entries whose file isn't in the library are skipped.
    QList<TrackRow> playlistTracks(qint64 playlistId) const;
    // Library tracks for these file paths, in the given order (unknown paths skipped).
    QList<TrackRow> tracksByPaths(const QStringList &paths) const;

    // Rows for these track ids, in the given order (missing ids are skipped).
    QList<TrackRow> tracksByIds(const QList<qint64> &ids) const;
    // Albums with neither folder art nor a track with embedded art, sorted like albums().
    // Albums skipped in the artwork finder are left out unless `includeSkipped`.
    QList<ArtworkTarget> albumsMissingArt(bool includeSkipped = false) const;
    // How many albumsMissingArt() would return: {not skipped, skipped}.
    std::pair<int, int> missingArtCounts() const;
    // One album, art or not; nullopt if there's no such album.
    std::optional<ArtworkTarget> artworkTarget(qint64 albumId) const;
    // The folders an album's tracks are directly in, in track order (disc, track).
    QList<AlbumFolder> albumFolders(qint64 albumId) const;

    // Full-text search (see Database::search), best match first.
    QList<TrackRow> search(const QString &text, int limit = 500) const;

    // Name used for sorting: leading "The " dropped ("The Beatles" sorts under B).
    static QString sortName(const QString &name);

private:
    const Database &m_db;
};

} // namespace mmm

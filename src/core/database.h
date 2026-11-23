// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "deviceprofile.h"
#include "synctypes.h"
#include "tracktags.h"

#include <QHash>
#include <QList>
#include <QSqlDatabase>
#include <QString>

#include <optional>

namespace mmm {

struct LibraryFolder
{
    qint64 id = 0;
    QString path;
};

// What a scan compares to decide whether a file needs re-reading.
struct FileState
{
    qint64 mtimeMs = 0;
    qint64 size = 0;
    friend bool operator==(const FileState &, const FileState &) = default;
};

// Identity of a track's file on disk.
struct TrackFile
{
    QString path;          // absolute
    qint64 mtimeMs = 0;
    qint64 size = 0;
    qint64 folderId = 0;   // library_folders.id, 0 = none
};

// One SQLite connection to the library database (WAL mode, foreign keys on).
// A Database object must only be used from the thread that opened it; use one
// instance per worker thread. Not copyable.
class Database
{
public:
    Database();
    ~Database();
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    // Opens (creating if needed) and migrates to the latest schema.
    // `path` may be ":memory:". Fails if the file was written by a newer schema.
    bool open(const QString &path, QString *error = nullptr);
    bool isOpen() const;
    void close();

    int schemaVersion() const;
    static int latestSchemaVersion();

    QSqlDatabase connection() const { return m_db; }

    // --- Library folders ---
    // `path` must be absolute and canonical (no trailing slash). Fails if it is inside an
    // existing library folder; existing folders inside it are absorbed into it.
    std::optional<qint64> addLibraryFolder(const QString &path, QString *error = nullptr);
    // Removes the folder and all of its tracks (and anything orphaned by that).
    bool removeLibraryFolder(const QString &path, QString *error = nullptr);
    QList<LibraryFolder> libraryFolders() const;

    // --- Tracks ---
    // Inserts or updates the track at `file.path` (matched exactly), creating artist /
    // album / genre rows as needed and updating the search index.
    // Uses a SAVEPOINT, so callers may wrap many calls in one outer transaction.
    std::optional<qint64> upsertTrack(const TrackFile &file, const TrackTags &tags, QString *error = nullptr);
    std::optional<qint64> upsertTrack(const QString &path, qint64 fileMtimeMs, qint64 fileSize,
                                      const TrackTags &tags, QString *error = nullptr)
    {
        return upsertTrack(TrackFile{path, fileMtimeMs, fileSize, 0}, tags, error);
    }
    // Stores a track again after its tags were edited (TagWriter): like upsertTrack, but keeps
    // the track's library folder (file.folderId is ignored) and never adds a track that
    // isn't in the library yet (fails with "not in the library"). If the edit moved it to another
    // album, the new album gets the old one's folder art (when it is in the track's folder).
    // Album year and compilation flag follow the edited tracks (a scan keeps the first seen).
    // Doesn't prune orphans: call pruneOrphans() after a batch.
    std::optional<qint64> updateEditedTrack(const TrackFile &file, const TrackTags &tags, QString *error = nullptr);
    bool removeTrack(const QString &path);
    std::optional<qint64> trackIdByPath(const QString &path) const;

    // Stored mtime/size of every track below directory `dir` (recursive), keyed by path.
    QHash<QString, FileState> fileStatesUnder(const QString &dir) const;

    // Deletes albums, genres and artists that no track refers to. Returns rows deleted.
    int pruneOrphans();

    // Sets `coverPath` as the folder art of albums with a track directly in `dir`
    // (not in subfolders) that don't have folder art yet.
    bool setFolderCover(const QString &dir, const QString &coverPath);
    // Clears folder art paths whose file no longer exists. Returns albums changed.
    int clearMissingCovers();
    // Sets an album's folder art explicitly (artwork finder), replacing any; "" clears it.
    // Also clears the album's "art skipped" mark. Fails if there's no such album.
    bool setAlbumCover(qint64 albumId, const QString &coverPath);
    // Marks an album as skipped by the artwork finder (it stops listing it), or unmarks it.
    bool setAlbumArtSkipped(qint64 albumId, bool skipped);

    // --- Device profiles (M8): one per sync target, keyed by Device::id ---
    QList<DeviceProfile> deviceProfiles() const; // by name
    std::optional<DeviceProfile> deviceProfile(const QString &deviceId) const;
    // Inserts or replaces the device's profile. Needs a device id and a name.
    bool saveDeviceProfile(const DeviceProfile &profile, QString *error = nullptr);
    bool deleteDeviceProfile(const QString &deviceId);
    // Remembers the device's current name (for listing profiles of unplugged devices).
    bool setDeviceLastSeenName(const QString &deviceId, const QString &name);

    // --- Device sync (M9): each device's selection and what was written to it ---
    QList<SyncItem> syncItems(const QString &deviceId) const; // in the order added
    // Adds an entry to the device's selection; adding one that's there already returns its id.
    std::optional<qint64> addSyncItem(const SyncItem &item, QString *error = nullptr);
    bool removeSyncItem(qint64 id);
    QList<ManifestEntry> manifest(const QString &deviceId) const; // by path
    // Records a file written to the device, replacing the row at `replacesRelPath` too (a
    // move, or an update to a new path). Paths are matched case-insensitively (FAT/exFAT).
    bool putManifestEntry(const ManifestEntry &entry, const QString &replacesRelPath = {},
                          QString *error = nullptr);
    bool removeManifestEntry(const QString &deviceId, const QString &relPath);
    // Tags of the audio files on a device (M9b's cache), keyed by pathKey(relPath).
    QHash<QString, DeviceTrack> deviceTracks(const QString &deviceId) const;
    // Inserts or replaces these rows (one transaction). The flags aren't stored.
    bool putDeviceTracks(const QString &deviceId, const QList<DeviceTrack> &tracks, QString *error = nullptr);
    bool removeDeviceTracks(const QString &deviceId, const QStringList &relPaths);
    // How device paths are compared: case-folded (FAT, exFAT and NTFS ignore case).
    static QString pathKey(const QString &relPath) { return relPath.toCaseFolded(); }

    // --- Playlists (items are file paths, in order; duplicates allowed) ---
    std::optional<qint64> createPlaylist(const QString &name, const QStringList &paths = {},
                                         QString *error = nullptr);
    bool renamePlaylist(qint64 id, const QString &name);
    bool deletePlaylist(qint64 id);
    bool setPlaylistPaths(qint64 id, const QStringList &paths, QString *error = nullptr);
    bool appendToPlaylist(qint64 id, const QStringList &paths, QString *error = nullptr);
    QStringList playlistPaths(qint64 id) const;

    // Full-text search over title / artist / album / album artist / genre / composer.
    // Every word is a prefix match ("beat abb" finds "The Beatles – Abbey Road").
    // Returns track ids, best match first.
    QList<qint64> search(const QString &text, int limit = 500) const;

    // Turns user input into a safe FTS5 MATCH expression (exposed for tests).
    static QString toFtsQuery(const QString &text);

    // [dir + "/", dir + "0"): half-open range of paths strictly below `dir`.
    static std::pair<QString, QString> prefixRange(const QString &dir);

private:
    bool migrate(QString *error);
    bool exec(const QString &sql, QString *error);

    QString m_connectionName;
    QSqlDatabase m_db;
};

} // namespace mmm

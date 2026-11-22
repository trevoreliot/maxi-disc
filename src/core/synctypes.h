// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

#include <optional>

#include "deviceprofile.h"
#include "libraryquery.h"

// Value types of a device sync (M9): what the user picked for a device, what was written to
// it, and the plan / progress / result of one sync. Plain values, passed between threads.
namespace mmm {

// One entry of a device's sync selection: a playlist, album, artist or genre from the
// library, resolved to tracks when a sync is planned (so later additions come along).
struct SyncItem
{
    enum class Kind { Playlist, Album, Artist, Genre };
    qint64 id = 0;         // device_sync_items.id
    QString deviceId;
    Kind kind = Kind::Album;
    qint64 targetId = 0;   // playlist / album / artist / genre id
    QString name;          // as shown when it was added (kept if the target disappears)
    friend bool operator==(const SyncItem &, const SyncItem &) = default;

    static QString toString(Kind k);              // "playlist", "album", "artist", "genre"
    static std::optional<Kind> kindFrom(const QString &s);
};

// A file the app wrote to a device (device_manifest). Only these files are ever replaced,
// moved or deleted by a sync: anything else on the device is the user's.
struct ManifestEntry
{
    enum class Kind { Track, Playlist };
    QString deviceId;
    QString relPath;        // from the mount point, '/'-separated, as written ("Music/A/B/01 C.mp3")
    Kind kind = Kind::Track;
    QString sourcePath;     // library file, or "playlist:<id>"
    qint64 sourceSize = 0;  // the source file when it was synced (tracks)
    qint64 sourceMtimeMs = 0;
    QString outputSig;      // how the file was made (SyncPlanner::outputSig); playlists: content hash
    qint64 destSize = 0;    // the device file after writing it
    qint64 destMtimeMs = 0;
    // For listing the device's music without reading it (M9b).
    QString title;
    QString artist;
    QString album;
    qint64 durationMs = 0;
    QString codec;          // what's on the device ("MP3", "FLAC", "M3U8")
    friend bool operator==(const ManifestEntry &, const ManifestEntry &) = default;
};

// One step of a sync plan.
struct SyncAction
{
    enum class Kind {
        Add,    // write a file that isn't on the device yet
        Update, // rewrite one of ours (source or profile changed, or changed on the device)
        Move,   // rename one of ours (layout changed); no copying
        Remove, // delete one of ours (no longer selected)
        Forget, // drop a manifest row: the file is gone, or was changed by the user (kept)
    };
    enum class Reason {
        New,
        MissingOnDevice,
        SourceChanged,
        ProfileChanged,
        ChangedOnDevice,
        LayoutChanged,
        Deselected,
        PlaylistChanged,
        GoneFromDevice,
        KeptUserChanges,
    };
    Kind kind = Kind::Add;
    ManifestEntry::Kind itemKind = ManifestEntry::Kind::Track;
    Reason reason = Reason::New;

    QString destRelPath;    // Add / Update / Move: where the file goes
    QString oldRelPath;     // Update / Move / Remove / Forget: where it is (manifest path)
    qint64 bytes = 0;       // Add / Update: (estimated) bytes to write
    qint64 oldBytes = 0;    // Update / Remove: bytes freed on the device

    // Tracks
    TrackRow track;
    qint64 sourceSize = 0;
    qint64 sourceMtimeMs = 0;
    QString outputSig;
    bool prepare = false;   // goes through the transcode cache (transcode and/or art change)
    bool transcode = false;
    QString artPath;        // folder art to use (empty: the track's embedded art)
    QString cacheKey;       // TranscodeCache key when `prepare`
    QString codec;          // what the device gets ("MP3", "FLAC", ...)

    // Playlists
    QString playlistName;
    QByteArray playlistData;
};

struct SyncPlan
{
    QString deviceId;
    QString mountPoint;
    DeviceProfile profile;
    QList<SyncAction> actions;   // in run order: removals, moves, writes, playlists
    QStringList staleTempFiles;  // leftovers of an interrupted sync (relative paths)

    int adds = 0;
    int updates = 0;
    int moves = 0;
    int removes = 0;
    int unchanged = 0;
    int playlistWrites = 0;
    int untracked = 0;           // files on the device the app didn't write (left alone)
    qint64 bytesToWrite = 0;
    qint64 bytesFreed = 0;
    qint64 freeBefore = -1;      // -1: unknown
    qint64 freeAfter = -1;
    bool insufficientSpace = false;
    QStringList missingItems;    // selection entries no longer in the library
    QStringList warnings;
    QString error;               // planning failed (device gone, read-only, ...)
    bool canceled = false;       // ... because it was canceled

    bool isValid() const { return error.isEmpty(); }
    bool hasWork() const { return !actions.isEmpty() || !staleTempFiles.isEmpty(); }
};

struct SyncProgress
{
    int filesDone = 0;
    int filesTotal = 0;
    qint64 bytesDone = 0;
    qint64 bytesTotal = 0;
    QString currentFile;  // relative path on the device
    QString stage;        // "Converting", "Copying", "Removing", ...
};

struct SyncResult
{
    bool ok = false;
    bool canceled = false;
    int added = 0;
    int updated = 0;
    int moved = 0;
    int removed = 0;
    int playlistsWritten = 0;
    int failed = 0;
    qint64 bytesWritten = 0;
    QString error;        // the sync stopped (device unplugged, full, ...)
    QStringList warnings; // per-file failures; the sync went on
};

} // namespace mmm

Q_DECLARE_METATYPE(mmm::SyncPlan)
Q_DECLARE_METATYPE(mmm::SyncProgress)
Q_DECLARE_METATYPE(mmm::SyncResult)

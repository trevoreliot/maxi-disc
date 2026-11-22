// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QList>
#include <QString>

#include <atomic>
#include <functional>

#include "device.h"
#include "devicescanner.h"
#include "synctypes.h"

namespace mmm {

class Database;
class LibraryQuery;
class TranscodeCache;

// A device's selection turned into tracks.
struct ResolvedPlaylist
{
    qint64 id = 0;
    QString name;
    QList<TrackRow> tracks; // in playlist order, repeats kept
};
struct ResolvedSelection
{
    QList<TrackRow> tracks;            // every track once, in selection order
    QList<ResolvedPlaylist> playlists; // the selected playlists (written to the device)
    QHash<qint64, int> trackCounts;    // SyncItem::id → its number of tracks
    QStringList missing;               // names of entries no longer in the library
};

// A library file as it is now.
struct SourceStat
{
    bool exists = false;
    qint64 size = 0;
    qint64 mtimeMs = 0;
    QString folderArt;      // the album's folder art if it exists ("" if none)
    QString folderArtSig;   // "<size>:<mtime>" of that file
};

struct PlannerInput
{
    QString deviceId;
    QString mountPoint;
    DeviceProfile profile;
    ResolvedSelection selection;
    QHash<QString, SourceStat> sources; // by TrackRow::path
    QList<ManifestEntry> manifest;
    DeviceScan scan;
    qint64 freeBytes = -1;              // -1: unknown (no space check)
    // Exact size of a prepared file already in the transcode cache, or -1.
    std::function<qint64(const QString &key, const QString &extension)> cachedSize;
};

// Decides what a sync does (M9): compares what the selection wants on the device with what
// the app wrote there before (the manifest) and what's there now (a DeviceScanner scan).
// Files the app didn't write are never touched: their names are avoided ("Name (2).mp3").
// diff() is pure (no I/O); plan() gathers its input from the database and the device.
class SyncPlanner
{
public:
    static constexpr qint64 kSpaceMarginBytes = 8LL * 1024 * 1024;

    // Runs on a worker thread with that thread's Database. Never writes anything.
    static SyncPlan plan(Database &db, const Device &device, const DeviceProfile &profile,
                         const TranscodeCache *cache, const std::atomic_bool *cancel = nullptr);

    static ResolvedSelection resolve(const LibraryQuery &query, const QList<SyncItem> &items);
    static QHash<QString, SourceStat> statSources(const QList<TrackRow> &tracks);
    static SyncPlan diff(const PlannerInput &input);

    // Where a playlist goes: "<musicDir>/Playlists/<name>.m3u8".
    static QString playlistPath(const DeviceProfile &profile, const QString &name);
    // Equal device timestamps: within FAT's 2 s resolution, also when shifted by whole hours
    // (FAT keeps local time, so a daylight-saving change moves every file by an hour).
    static bool sameDeviceTime(qint64 aMs, qint64 bMs);
    // A generous estimate of an MP3's size (the plan's space check).
    static qint64 estimateMp3Bytes(qint64 durationMs, TranscodeTarget target);
    // "Name (2).mp3" for "Name.mp3", shortened so the whole path stays within maxLength.
    static QString withSuffix(const QString &relPath, int n, int maxLength = 240);
};

} // namespace mmm

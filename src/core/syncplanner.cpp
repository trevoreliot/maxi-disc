// SPDX-License-Identifier: GPL-3.0-or-later
#include "syncplanner.h"

#include "database.h"
#include "libraryquery.h"
#include "m3u.h"
#include "pathlayout.h"
#include "transcodecache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStorageInfo>

#include <algorithm>

namespace mmm {

namespace {

using Kind = SyncAction::Kind;
using Reason = SyncAction::Reason;

QString key(const QString &relPath)
{
    return Database::pathKey(relPath);
}

// The file is as the app left it: same size and (allowing for FAT) the same time.
bool unchangedOnDevice(const DeviceFile &f, const ManifestEntry &m)
{
    return f.size == m.destSize && SyncPlanner::sameDeviceTime(f.mtimeMs, m.destMtimeMs);
}

QString dirOf(const QString &relPath)
{
    const qsizetype slash = relPath.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? QString() : relPath.left(slash);
}

// "A/B (3).mp3" is a suffixed variant of "A/B.mp3": one withSuffix() makes (so also when it
// had to shorten the name).
bool isVariantOf(const QString &candidate, const QString &base)
{
    const QString c = key(candidate);
    for (int n = 2; n < 1000; ++n) {
        if (key(SyncPlanner::withSuffix(base, n)) == c)
            return true;
    }
    return false;
}

// Hands out device paths: never one of a file the app doesn't own, never the same twice
// (compared case-insensitively, as FAT/exFAT do).
class PathAllocator
{
public:
    void claim(const QString &relPath) { m_claimed.insert(key(relPath)); }
    bool isClaimed(const QString &relPath) const { return m_claimed.contains(key(relPath)); }
    QString allocate(const QString &base)
    {
        QString candidate = base;
        for (int n = 2; isClaimed(candidate); ++n)
            candidate = SyncPlanner::withSuffix(base, n);
        claim(candidate);
        return candidate;
    }

private:
    QSet<QString> m_claimed;
};

// One thing the selection wants on the device.
struct Wanted
{
    QString base;              // its path by the profile's layout
    QString path;              // after collisions
    const ManifestEntry *previous = nullptr;
    qsizetype order = 0;
};

// Picks every wanted item's path: items already at their layout path keep it, then the
// rest in a stable order (so "(2)" names don't flip between syncs).
void assignPaths(QList<Wanted> &wanted, PathAllocator &paths)
{
    for (Wanted &w : wanted) {
        if (w.previous && key(w.previous->relPath) == key(w.base) && !paths.isClaimed(w.base)) {
            w.path = w.previous->relPath; // keep the casing it has on the device
            paths.claim(w.path);
        }
    }
    QList<Wanted *> rest;
    for (Wanted &w : wanted) {
        if (w.path.isEmpty())
            rest << &w;
    }
    std::stable_sort(rest.begin(), rest.end(), [](const Wanted *a, const Wanted *b) {
        const int c = key(a->base).compare(key(b->base));
        return c != 0 ? c < 0 : a->order < b->order;
    });
    for (Wanted *w : std::as_const(rest)) {
        if (w->previous && !paths.isClaimed(w->previous->relPath) && isVariantOf(w->previous->relPath, w->base)) {
            w->path = w->previous->relPath;
            paths.claim(w->path);
        } else {
            w->path = paths.allocate(w->base);
        }
    }
}

QString outputSig(bool prepare, bool transcode, const DeviceProfile &profile, const SourceStat &source)
{
    if (!prepare)
        return QStringLiteral("v1|copy");
    QString art;
    if (profile.artworkPolicy != ArtworkPolicy::Strip)
        art = source.folderArt.isEmpty() ? QStringLiteral("embedded")
                                         : QStringLiteral("folder:%1:%2").arg(source.folderArt, source.folderArtSig);
    return QStringLiteral("v1|%1|%2|%3")
        .arg(transcode ? DeviceProfiles::toString(profile.transcodeTarget) : QStringLiteral("copy"),
             DeviceProfiles::toString(profile.artworkPolicy), art);
}

QString displayArtist(const TrackRow &t)
{
    return t.artist.isEmpty() ? t.albumArtist : t.artist;
}

} // namespace

QString SyncPlanner::playlistPath(const DeviceProfile &profile, const QString &name)
{
    const QString dir = PathLayout::cleanFolder(profile.musicDir);
    return (dir.isEmpty() ? QString() : dir + QLatin1Char('/')) + QStringLiteral("Playlists/")
        + PathLayout::sanitizeName(name) + QStringLiteral(".m3u8");
}

bool SyncPlanner::sameDeviceTime(qint64 aMs, qint64 bMs)
{
    constexpr qint64 kTolerance = 2000;
    constexpr qint64 kQuarter = 15 * 60 * 1000; // time zones are whole quarter hours off UTC
    const qint64 d = std::abs(aMs - bMs);
    if (d <= kTolerance)
        return true;
    const qint64 nearest = (d + kQuarter / 2) / kQuarter * kQuarter;
    return nearest <= 14 * 4 * kQuarter && std::abs(d - nearest) <= kTolerance;
}

qint64 SyncPlanner::estimateMp3Bytes(qint64 durationMs, TranscodeTarget target)
{
    // V0 averages ~245 kbit/s; allow up to 260. Plus room for tags and a 500 px cover.
    const qint64 bytesPerSecond = target == TranscodeTarget::Mp3Cbr320 ? 40000 : 32500;
    return durationMs * bytesPerSecond / 1000 + 256 * 1024;
}

QString SyncPlanner::withSuffix(const QString &relPath, int n, int maxLength)
{
    const QString dir = dirOf(relPath);
    const QString file = relPath.mid(dir.isEmpty() ? 0 : dir.size() + 1);
    const qsizetype dot = file.lastIndexOf(QLatin1Char('.'));
    QString stem = dot > 0 ? file.left(dot) : file;
    const QString ext = dot > 0 ? file.mid(dot) : QString();
    const QString suffix = QStringLiteral(" (%1)").arg(n);
    const qsizetype room = maxLength - (dir.isEmpty() ? 0 : dir.size() + 1) - suffix.size() - ext.size();
    if (stem.size() > room) {
        stem = PathLayout::sanitizeName(stem, static_cast<int>(std::max<qsizetype>(1, room)));
    }
    return (dir.isEmpty() ? QString() : dir + QLatin1Char('/')) + stem + suffix + ext;
}

ResolvedSelection SyncPlanner::resolve(const LibraryQuery &query, const QList<SyncItem> &items)
{
    ResolvedSelection out;
    QSet<QString> seen;
    QHash<qint64, QString> playlistNames;
    for (const PlaylistRow &p : query.playlists())
        playlistNames.insert(p.id, p.name);
    for (const SyncItem &item : items) {
        QList<TrackRow> tracks;
        bool exists = true;
        switch (item.kind) {
        case SyncItem::Kind::Playlist:
            exists = playlistNames.contains(item.targetId);
            if (exists) {
                tracks = query.playlistTracks(item.targetId);
                out.playlists << ResolvedPlaylist{item.targetId, playlistNames.value(item.targetId), tracks};
            }
            break;
        case SyncItem::Kind::Album:
            tracks = query.albumTracks(item.targetId);
            break;
        case SyncItem::Kind::Artist:
            tracks = query.artistTracks(item.targetId);
            break;
        case SyncItem::Kind::Genre:
            tracks = query.genreTracks(item.targetId);
            break;
        }
        if (!exists || (tracks.isEmpty() && item.kind != SyncItem::Kind::Playlist))
            out.missing << item.name;
        out.trackCounts.insert(item.id, static_cast<int>(tracks.size()));
        for (const TrackRow &t : std::as_const(tracks)) {
            if (!seen.contains(t.path)) {
                seen.insert(t.path);
                out.tracks << t;
            }
        }
    }
    return out;
}

QHash<QString, SourceStat> SyncPlanner::statSources(const QList<TrackRow> &tracks)
{
    QHash<QString, SourceStat> out;
    QHash<QString, std::pair<bool, QString>> art; // cover path → exists, sig
    for (const TrackRow &t : tracks) {
        if (out.contains(t.path))
            continue;
        const QFileInfo info(t.path);
        SourceStat s;
        s.exists = info.isFile();
        s.size = info.size();
        s.mtimeMs = info.lastModified().toMSecsSinceEpoch();
        if (!t.coverPath.isEmpty()) {
            auto it = art.find(t.coverPath);
            if (it == art.end()) {
                const QFileInfo cover(t.coverPath);
                it = art.insert(t.coverPath, {cover.isFile(), QStringLiteral("%1:%2").arg(cover.size()).arg(
                                                                cover.lastModified().toMSecsSinceEpoch())});
            }
            if (it->first) {
                s.folderArt = t.coverPath;
                s.folderArtSig = it->second;
            }
        }
        out.insert(t.path, s);
    }
    return out;
}

SyncPlan SyncPlanner::diff(const PlannerInput &in)
{
    SyncPlan plan;
    plan.deviceId = in.deviceId;
    plan.mountPoint = in.mountPoint;
    plan.profile = in.profile;
    plan.missingItems = in.selection.missing;
    plan.staleTempFiles = in.scan.tempFiles;
    plan.freeBefore = in.freeBytes;

    QHash<QString, const ManifestEntry *> byKey;
    QHash<QString, const ManifestEntry *> trackBySource;
    QHash<QString, const ManifestEntry *> playlistBySource;
    for (const ManifestEntry &m : in.manifest) {
        byKey.insert(key(m.relPath), &m);
        auto &bySource = m.kind == ManifestEntry::Kind::Track ? trackBySource : playlistBySource;
        if (!bySource.contains(m.sourcePath))
            bySource.insert(m.sourcePath, &m);
    }

    // Names that aren't ours to use: the user's files, and files of ours the user changed
    // and that no longer belong to the selection (left in place).
    PathAllocator paths;
    QSet<QString> wantedSources;
    for (const TrackRow &t : in.selection.tracks)
        wantedSources.insert(t.path);
    for (const ResolvedPlaylist &p : in.selection.playlists)
        wantedSources.insert(QStringLiteral("playlist:%1").arg(p.id));
    for (auto it = in.scan.files.cbegin(); it != in.scan.files.cend(); ++it) {
        const ManifestEntry *m = byKey.value(it.key());
        if (!m) {
            ++plan.untracked;
            paths.claim(it->relPath);
        } else if (!wantedSources.contains(m->sourcePath) && !unchangedOnDevice(*it, *m)) {
            paths.claim(it->relPath);
        }
    }

    QSet<QString> consumed; // manifest keys still wanted
    QHash<QString, QString> trackDest; // source path → device path (for playlists)
    QList<SyncAction> removals, moves, writes, playlistActions;

    // --- Tracks ---
    struct TrackWant
    {
        const TrackRow *track;
        SourceStat source;
        DeviceProfiles::Decision decision;
    };
    QList<TrackWant> trackWants;
    QList<Wanted> wanted;
    for (const TrackRow &t : in.selection.tracks) {
        const SourceStat source = in.sources.value(t.path);
        const ManifestEntry *previous = trackBySource.value(t.path);
        if (!source.exists) {
            // The library file is gone (or its drive is unplugged): leave the device's copy.
            plan.warnings << QStringLiteral("Not found, skipped: %1").arg(t.path);
            if (previous) {
                consumed.insert(key(previous->relPath));
                trackDest.insert(t.path, previous->relPath);
                paths.claim(previous->relPath);
            }
            continue;
        }
        trackWants << TrackWant{&t, source, DeviceProfiles::decide(t.codec, in.profile)};
        wanted << Wanted{PathLayout::render(t, in.profile), QString(), previous, wanted.size()};
    }
    assignPaths(wanted, paths);

    for (qsizetype i = 0; i < trackWants.size(); ++i) {
        const TrackWant &tw = trackWants[i];
        const Wanted &w = wanted[i];
        const TrackRow &t = *tw.track;
        trackDest.insert(t.path, w.path);

        const ArtworkPolicy artPolicy = in.profile.artworkPolicy;
        const bool hasArt = !tw.source.folderArt.isEmpty() || t.hasEmbeddedArt;
        const bool prepare = tw.decision.transcode || (artPolicy == ArtworkPolicy::Shrink500 && hasArt)
            || (artPolicy == ArtworkPolicy::Strip && t.hasEmbeddedArt);

        SyncAction a;
        a.itemKind = ManifestEntry::Kind::Track;
        a.track = t;
        a.destRelPath = w.path;
        a.sourceSize = tw.source.size;
        a.sourceMtimeMs = tw.source.mtimeMs;
        a.prepare = prepare;
        a.transcode = tw.decision.transcode;
        a.artPath = tw.source.folderArt;
        a.outputSig = outputSig(prepare, tw.decision.transcode, in.profile, tw.source);
        a.codec = tw.decision.transcode ? QStringLiteral("MP3") : t.codec;
        if (prepare)
            a.cacheKey = TranscodeCache::key(t.path, tw.source.size, tw.source.mtimeMs, a.outputSig);
        const qint64 cached = prepare && in.cachedSize ? in.cachedSize(a.cacheKey, tw.decision.extension) : -1;
        a.bytes = cached >= 0                   ? cached
            : tw.decision.transcode             ? estimateMp3Bytes(t.durationMs, in.profile.transcodeTarget)
                                                : tw.source.size;

        const ManifestEntry *m = w.previous;
        if (!m) {
            a.kind = Kind::Add;
            a.reason = Reason::New;
            writes << a;
            continue;
        }
        consumed.insert(key(m->relPath));
        a.oldRelPath = m->relPath;
        const auto f = in.scan.files.constFind(key(m->relPath));
        if (f == in.scan.files.cend()) {
            a.kind = Kind::Add;
            a.reason = Reason::MissingOnDevice;
            writes << a;
            continue;
        }
        a.oldRelPath = f->relPath; // as it's spelled on the device (ext4 sticks care)
        const bool sourceChanged = tw.source.size != m->sourceSize || tw.source.mtimeMs != m->sourceMtimeMs;
        const bool profileChanged = a.outputSig != m->outputSig;
        const bool deviceChanged = !unchangedOnDevice(*f, *m);
        if (sourceChanged || profileChanged || deviceChanged) {
            a.kind = Kind::Update;
            a.reason = sourceChanged ? Reason::SourceChanged : profileChanged ? Reason::ProfileChanged : Reason::ChangedOnDevice;
            a.oldBytes = f->size;
            writes << a;
        } else if (key(w.path) != key(m->relPath)) {
            a.kind = Kind::Move;
            a.reason = Reason::LayoutChanged;
            a.bytes = 0;
            moves << a;
        } else {
            ++plan.unchanged;
        }
    }

    // --- Playlists (after the tracks: they list the tracks' device paths) ---
    QList<Wanted> wantedPlaylists;
    for (const ResolvedPlaylist &p : in.selection.playlists)
        wantedPlaylists << Wanted{playlistPath(in.profile, p.name), QString(),
                                  playlistBySource.value(QStringLiteral("playlist:%1").arg(p.id)), wantedPlaylists.size()};
    assignPaths(wantedPlaylists, paths);
    const QDir mount(in.mountPoint.isEmpty() ? QStringLiteral("/") : in.mountPoint);
    for (qsizetype i = 0; i < in.selection.playlists.size(); ++i) {
        const ResolvedPlaylist &p = in.selection.playlists[i];
        const Wanted &w = wantedPlaylists[i];
        QList<M3u::Entry> entries;
        for (const TrackRow &t : p.tracks) {
            const auto dest = trackDest.constFind(t.path);
            if (dest == trackDest.cend())
                continue;
            entries << M3u::Entry{mount.filePath(*dest), displayArtist(t) + QStringLiteral(" - ") + t.title, t.durationMs};
        }
        SyncAction a;
        a.itemKind = ManifestEntry::Kind::Playlist;
        a.playlistName = p.name;
        a.destRelPath = w.path;
        a.playlistData = M3u::format(mount.filePath(w.path), entries, true);
        a.outputSig = QString::fromLatin1(QCryptographicHash::hash(a.playlistData, QCryptographicHash::Sha1).toHex());
        a.bytes = a.playlistData.size();
        a.codec = QStringLiteral("M3U8");
        a.track.path = QStringLiteral("playlist:%1").arg(p.id);
        a.track.title = p.name;
        a.track.durationMs = 0;
        for (const TrackRow &t : p.tracks)
            a.track.durationMs += t.durationMs;

        const ManifestEntry *m = w.previous;
        if (!m) {
            a.kind = Kind::Add;
            a.reason = Reason::New;
            playlistActions << a;
            continue;
        }
        consumed.insert(key(m->relPath));
        a.oldRelPath = m->relPath;
        const auto f = in.scan.files.constFind(key(m->relPath));
        if (f != in.scan.files.cend())
            a.oldRelPath = f->relPath;
        if (f == in.scan.files.cend()) {
            a.kind = Kind::Add;
            a.reason = Reason::MissingOnDevice;
        } else if (a.outputSig != m->outputSig || !unchangedOnDevice(*f, *m)) {
            a.kind = Kind::Update;
            a.reason = a.outputSig != m->outputSig ? Reason::PlaylistChanged : Reason::ChangedOnDevice;
            a.oldBytes = f->size;
        } else if (key(w.path) != key(m->relPath)) {
            a.kind = Kind::Move;
            a.reason = Reason::LayoutChanged;
            a.bytes = 0;
        } else {
            continue; // unchanged
        }
        playlistActions << a;
    }

    // --- What the app wrote before that isn't wanted any more ---
    for (const ManifestEntry &m : in.manifest) {
        if (consumed.contains(key(m.relPath)))
            continue;
        SyncAction a;
        a.itemKind = m.kind;
        a.oldRelPath = m.relPath;
        a.track.path = m.sourcePath;
        a.track.title = m.title;
        a.track.artist = m.artist;
        a.track.album = m.album;
        a.codec = m.codec;
        const auto f = in.scan.files.constFind(key(m.relPath));
        if (f == in.scan.files.cend()) {
            a.kind = Kind::Forget;
            a.reason = Reason::GoneFromDevice;
        } else if (unchangedOnDevice(*f, m)) {
            a.oldRelPath = f->relPath;
            a.kind = Kind::Remove;
            a.reason = Reason::Deselected;
            a.oldBytes = f->size;
        } else {
            a.kind = Kind::Forget;
            a.reason = Reason::KeptUserChanges;
            plan.warnings << QStringLiteral("Left in place (changed on the device): %1").arg(m.relPath);
        }
        removals << a;
    }

    plan.actions = removals + moves + writes + playlistActions;
    for (const SyncAction &a : std::as_const(plan.actions)) {
        switch (a.kind) {
        case Kind::Add:
            a.itemKind == ManifestEntry::Kind::Playlist ? ++plan.playlistWrites : ++plan.adds;
            break;
        case Kind::Update:
            a.itemKind == ManifestEntry::Kind::Playlist ? ++plan.playlistWrites : ++plan.updates;
            break;
        case Kind::Move:
            ++plan.moves;
            break;
        case Kind::Remove:
            ++plan.removes;
            break;
        case Kind::Forget:
            break;
        }
        plan.bytesToWrite += a.bytes;
        plan.bytesFreed += a.oldBytes;
    }
    if (in.freeBytes >= 0) {
        plan.freeAfter = in.freeBytes - plan.bytesToWrite + plan.bytesFreed;
        // The low point can come mid-sync: walk the actions in run order. An update frees its
        // old file only once the new one is written.
        qint64 free = in.freeBytes;
        qint64 lowest = free;
        for (const SyncAction &a : std::as_const(plan.actions)) {
            free -= a.bytes;
            lowest = std::min(lowest, free);
            free += a.oldBytes;
        }
        plan.insufficientSpace = lowest < kSpaceMarginBytes;
    }
    return plan;
}

SyncPlan SyncPlanner::plan(Database &db, const Device &device, const DeviceProfile &profile,
                           const TranscodeCache *cache, const std::atomic_bool *cancel)
{
    SyncPlan failed;
    failed.deviceId = profile.deviceId;
    failed.profile = profile;
    failed.mountPoint = device.mountPoint;
    if (!device.mounted()) {
        failed.error = QStringLiteral("Mount the device first.");
        return failed;
    }
    if (device.readOnly || !QFileInfo(device.mountPoint).isWritable()) {
        failed.error = QStringLiteral("The device is read-only.");
        return failed;
    }

    const QList<SyncItem> items = db.syncItems(profile.deviceId);
    PlannerInput in;
    in.deviceId = profile.deviceId;
    in.mountPoint = device.mountPoint;
    in.profile = profile;
    in.selection = resolve(LibraryQuery(db), items);
    in.manifest = db.manifest(profile.deviceId);

    // Manifest files outside the music folder (it was renamed in the profile) are checked too.
    const QString musicDir = PathLayout::cleanFolder(profile.musicDir);
    QStringList extra;
    for (const ManifestEntry &m : std::as_const(in.manifest)) {
        if (!musicDir.isEmpty() && !key(m.relPath).startsWith(key(musicDir + QLatin1Char('/'))))
            extra << m.relPath;
    }
    in.scan = DeviceScanner::scan(device.mountPoint, musicDir, extra, cancel);
    if (!in.scan.error.isEmpty() || in.scan.canceled) {
        failed.canceled = in.scan.canceled;
        failed.error = in.scan.canceled ? QStringLiteral("Canceled.") : in.scan.error;
        return failed;
    }
    in.sources = statSources(in.selection.tracks);
    if (cancel && cancel->load()) {
        failed.canceled = true;
        failed.error = QStringLiteral("Canceled.");
        return failed;
    }
    const QStorageInfo storage(device.mountPoint);
    in.freeBytes = storage.isValid() ? storage.bytesAvailable() : -1;
    if (cache) {
        in.cachedSize = [cache](const QString &k, const QString &ext) { return cache->size(k, ext); };
    }
    return diff(in);
}

} // namespace mmm

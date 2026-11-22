// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include "database.h"
#include "pathlayout.h"
#include "syncplanner.h"

using namespace mmm;

namespace {

using Kind = SyncAction::Kind;
using Reason = SyncAction::Reason;

constexpr qint64 kTime = 1'700'000'000'000; // device and source mtimes in the tests

DeviceProfile flash()
{
    DeviceProfile p = *DeviceProfiles::builtIn(QStringLiteral("usb-flash"));
    p.deviceId = QStringLiteral("uuid:T");
    p.name = QStringLiteral("Stick");
    return p;
}

DeviceProfile dap()
{
    DeviceProfile p = *DeviceProfiles::builtIn(QStringLiteral("generic-dap"));
    p.deviceId = QStringLiteral("uuid:T");
    p.name = QStringLiteral("DAP");
    p.artworkPolicy = ArtworkPolicy::Keep;
    return p;
}

TrackRow track(qint64 id, const QString &artist, const QString &album, const QString &title, int number,
               const QString &codec = QStringLiteral("FLAC"))
{
    TrackRow t;
    t.id = id;
    t.path = QStringLiteral("/lib/%1.%2").arg(id).arg(codec.toLower());
    t.artist = artist;
    t.albumArtist = artist;
    t.album = album;
    t.title = title;
    t.trackNumber = number;
    t.durationMs = 180'000;
    t.codec = codec;
    return t;
}

// The planner input after a sync that wrote exactly what `previous` planned.
struct World
{
    PlannerInput in;

    explicit World(const DeviceProfile &profile)
    {
        in.deviceId = profile.deviceId;
        in.mountPoint = QStringLiteral("/media/stick");
        in.profile = profile;
        in.freeBytes = 1'000'000'000;
    }
    void select(const QList<TrackRow> &tracks)
    {
        in.selection.tracks = tracks;
        for (const TrackRow &t : tracks)
            in.sources.insert(t.path, SourceStat{true, 5'000'000 + t.id, kTime, {}, {}});
    }
    void addDeviceFile(const QString &rel, qint64 size, qint64 mtime = kTime)
    {
        in.scan.files.insert(Database::pathKey(rel), DeviceFile{rel, size, mtime});
    }
    // Applies a plan the way UsbSync would (manifest + device files), without I/O.
    void apply(const SyncPlan &plan)
    {
        for (const SyncAction &a : plan.actions) {
            auto removeEntry = [&](const QString &rel) {
                in.scan.files.remove(Database::pathKey(rel));
                in.manifest.removeIf([&](const ManifestEntry &m) { return Database::pathKey(m.relPath) == Database::pathKey(rel); });
            };
            switch (a.kind) {
            case Kind::Remove:
                removeEntry(a.oldRelPath);
                break;
            case Kind::Forget:
                in.manifest.removeIf([&](const ManifestEntry &m) { return m.relPath == a.oldRelPath; });
                break;
            case Kind::Add:
            case Kind::Update:
            case Kind::Move: {
                if (!a.oldRelPath.isEmpty())
                    removeEntry(a.oldRelPath);
                const qint64 size = a.kind == Kind::Move ? 1234 : a.bytes;
                ManifestEntry m;
                m.deviceId = in.deviceId;
                m.relPath = a.destRelPath;
                m.kind = a.itemKind;
                m.sourcePath = a.track.path;
                m.sourceSize = a.sourceSize;
                m.sourceMtimeMs = a.sourceMtimeMs;
                m.outputSig = a.outputSig;
                m.destSize = size;
                m.destMtimeMs = kTime;
                in.manifest << m;
                addDeviceFile(a.destRelPath, size);
                break;
            }
            }
        }
    }
};

QStringList destPaths(const SyncPlan &plan, Kind kind)
{
    QStringList out;
    for (const SyncAction &a : plan.actions) {
        if (a.kind == kind)
            out << (a.destRelPath.isEmpty() ? a.oldRelPath : a.destRelPath);
    }
    return out;
}

} // namespace

class TestSyncPlanner : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void newTracksAreCopied()
    {
        World w(flash());
        w.select({track(1, QStringLiteral("Radiohead"), QStringLiteral("OK Computer"), QStringLiteral("Airbag"), 1),
                  track(2, QStringLiteral("Radiohead"), QStringLiteral("OK Computer"), QStringLiteral("Paranoid Android"), 2)});
        const SyncPlan plan = SyncPlanner::diff(w.in);
        QVERIFY(plan.isValid());
        QCOMPARE(plan.adds, 2);
        QCOMPARE(destPaths(plan, Kind::Add),
                 QStringList({QStringLiteral("Music/Radiohead/OK Computer/01 Airbag.flac"),
                              QStringLiteral("Music/Radiohead/OK Computer/02 Paranoid Android.flac")}));
        QVERIFY(!plan.actions[0].prepare); // a plain copy
        QCOMPARE(plan.bytesToWrite, qint64(5'000'001 + 5'000'002));
        QCOMPARE(plan.freeAfter, 1'000'000'000 - plan.bytesToWrite);
        QVERIFY(!plan.insufficientSpace);
    }

    void secondPlanIsEmpty()
    {
        World w(flash());
        w.select({track(1, QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"), 1)});
        w.apply(SyncPlanner::diff(w.in));
        const SyncPlan again = SyncPlanner::diff(w.in);
        QVERIFY(!again.hasWork());
        QCOMPARE(again.unchanged, 1);
        QCOMPARE(again.untracked, 0);
    }

    void deviceTimesAllowForFat()
    {
        QVERIFY(SyncPlanner::sameDeviceTime(kTime, kTime + 1999));
        QVERIFY(SyncPlanner::sameDeviceTime(kTime, kTime - 2000));
        QVERIFY(!SyncPlanner::sameDeviceTime(kTime, kTime + 5000));
        // A daylight-saving change shifts every FAT time by an hour.
        QVERIFY(SyncPlanner::sameDeviceTime(kTime, kTime + 3'600'000 + 1500));
        QVERIFY(SyncPlanner::sameDeviceTime(kTime, kTime - 3'600'000));
        QVERIFY(SyncPlanner::sameDeviceTime(kTime, kTime + 19'800'000)); // UTC+5:30
        QVERIFY(!SyncPlanner::sameDeviceTime(kTime, kTime + 600'000));

        World w(flash());
        w.select({track(1, QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"), 1)});
        w.apply(SyncPlanner::diff(w.in));
        const QString rel = w.in.manifest[0].relPath;
        const qint64 size = w.in.manifest[0].destSize;
        w.addDeviceFile(rel, size, kTime + 3'600'000);
        QVERIFY(!SyncPlanner::diff(w.in).hasWork());
        // Changed by someone else: put back.
        w.addDeviceFile(rel, size + 10);
        const SyncPlan plan = SyncPlanner::diff(w.in);
        QCOMPARE(plan.updates, 1);
        QCOMPARE(plan.actions[0].reason, Reason::ChangedOnDevice);
        QCOMPARE(plan.actions[0].oldBytes, size + 10);
    }

    void editedSourceIsUpdated()
    {
        World w(flash());
        const TrackRow t = track(1, QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"), 1);
        w.select({t});
        w.apply(SyncPlanner::diff(w.in));
        w.in.sources[t.path].mtimeMs += 60'000; // tags edited
        const SyncPlan plan = SyncPlanner::diff(w.in);
        QCOMPARE(plan.updates, 1);
        QCOMPARE(plan.actions[0].reason, Reason::SourceChanged);
        QCOMPARE(plan.actions[0].destRelPath, plan.actions[0].oldRelPath);
    }

    void profileChangeTranscodes()
    {
        World w(flash());
        const TrackRow flac = track(1, QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"), 1);
        const TrackRow mp3 = track(2, QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("D"), 2, QStringLiteral("MP3"));
        w.select({flac, mp3});
        w.apply(SyncPlanner::diff(w.in));
        w.in.profile = dap(); // MP3 only: the FLAC becomes an MP3, the MP3 stays a copy
        const SyncPlan plan = SyncPlanner::diff(w.in);
        QCOMPARE(plan.updates, 1);
        QCOMPARE(plan.unchanged, 1);
        const SyncAction &a = plan.actions[0];
        QCOMPARE(a.reason, Reason::ProfileChanged);
        QVERIFY(a.transcode);
        QVERIFY(a.prepare);
        QVERIFY(!a.cacheKey.isEmpty());
        QCOMPARE(a.codec, QStringLiteral("MP3"));
        QCOMPARE(a.oldRelPath, QStringLiteral("Music/A/B/01 C.flac"));
        QCOMPARE(a.destRelPath, QStringLiteral("Music/A/B/01 C.mp3"));
        QCOMPARE(a.bytes, SyncPlanner::estimateMp3Bytes(180'000, TranscodeTarget::Mp3V0));
        // An exact size once it's in the transcode cache.
        w.in.cachedSize = [](const QString &, const QString &) { return qint64(4321); };
        QCOMPARE(SyncPlanner::diff(w.in).actions[0].bytes, qint64(4321));
        // 320 kbit/s is another output: another cache key.
        PlannerInput cbr = w.in;
        cbr.profile.transcodeTarget = TranscodeTarget::Mp3Cbr320;
        QVERIFY(SyncPlanner::diff(cbr).actions[0].cacheKey != a.cacheKey);
    }

    void artPolicyPreparesCopies()
    {
        World w(flash());
        TrackRow t = track(1, QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"), 1);
        t.hasEmbeddedArt = true;
        w.select({t});
        w.in.profile.artworkPolicy = ArtworkPolicy::Shrink500;
        SyncPlan plan = SyncPlanner::diff(w.in);
        QVERIFY(plan.actions[0].prepare);
        QVERIFY(!plan.actions[0].transcode);
        QCOMPARE(plan.actions[0].destRelPath, QStringLiteral("Music/A/B/01 C.flac"));
        w.apply(plan);
        QVERIFY(!SyncPlanner::diff(w.in).hasWork());
        // New folder art changes the output.
        w.in.sources[t.path].folderArt = QStringLiteral("/lib/cover.jpg");
        w.in.sources[t.path].folderArtSig = QStringLiteral("100:1");
        plan = SyncPlanner::diff(w.in);
        QCOMPARE(plan.updates, 1);
        QCOMPARE(plan.actions[0].artPath, QStringLiteral("/lib/cover.jpg"));
        // No art and nothing to strip: a plain copy.
        World bare(flash());
        bare.in.profile.artworkPolicy = ArtworkPolicy::Strip;
        bare.select({track(2, QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("D"), 2)});
        QVERIFY(!SyncPlanner::diff(bare.in).actions[0].prepare);
    }

    void layoutChangeMoves()
    {
        World w(flash());
        w.select({track(1, QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"), 1)});
        w.apply(SyncPlanner::diff(w.in));
        w.in.profile.pathLayout = QStringLiteral("{artist} - {title}");
        const SyncPlan plan = SyncPlanner::diff(w.in);
        QCOMPARE(plan.moves, 1);
        QCOMPARE(plan.actions[0].kind, Kind::Move);
        QCOMPARE(plan.actions[0].oldRelPath, QStringLiteral("Music/A/B/01 C.flac"));
        QCOMPARE(plan.actions[0].destRelPath, QStringLiteral("Music/A - C.flac"));
        QCOMPARE(plan.bytesToWrite, qint64(0));
    }

    void removalsOnlyTouchOurUnchangedFiles()
    {
        World w(flash());
        const TrackRow a = track(1, QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("One"), 1);
        const TrackRow b = track(2, QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("Two"), 2);
        const TrackRow c = track(3, QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("Three"), 3);
        w.select({a, b, c});
        w.apply(SyncPlanner::diff(w.in));
        w.addDeviceFile(QStringLiteral("Music/A/B/my notes.txt"), 10);   // the user's
        w.in.scan.files.remove(Database::pathKey(QStringLiteral("Music/A/B/02 Two.flac"))); // deleted by hand
        w.addDeviceFile(QStringLiteral("Music/A/B/03 Three.flac"), 99);  // replaced by the user
        w.select({});
        const SyncPlan plan = SyncPlanner::diff(w.in);
        QCOMPARE(plan.removes, 1);
        QCOMPARE(destPaths(plan, Kind::Remove), QStringList{QStringLiteral("Music/A/B/01 One.flac")});
        QCOMPARE(destPaths(plan, Kind::Forget).size(), 2);
        QCOMPARE(plan.untracked, 1);
        QCOMPARE(plan.warnings.size(), 1);
        QVERIFY(plan.warnings[0].contains(QStringLiteral("03 Three.flac")));
        for (const SyncAction &x : plan.actions)
            QVERIFY(!x.oldRelPath.endsWith(QStringLiteral(".txt")));
    }

    void missingSourceKeepsTheDeviceCopy()
    {
        World w(flash());
        const TrackRow t = track(1, QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"), 1);
        w.select({t});
        w.apply(SyncPlanner::diff(w.in));
        w.in.sources[t.path].exists = false; // library drive unplugged
        const SyncPlan plan = SyncPlanner::diff(w.in);
        QVERIFY(!plan.hasWork());
        QCOMPARE(plan.warnings.size(), 1);
    }

    void namesNeverClobberUserFiles()
    {
        World w(flash());
        w.addDeviceFile(QStringLiteral("Music/A/B/01 C.flac"), 1); // the user's, same name
        w.select({track(1, QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"), 1)});
        const SyncPlan plan = SyncPlanner::diff(w.in);
        QCOMPARE(destPaths(plan, Kind::Add), QStringList{QStringLiteral("Music/A/B/01 C (2).flac")});
        QCOMPARE(plan.untracked, 1);
    }

    void caseInsensitiveCollisionsAreStable()
    {
        World w(flash());
        const TrackRow upper = track(1, QStringLiteral("ABBA"), QStringLiteral("Gold"), QStringLiteral("SOS"), 1);
        const TrackRow lower = track(2, QStringLiteral("Abba"), QStringLiteral("Gold"), QStringLiteral("SOS"), 1);
        w.select({upper, lower});
        SyncPlan plan = SyncPlanner::diff(w.in);
        QCOMPARE(destPaths(plan, Kind::Add),
                 QStringList({QStringLiteral("Music/ABBA/Gold/01 SOS.flac"), QStringLiteral("Music/Abba/Gold/01 SOS (2).flac")}));
        w.apply(plan);
        QVERIFY(!SyncPlanner::diff(w.in).hasWork());
        // Order doesn't matter: the same names next time.
        w.select({lower, upper});
        QVERIFY(!SyncPlanner::diff(w.in).hasWork());
        // When the first goes, the second keeps its name (no shuffling).
        w.select({lower});
        plan = SyncPlanner::diff(w.in);
        QCOMPARE(plan.removes, 1);
        QCOMPARE(plan.moves, 0);
    }

    void playlistsAreWrittenWithRelativePaths()
    {
        World w(flash());
        const TrackRow a = track(1, QStringLiteral("AC/DC"), QStringLiteral("Back in Black"), QStringLiteral("Hells Bells"), 1);
        const TrackRow b = track(2, QStringLiteral("Björk"), QStringLiteral("Post"), QStringLiteral("Army of Me"), 1);
        w.select({a, b});
        w.in.selection.playlists = {ResolvedPlaylist{7, QStringLiteral("Road: Trip?"), {b, a, b}}};
        SyncPlan plan = SyncPlanner::diff(w.in);
        QCOMPARE(plan.playlistWrites, 1);
        const SyncAction &p = plan.actions.last();
        QCOMPARE(p.itemKind, ManifestEntry::Kind::Playlist);
        QCOMPARE(p.destRelPath, QStringLiteral("Music/Playlists/Road_ Trip_.m3u8"));
        const QString text = QString::fromUtf8(p.playlistData);
        QCOMPARE(text,
                 QStringLiteral("#EXTM3U\n"
                                "#EXTINF:180,Björk - Army of Me\n../Björk/Post/01 Army of Me.flac\n"
                                "#EXTINF:180,AC/DC - Hells Bells\n../AC_DC/Back in Black/01 Hells Bells.flac\n"
                                "#EXTINF:180,Björk - Army of Me\n../Björk/Post/01 Army of Me.flac\n"));
        w.apply(plan);
        QVERIFY(!SyncPlanner::diff(w.in).hasWork());
        // Reordered: rewritten, tracks untouched.
        w.in.selection.playlists[0].tracks = {a, b};
        plan = SyncPlanner::diff(w.in);
        QCOMPARE(plan.actions.size(), 1);
        QCOMPARE(plan.actions[0].kind, Kind::Update);
        QCOMPARE(plan.actions[0].reason, Reason::PlaylistChanged);
        w.apply(plan);
        // Deselected: removed like any file we wrote.
        w.in.selection.playlists.clear();
        plan = SyncPlanner::diff(w.in);
        QCOMPARE(destPaths(plan, Kind::Remove), QStringList{QStringLiteral("Music/Playlists/Road_ Trip_.m3u8")});
    }

    void spaceCheck()
    {
        World w(flash());
        w.select({track(1, QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"), 1)});
        w.in.freeBytes = 5'000'001 + SyncPlanner::kSpaceMarginBytes - 1;
        QVERIFY(SyncPlanner::diff(w.in).insufficientSpace);
        w.in.freeBytes = 5'000'001 + SyncPlanner::kSpaceMarginBytes;
        QVERIFY(!SyncPlanner::diff(w.in).insufficientSpace);
        w.in.freeBytes = -1; // unknown: no check
        QVERIFY(!SyncPlanner::diff(w.in).insufficientSpace);
    }

    void spaceLowPointMidSync()
    {
        // An update that grows runs before updates that shrink: the low point is mid-sync.
        World w(flash());
        const TrackRow a = track(1, QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("One"), 1);
        const TrackRow b = track(2, QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("Two"), 2);
        const TrackRow c = track(3, QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("Three"), 3);
        w.select({a, b, c});
        w.apply(SyncPlanner::diff(w.in));
        constexpr qint64 MiB = 1024 * 1024;
        w.in.sources[a.path].size = 70 * MiB;  // grows from ~5 MB
        w.in.sources[b.path].size = 1 * MiB;
        w.in.sources[c.path].size = 1 * MiB;
        for (ManifestEntry &m : w.in.manifest) {
            if (m.sourcePath != a.path) {
                m.destSize = 30 * MiB;
                w.addDeviceFile(m.relPath, 30 * MiB);
            }
        }
        w.in.freeBytes = 70 * MiB;
        const SyncPlan plan = SyncPlanner::diff(w.in);
        QCOMPARE(plan.updates, 3);
        QVERIFY(plan.freeAfter > SyncPlanner::kSpaceMarginBytes); // fine at the end...
        QVERIFY(plan.insufficientSpace);                         // ...but not after the first
    }

    void longSuffixedNamesStay()
    {
        World w(flash());
        const QString longTitle(230, QLatin1Char('t'));
        w.addDeviceFile(QStringLiteral("Music/A/B/01 ") + longTitle.left(200), 1); // anything taking the name
        const TrackRow t = track(1, QStringLiteral("A"), QStringLiteral("B"), longTitle, 1);
        w.select({t});
        SyncPlan plan = SyncPlanner::diff(w.in);
        const QString first = plan.actions[0].destRelPath;
        w.in.scan.files.clear();
        w.apply(plan);
        // The user's file returns at the plain name: ours keeps its name, no move.
        const QString plain = PathLayout::render(t, w.in.profile);
        if (Database::pathKey(plain) != Database::pathKey(first)) {
            w.addDeviceFile(plain, 1);
            QVERIFY(!SyncPlanner::diff(w.in).hasWork());
        }
        QVERIFY(first.size() <= 240);
    }

    void suffixKeepsPathsShort()
    {
        QCOMPARE(SyncPlanner::withSuffix(QStringLiteral("Music/A/x.mp3"), 2), QStringLiteral("Music/A/x (2).mp3"));
        QCOMPARE(SyncPlanner::withSuffix(QStringLiteral("noext"), 3), QStringLiteral("noext (3)"));
        const QString longPath = QStringLiteral("Music/") + QString(200, QLatin1Char('a')) + QStringLiteral(".flac");
        const QString s = SyncPlanner::withSuffix(longPath, 12, 100);
        QVERIFY2(s.size() <= 100, qPrintable(s));
        QVERIFY(s.endsWith(QStringLiteral(" (12).flac")));
    }

    void playlistPath()
    {
        DeviceProfile p = flash();
        QCOMPARE(SyncPlanner::playlistPath(p, QStringLiteral("Mix")), QStringLiteral("Music/Playlists/Mix.m3u8"));
        p.musicDir.clear();
        QCOMPARE(SyncPlanner::playlistPath(p, QStringLiteral("a/b")), QStringLiteral("Playlists/a_b.m3u8"));
    }
};

QTEST_GUILESS_MAIN(TestSyncPlanner)
#include "tst_syncplanner.moc"

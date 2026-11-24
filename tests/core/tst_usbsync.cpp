// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <sys/stat.h>

#include "database.h"
#include "devicescanner.h"
#include "syncjob.h"
#include "syncplanner.h"
#include "tagreader.h"
#include "testmedia.h"
#include "transcodecache.h"
#include "usbsync.h"

using namespace mmm;

namespace {

ino_t inodeOf(const QString &path)
{
    struct stat st {};
    return ::stat(QFile::encodeName(path).constData(), &st) == 0 ? st.st_ino : 0;
}

QStringList filesUnder(const QString &dir)
{
    QStringList out;
    QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext())
        out << QDir(dir).relativeFilePath(it.next());
    out.sort();
    return out;
}

QString readText(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

} // namespace

// The whole sync on a temp dir standing in for a mounted stick: plan, run, re-plan.
class TestUsbSync : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    Database m_db;
    QString m_a, m_b, m_c; // library files: two FLACs, one MP3
    qint64 m_playlist = 0;
    QString m_mount;
    Device m_device;
    DeviceProfile m_profile;
    std::unique_ptr<TranscodeCache> m_cache;

    QString lib(const QString &name) const { return m_dir.filePath(QStringLiteral("lib/") + name); }
    QString dev(const QString &rel) const { return QDir(m_mount).filePath(rel); }

    bool addTrack(const QString &path, const QString &encoder, const QString &tags)
    {
        if (!test::generateTone(path, encoder, 400, tags).isEmpty())
            return false;
        const auto t = TagReader::read(path);
        const QFileInfo fi(path);
        return t && m_db.upsertTrack(path, fi.lastModified().toMSecsSinceEpoch(), fi.size(), *t);
    }

    SyncPlan plan() { return SyncPlanner::plan(m_db, m_device, m_profile, m_cache.get()); }
    SyncResult run(const SyncPlan &p, const UsbSync::ProgressFn &progress = {})
    {
        std::atomic_bool cancel{false};
        return UsbSync::run(p, m_db, *m_cache, cancel, progress, 2);
    }
    // Plans and runs; the plan must be valid and the run clean.
    SyncResult sync()
    {
        const SyncPlan p = plan();
        if (!p.isValid())
            qWarning() << "plan failed:" << p.error;
        return run(p);
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(QDir().mkpath(lib(QString())));
        QVERIFY(m_db.open(m_dir.filePath(QStringLiteral("library.db"))));
        QVERIFY(addTrack(lib(QStringLiteral("a.flac")), QStringLiteral("flacenc"),
                         QStringLiteral("title=\"Hells Bells\",artist=\"AC/DC\",album=\"Back in Black\",track-number=(uint)1")));
        QVERIFY(addTrack(lib(QStringLiteral("b.flac")), QStringLiteral("flacenc"),
                         QStringLiteral("title=\"Shoot to Thrill\",artist=\"AC/DC\",album=\"Back in Black\",track-number=(uint)2")));
        QVERIFY(addTrack(lib(QStringLiteral("c.mp3")), QStringLiteral("lamemp3enc ! id3v2mux"),
                         QStringLiteral("title=\"Army of Me\",artist=\"Björk\",album=\"Post\",track-number=(uint)1")));
        m_a = lib(QStringLiteral("a.flac"));
        m_b = lib(QStringLiteral("b.flac"));
        m_c = lib(QStringLiteral("c.mp3"));
        m_playlist = m_db.createPlaylist(QStringLiteral("Road Trip"), {m_a, m_c}).value_or(0);
        QVERIFY(m_playlist > 0);

        m_mount = m_dir.filePath(QStringLiteral("stick"));
        QVERIFY(QDir().mkpath(m_mount));
        m_device.id = QStringLiteral("uuid:TEST");
        m_device.objectPath = QStringLiteral("/org/freedesktop/UDisks2/block_devices/sdz1");
        m_device.name = QStringLiteral("STICK");
        m_device.mountPoint = m_mount;
        m_device.fsType = QStringLiteral("vfat");
        m_profile = *DeviceProfiles::builtIn(QStringLiteral("usb-flash"));
        m_profile.deviceId = m_device.id;
        m_profile.name = QStringLiteral("Stick");
        m_cache = std::make_unique<TranscodeCache>(m_dir.filePath(QStringLiteral("cache")));

        // Files of the user's that a sync must never touch.
        QFile notes(dev(QStringLiteral("Music/notes.txt")));
        QVERIFY(QDir().mkpath(dev(QStringLiteral("Music"))));
        QVERIFY(notes.open(QIODevice::WriteOnly));
        notes.write("mine");
        notes.close();
    }

    void selectionAndManifestTables()
    {
        const auto id = m_db.addSyncItem(SyncItem{0, m_device.id, SyncItem::Kind::Playlist, m_playlist, QStringLiteral("Road Trip")});
        QVERIFY(id);
        // Adding it again is fine and returns the same entry.
        QCOMPARE(m_db.addSyncItem(SyncItem{0, m_device.id, SyncItem::Kind::Playlist, m_playlist, QStringLiteral("Road Trip")}), id);
        const QList<SyncItem> items = m_db.syncItems(m_device.id);
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].kind, SyncItem::Kind::Playlist);
        QCOMPARE(items[0].name, QStringLiteral("Road Trip"));
        QVERIFY(m_db.syncItems(QStringLiteral("uuid:OTHER")).isEmpty());

        ManifestEntry e;
        e.deviceId = QStringLiteral("uuid:OTHER");
        e.relPath = QStringLiteral("Music/X/y.mp3");
        e.sourcePath = QStringLiteral("/lib/y.flac");
        QVERIFY(m_db.putManifestEntry(e));
        // Case-insensitive paths: the same file on FAT.
        e.relPath = QStringLiteral("music/x/Y.MP3");
        e.destSize = 5;
        QVERIFY(m_db.putManifestEntry(e));
        QCOMPARE(m_db.manifest(e.deviceId).size(), 1);
        QCOMPARE(m_db.manifest(e.deviceId)[0].destSize, qint64(5));
        // A move replaces the old row.
        e.relPath = QStringLiteral("Music/Z.mp3");
        QVERIFY(m_db.putManifestEntry(e, QStringLiteral("Music/X/y.mp3")));
        QCOMPARE(m_db.manifest(e.deviceId).size(), 1);
        QCOMPARE(m_db.manifest(e.deviceId)[0].relPath, QStringLiteral("Music/Z.mp3"));
        QVERIFY(m_db.removeManifestEntry(e.deviceId, QStringLiteral("MUSIC/z.mp3")));
        QVERIFY(m_db.manifest(e.deviceId).isEmpty());
    }

    // M11b: single tracks as sync items, added in batches.
    void singleTracksAndBatchAdd()
    {
        const QString device = QStringLiteral("uuid:PICKS");
        const LibraryQuery query(m_db);
        const QList<TrackRow> picked = query.tracksByPaths({m_c, m_a});
        QCOMPARE(picked.size(), 2);
        const qint64 post = [&] {
            for (const AlbumRow &a : query.albums())
                if (a.title == QStringLiteral("Post"))
                    return a.id;
            return qint64(0);
        }();
        const QList<SyncItem> batch{
            SyncItem{0, device, SyncItem::Kind::Track, picked[0].id, QStringLiteral("Army of Me")},
            SyncItem{0, device, SyncItem::Kind::Track, picked[1].id, QStringLiteral("Hells Bells")},
        };
        QCOMPARE(m_db.addSyncItems(device, batch), std::optional<int>(2));
        // Again, plus one new entry: only that one is new; the others keep their place.
        QCOMPARE(m_db.addSyncItems(device, batch + QList<SyncItem>{SyncItem{0, device, SyncItem::Kind::Album, post, QStringLiteral("Post")}}),
                 std::optional<int>(1));
        QCOMPARE(m_db.addSyncItems(device, {}), std::optional<int>(0));
        QVERIFY(!m_db.addSyncItems(QString(), batch));
        QList<SyncItem> items = m_db.syncItems(device);
        QCOMPARE(items.size(), 3);
        QCOMPARE(items[0].kind, SyncItem::Kind::Track);
        QCOMPARE(items[0].targetId, picked[0].id);
        QCOMPARE(items[2].kind, SyncItem::Kind::Album);
        QCOMPARE(SyncItem::kindFrom(QStringLiteral("track")), std::optional<SyncItem::Kind>(SyncItem::Kind::Track));

        // Resolved in order; Post's only track is already there (deduped by path); a track
        // that left the library is "missing".
        items << SyncItem{99, device, SyncItem::Kind::Track, 999999, QStringLiteral("Gone")};
        const ResolvedSelection r = SyncPlanner::resolve(query, items);
        QCOMPARE(r.tracks.size(), 2);
        QCOMPARE(r.tracks[0].path, m_c);
        QCOMPARE(r.tracks[1].path, m_a);
        QCOMPARE(r.missing, QStringList{QStringLiteral("Gone")});
        QCOMPARE(r.trackCounts.value(items[0].id), 1);
        QCOMPARE(r.trackCounts.value(items[2].id), 1);
        QCOMPARE(r.trackCounts.value(99), 0);
        for (const SyncItem &i : m_db.syncItems(device))
            QVERIFY(m_db.removeSyncItem(i.id));
    }

    void notMountedOrReadOnly()
    {
        Device unmounted = m_device;
        unmounted.mountPoint.clear();
        QVERIFY(!SyncPlanner::plan(m_db, unmounted, m_profile, nullptr).isValid());
        Device readOnly = m_device;
        readOnly.readOnly = true;
        QVERIFY(!SyncPlanner::plan(m_db, readOnly, m_profile, nullptr).isValid());
        Device gone = m_device;
        gone.mountPoint = m_dir.filePath(QStringLiteral("nowhere"));
        SyncPlan p = SyncPlanner::plan(m_db, gone, m_profile, nullptr);
        QVERIFY(!p.isValid());
        std::atomic_bool cancel{false};
        p.mountPoint = gone.mountPoint;
        QVERIFY(!UsbSync::run(p, m_db, *m_cache, cancel).error.isEmpty());
    }

    // The M9 user test: sync a playlist, change it, sync again: only the changes copy.
    void playlistSyncThenOnlyChanges()
    {
        SyncPlan p = plan();
        QVERIFY2(p.isValid(), qPrintable(p.error));
        QCOMPARE(p.adds, 2);
        QCOMPARE(p.playlistWrites, 1);
        QCOMPARE(p.untracked, 1); // notes.txt
        QList<SyncProgress> progress;
        SyncResult r = run(p, [&](const SyncProgress &s) { progress << s; });
        QVERIFY2(r.ok, qPrintable(r.error + r.warnings.join(QLatin1Char('\n'))));
        QCOMPARE(r.added, 2);
        QCOMPARE(r.playlistsWritten, 1);
        QVERIFY(!progress.isEmpty());
        QCOMPARE(progress.last().filesDone, 3);
        QCOMPARE(progress.last().filesTotal, 3);

        const QString hells = QStringLiteral("Music/AC_DC/Back in Black/01 Hells Bells.flac");
        const QString army = QStringLiteral("Music/Björk/Post/01 Army of Me.mp3");
        const QString list = QStringLiteral("Music/Playlists/Road Trip.m3u8");
        QCOMPARE(filesUnder(m_mount), QStringList({hells, army, QStringLiteral("Music/Playlists/Road Trip.m3u8"),
                                                   QStringLiteral("Music/notes.txt")}));
        QCOMPARE(QFileInfo(dev(hells)).size(), QFileInfo(m_a).size()); // a plain copy
        QVERIFY(readText(dev(list)).contains(QStringLiteral("\n../AC_DC/Back in Black/01 Hells Bells.flac\n")));
        QCOMPARE(m_db.manifest(m_device.id).size(), 3);

        // Nothing changed: nothing to do.
        p = plan();
        QVERIFY(!p.hasWork());
        QCOMPARE(p.unchanged, 2);

        // Change the playlist: + Shoot to Thrill, − Army of Me.
        const ino_t hellsInode = inodeOf(dev(hells));
        const qint64 hellsMtime = QFileInfo(dev(hells)).lastModified().toMSecsSinceEpoch();
        QVERIFY(m_db.setPlaylistPaths(m_playlist, {m_a, m_b}));
        p = plan();
        QCOMPARE(p.adds, 1);
        QCOMPARE(p.removes, 1);
        QCOMPARE(p.updates, 0);
        QCOMPARE(p.playlistWrites, 1);
        QCOMPARE(p.unchanged, 1);
        r = run(p);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.added, 1);
        QCOMPARE(r.removed, 1);
        QCOMPARE(inodeOf(dev(hells)), hellsInode); // untouched
        QCOMPARE(QFileInfo(dev(hells)).lastModified().toMSecsSinceEpoch(), hellsMtime);
        QVERIFY(!QFileInfo::exists(dev(army)));
        QVERIFY(!QFileInfo::exists(dev(QStringLiteral("Music/Björk")))); // empty folders go
        QVERIFY(QFileInfo::exists(dev(QStringLiteral("Music/AC_DC/Back in Black/02 Shoot to Thrill.flac"))));
        QVERIFY(readText(dev(list)).contains(QStringLiteral("02 Shoot to Thrill.flac")));
        QVERIFY(!readText(dev(list)).contains(QStringLiteral("Army of Me")));
        QVERIFY(QFileInfo::exists(dev(QStringLiteral("Music/notes.txt"))));
        QVERIFY(!plan().hasWork());
    }

    void editedTagsUpdateOnlyThatTrack()
    {
        const QDateTime later = QFileInfo(m_b).lastModified().addSecs(30);
        QFile f(m_b);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(later, QFileDevice::FileModificationTime));
        f.close();
        const SyncPlan p = plan();
        QCOMPARE(p.updates, 1);
        QCOMPARE(p.adds + p.removes + p.moves, 0);
        QVERIFY(sync().ok);
        QVERIFY(!plan().hasWork());
    }

    void layoutChangeMovesInPlace()
    {
        const QString before = dev(QStringLiteral("Music/AC_DC/Back in Black/01 Hells Bells.flac"));
        const ino_t inode = inodeOf(before);
        m_profile.pathLayout = QStringLiteral("{artist}/{track:02} - {title}");
        SyncPlan p = plan();
        QCOMPARE(p.moves, 2);
        QCOMPARE(p.adds + p.updates, 0);
        QCOMPARE(p.playlistWrites, 1); // its paths changed
        const SyncResult r = run(p);
        QVERIFY2(r.ok, qPrintable(r.error + r.warnings.join(QLatin1Char('\n'))));
        QCOMPARE(r.moved, 2);
        const QString after = dev(QStringLiteral("Music/AC_DC/01 - Hells Bells.flac"));
        QCOMPARE(inodeOf(after), inode);
        QVERIFY(!QFileInfo::exists(dev(QStringLiteral("Music/AC_DC/Back in Black"))));
        QVERIFY(!plan().hasWork());
    }

    void dapProfileTranscodesThroughTheCache()
    {
        m_profile = *DeviceProfiles::builtIn(QStringLiteral("generic-dap"));
        m_profile.deviceId = m_device.id;
        m_profile.name = QStringLiteral("DAP");
        SyncPlan p = plan();
        QCOMPARE(p.updates, 2); // FLAC → MP3
        QVERIFY(p.actions.size() >= 2);
        SyncResult r = run(p);
        QVERIFY2(r.ok, qPrintable(r.error + r.warnings.join(QLatin1Char('\n'))));
        const QString hells = dev(QStringLiteral("Music/AC_DC/Back in Black/01 Hells Bells.mp3"));
        const auto tags = TagReader::read(hells);
        QVERIFY(tags);
        QCOMPARE(tags->codec, QStringLiteral("MP3"));
        QCOMPARE(tags->title, QStringLiteral("Hells Bells"));
        QVERIFY(!QFileInfo::exists(dev(QStringLiteral("Music/AC_DC/Back in Black/01 Hells Bells.flac"))));
        QVERIFY(readText(dev(QStringLiteral("Music/Playlists/Road Trip.m3u8"))).contains(QStringLiteral("01 Hells Bells.mp3")));
        QVERIFY(!plan().hasWork());

        // Deleted on the device: copied again from the cache (no new transcode).
        const QStringList cached = filesUnder(m_cache->dir());
        QCOMPARE(cached.size(), 2);
        QVERIFY(QFile::remove(hells));
        p = plan();
        QCOMPARE(p.adds, 1);
        QCOMPARE(p.actions[0].reason, SyncAction::Reason::MissingOnDevice);
        QCOMPARE(p.actions[0].bytes, m_cache->size(p.actions[0].cacheKey, QStringLiteral("mp3"))); // exact, from the cache
        r = run(p);
        QVERIFY(r.ok);
        QCOMPARE(filesUnder(m_cache->dir()), cached);
        QVERIFY(QFileInfo::exists(hells));
    }

    void cancelKeepsWhatWasDone()
    {
        // Deselect everything, then select the album again: three files to write.
        for (const SyncItem &i : m_db.syncItems(m_device.id))
            QVERIFY(m_db.removeSyncItem(i.id));
        QVERIFY(sync().ok);
        QCOMPARE(filesUnder(m_mount), QStringList{QStringLiteral("Music/notes.txt")});
        const qint64 album = [&] {
            for (const AlbumRow &a : LibraryQuery(m_db).albums())
                if (a.title == QStringLiteral("Back in Black"))
                    return a.id;
            return qint64(0);
        }();
        QVERIFY(m_db.addSyncItem(SyncItem{0, m_device.id, SyncItem::Kind::Album, album, QStringLiteral("Back in Black")}));
        QVERIFY(m_db.addSyncItem(SyncItem{0, m_device.id, SyncItem::Kind::Playlist, m_playlist, QStringLiteral("Road Trip")}));

        const SyncPlan p = plan();
        QCOMPARE(p.adds, 2);
        std::atomic_bool cancel{false};
        const SyncResult r = UsbSync::run(p, m_db, *m_cache, cancel, [&](const SyncProgress &s) {
            if (s.filesDone >= 1)
                cancel = true;
        }, 1);
        QVERIFY(r.canceled);
        QVERIFY(!r.ok);
        for (const QString &f : filesUnder(m_mount))
            QVERIFY2(!QFileInfo(f).fileName().startsWith(QStringLiteral(".mmm-part-")), qPrintable(f));
        QCOMPARE(m_db.manifest(m_device.id).size(), 1); // the file done before the cancel
        const SyncPlan rest = plan();
        QCOMPARE(rest.adds, 1);
        QCOMPARE(rest.playlistWrites, 1);
        QVERIFY(run(rest).ok);
        QVERIFY(!plan().hasWork());
    }

    void staleTempFilesAreSwept()
    {
        QFile stale(dev(QStringLiteral("Music/AC_DC/.mmm-part-dead")));
        QVERIFY(stale.open(QIODevice::WriteOnly));
        stale.write("half a file");
        stale.close();
        const SyncPlan p = plan();
        QCOMPARE(p.staleTempFiles, QStringList{QStringLiteral("Music/AC_DC/.mmm-part-dead")});
        QVERIFY(p.hasWork());
        QVERIFY(run(p).ok);
        QVERIFY(!stale.exists());
        QVERIFY(!plan().hasWork());
    }

    // Renames that depend on each other: a swap, a chain listed in the "wrong" order, and two
    // updates trading paths. Hand-made plan on a device of its own.
    void reordersSwapsAndChains()
    {
        const QString mount = m_dir.filePath(QStringLiteral("swap"));
        const QString deviceId = QStringLiteral("uuid:SWAP");
        auto write = [&](const QString &rel, const QByteArray &data) {
            QDir().mkpath(QFileInfo(QDir(mount).filePath(rel)).path());
            QFile f(QDir(mount).filePath(rel));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(data);
            f.close();
            ManifestEntry e;
            e.deviceId = deviceId;
            e.relPath = rel;
            e.sourcePath = QStringLiteral("src:") + QString::fromLatin1(data);
            e.destSize = data.size();
            QVERIFY(m_db.putManifestEntry(e));
        };
        write(QStringLiteral("Music/A.flac"), "a");
        write(QStringLiteral("Music/B.flac"), "b");
        write(QStringLiteral("Music/C.flac"), "c");
        write(QStringLiteral("Music/D.flac"), "d");
        write(QStringLiteral("Music/L1.m3u8"), "list one");
        write(QStringLiteral("Music/L2.m3u8"), "list two");
        auto move = [](const QString &from, const QString &to, const char *source) {
            SyncAction a;
            a.kind = SyncAction::Kind::Move;
            a.oldRelPath = from;
            a.destRelPath = to;
            a.track.path = QStringLiteral("src:") + QLatin1String(source);
            return a;
        };
        auto rewrite = [](const QString &from, const QString &to, const QByteArray &data) {
            SyncAction a;
            a.kind = SyncAction::Kind::Update;
            a.itemKind = ManifestEntry::Kind::Playlist;
            a.oldRelPath = from;
            a.destRelPath = to;
            a.playlistData = data;
            a.bytes = data.size();
            a.track.path = QStringLiteral("src:") + QString::fromLatin1(data);
            return a;
        };
        SyncPlan p;
        p.deviceId = deviceId;
        p.mountPoint = mount;
        p.profile = m_profile;
        p.actions = {move(QStringLiteral("Music/A.flac"), QStringLiteral("Music/B.flac"), "a"),
                     move(QStringLiteral("Music/B.flac"), QStringLiteral("Music/A.flac"), "b"),
                     move(QStringLiteral("Music/C.flac"), QStringLiteral("Music/D.flac"), "c"),
                     move(QStringLiteral("Music/D.flac"), QStringLiteral("Music/E/D.flac"), "d"),
                     rewrite(QStringLiteral("Music/L1.m3u8"), QStringLiteral("Music/L2.m3u8"), "new one"),
                     rewrite(QStringLiteral("Music/L2.m3u8"), QStringLiteral("Music/L1.m3u8"), "new two")};
        std::atomic_bool cancel{false};
        const SyncResult r = UsbSync::run(p, m_db, *m_cache, cancel);
        QVERIFY2(r.ok && r.failed == 0, qPrintable(r.error + r.warnings.join(QLatin1Char('\n'))));
        QCOMPARE(r.moved, 4);
        QCOMPARE(r.playlistsWritten, 2);
        QCOMPARE(readText(QDir(mount).filePath(QStringLiteral("Music/B.flac"))), QStringLiteral("a"));
        QCOMPARE(readText(QDir(mount).filePath(QStringLiteral("Music/A.flac"))), QStringLiteral("b"));
        QCOMPARE(readText(QDir(mount).filePath(QStringLiteral("Music/D.flac"))), QStringLiteral("c"));
        QCOMPARE(readText(QDir(mount).filePath(QStringLiteral("Music/E/D.flac"))), QStringLiteral("d"));
        QCOMPARE(readText(QDir(mount).filePath(QStringLiteral("Music/L2.m3u8"))), QStringLiteral("new one"));
        QCOMPARE(readText(QDir(mount).filePath(QStringLiteral("Music/L1.m3u8"))), QStringLiteral("new two"));
        QCOMPARE(filesUnder(mount).size(), 6); // nothing parked left behind
        QHash<QString, QString> rows; // path → source
        for (const ManifestEntry &e : m_db.manifest(deviceId))
            rows.insert(e.relPath, e.sourcePath);
        QCOMPARE(rows.size(), 6);
        QCOMPARE(rows.value(QStringLiteral("Music/B.flac")), QStringLiteral("src:a"));
        QCOMPARE(rows.value(QStringLiteral("Music/A.flac")), QStringLiteral("src:b"));
        QCOMPARE(rows.value(QStringLiteral("Music/D.flac")), QStringLiteral("src:c"));
        QCOMPARE(rows.value(QStringLiteral("Music/E/D.flac")), QStringLiteral("src:d"));
        QCOMPARE(rows.value(QStringLiteral("Music/L2.m3u8")), QStringLiteral("src:new one"));
        QCOMPARE(rows.value(QStringLiteral("Music/L1.m3u8")), QStringLiteral("src:new two"));
    }

    void neverOverwritesUserSymlinks()
    {
        const QString link = dev(QStringLiteral("Music/AC_DC/Back in Black/03 Hells Bells.flac"));
        QVERIFY(QFile::link(QStringLiteral("/nonexistent/target"), link)); // broken: still someone's
        const DeviceScan scan = DeviceScanner::scan(m_mount, QStringLiteral("Music"));
        QVERIFY(scan.files.contains(Database::pathKey(QStringLiteral("Music/AC_DC/Back in Black/03 Hells Bells.flac"))));
        QVERIFY(QFile::remove(link));
    }

    void jobRunsOffThread()
    {
        QVERIFY(m_db.setPlaylistPaths(m_playlist, {m_c}));
        SyncJob job({m_db.connection().databaseName(), m_cache->dir(), 0, 2});
        QSignalSpy planned(&job, &SyncJob::planReady);
        QSignalSpy progress(&job, &SyncJob::progressChanged);
        QSignalSpy finished(&job, &SyncJob::finished);
        QVERIFY(job.plan(m_device, m_profile));
        QVERIFY(job.isBusy());
        QVERIFY(!job.run(SyncPlan())); // one thing at a time
        QVERIFY(planned.wait(10000));
        QVERIFY(!job.isBusy());
        const auto p = planned.at(0).at(0).value<SyncPlan>();
        QVERIFY2(p.isValid(), qPrintable(p.error));
        QCOMPARE(p.adds, 1);
        QVERIFY(job.run(p));
        QVERIFY(finished.wait(20000));
        const auto r = finished.at(0).at(0).value<SyncResult>();
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.added, 1);
        QVERIFY(!progress.isEmpty());
        QVERIFY(QFileInfo::exists(dev(QStringLiteral("Music/Björk/Post/01 Army of Me.mp3"))));
    }
};

QTEST_GUILESS_MAIN(TestUsbSync)
#include "tst_usbsync.moc"

// SPDX-License-Identifier: GPL-3.0-or-later
#include "libraryscanner.h"

#include "database.h"
#include "tagreader.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QHash>
#include <QLoggingCategory>
#include <QPointer>
#include <QSet>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent>

#include <atomic>
#include <functional>

Q_LOGGING_CATEGORY(lcScan, "mmm.scanner")

namespace mmm {

namespace {

constexpr int kChunkSize = 256;          // files per tag-read batch / DB transaction
constexpr int kMaxFailedPaths = 100;
constexpr qint64 kProgressIntervalMs = 100;

struct Root
{
    qint64 folderId = 0;
    QString folderPath; // the library folder
    QString scanDir;    // what to walk: the folder itself or a directory inside it
};

struct ScanJob
{
    QList<Root> roots;
    bool full = false;
};

struct ScanResult
{
    ScanStats stats;
    QHash<QString, QStringList> directories; // scanDir → every directory found below it (incl. itself)
    QString error;
};

struct PendingRead
{
    TrackFile file;
    bool isNew = false;
};

struct ReadResult
{
    PendingRead item;
    std::optional<TrackTags> tags;
    QString error;
};

using CancelFlag = std::shared_ptr<std::atomic<bool>>;
using ProgressFn = std::function<void(int processed, int total)>;

// Folder-art file names, best first. Matched on the lower-cased base name.
int coverRank(const QString &fileName)
{
    const qsizetype dot = fileName.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0)
        return 0;
    const QString ext = fileName.mid(dot + 1).toLower();
    if (ext != QLatin1String("jpg") && ext != QLatin1String("jpeg") && ext != QLatin1String("png"))
        return 0;
    static const QStringList names = {QStringLiteral("cover"), QStringLiteral("folder"),
                                      QStringLiteral("front"), QStringLiteral("album")};
    const qsizetype i = names.indexOf(fileName.left(dot).toLower());
    return i < 0 ? 0 : static_cast<int>(names.size() - i);
}

ReadResult readOne(const PendingRead &item)
{
    ReadResult r;
    r.item = item;
    r.tags = TagReader::read(item.file.path, &r.error);
    return r;
}

// Runs on a pool thread. Owns its own Database connection; touches nothing else shared.
ScanResult runScan(const QString &dbPath, const ScanJob &job, const CancelFlag &cancel,
                   QThreadPool *readPool, const ProgressFn &progress)
{
    QElapsedTimer timer;
    timer.start();
    ScanResult result;
    ScanStats &st = result.stats;
    st.fullScan = job.full;
    auto cancelled = [&] { return cancel->load(std::memory_order_relaxed); };

    Database db;
    if (!db.open(dbPath, &result.error))
        return result;

    // --- 1. Walk the folders and compare against the DB (stat only, no file reads) ---
    QList<PendingRead> toRead;
    QStringList toRemove;
    QHash<QString, QPair<int, QString>> covers; // dir → (rank, path)

    for (const Root &root : job.roots) {
        if (cancelled())
            break;
        const QFileInfo rootInfo(root.scanDir);
        if (!rootInfo.isDir()) {
            if (root.scanDir == root.folderPath) {
                // Whole library folder missing (unmounted?). Keep its tracks.
                st.unavailableFolders << root.folderPath;
                continue;
            }
            // A subdirectory that vanished: walking finds nothing, so its tracks are removed.
        }

        const QHash<QString, FileState> known = db.fileStatesUnder(root.scanDir);
        QSet<QString> seen;
        seen.reserve(known.size());
        QStringList dirs;
        if (rootInfo.isDir())
            dirs << root.scanDir;

        QDirIterator it(root.scanDir, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        int entries = 0;
        bool interrupted = false;
        while (it.hasNext()) {
            if ((++entries % 256) == 0 && cancelled()) {
                interrupted = true;
                break;
            }
            it.next();
            const QFileInfo fi = it.fileInfo();
            if (fi.isDir()) {
                dirs << fi.filePath();
                continue;
            }
            const QString name = fi.fileName();
            if (LibraryScanner::isAudioFile(name)) {
                ++st.filesSeen;
                const QString path = fi.filePath();
                const FileState state{fi.lastModified().toMSecsSinceEpoch(), fi.size()};
                seen.insert(path);
                const auto k = known.constFind(path);
                if (k == known.cend())
                    toRead << PendingRead{TrackFile{path, state.mtimeMs, state.size, root.folderId}, true};
                else if (*k != state)
                    toRead << PendingRead{TrackFile{path, state.mtimeMs, state.size, root.folderId}, false};
                else
                    ++st.unchanged;
            } else if (const int rank = coverRank(name)) {
                auto &best = covers[fi.path()];
                if (rank > best.first)
                    best = {rank, fi.filePath()};
            }
        }
        // Never remove tracks based on an incomplete walk.
        if (interrupted)
            break;
        for (auto k = known.cbegin(); k != known.cend(); ++k) {
            if (!seen.contains(k.key()))
                toRemove << k.key();
        }
        result.directories.insert(root.scanDir, dirs);
    }

    // --- 2. Remove tracks whose files are gone ---
    if (!cancelled() && !toRemove.isEmpty()) {
        db.connection().transaction();
        for (const QString &path : std::as_const(toRemove)) {
            if (db.removeTrack(path))
                ++st.removed;
        }
        db.connection().commit();
    }

    // --- 3. Read tags in parallel, write each batch in one transaction ---
    const int total = static_cast<int>(toRead.size());
    int processed = 0;
    QElapsedTimer sinceProgress;
    sinceProgress.start();
    if (!cancelled())
        progress(0, total);

    for (qsizetype i = 0; i < toRead.size() && !cancelled(); i += kChunkSize) {
        const QList<PendingRead> chunk = toRead.mid(i, kChunkSize);
        const QList<ReadResult> results =
            QtConcurrent::blockingMapped<QList<ReadResult>>(readPool, chunk, readOne);

        db.connection().transaction();
        for (const ReadResult &r : results) {
            QString error = r.error;
            if (r.tags && db.upsertTrack(r.item.file, *r.tags, &error)) {
                ++(r.item.isNew ? st.added : st.updated);
                continue;
            }
            // Unreadable: leave any existing DB entry alone (its old mtime makes us retry next scan).
            ++st.failed;
            if (st.failedPaths.size() < kMaxFailedPaths)
                st.failedPaths << r.item.file.path + QStringLiteral(": ") + error;
            qCWarning(lcScan) << "cannot read" << r.item.file.path << ":" << error;
        }
        db.connection().commit();

        processed += static_cast<int>(chunk.size());
        if (processed == total || sinceProgress.elapsed() >= kProgressIntervalMs) {
            progress(processed, total);
            sinceProgress.restart();
        }
    }

    // --- 4. Folder art and cleanup ---
    if (!cancelled()) {
        db.connection().transaction();
        for (auto c = covers.cbegin(); c != covers.cend(); ++c)
            db.setFolderCover(c.key(), c.value().second);
        if (job.full)
            db.clearMissingCovers();
        db.connection().commit();
    }
    db.pruneOrphans(); // even after cancel: removals above may have orphaned rows

    st.cancelled = cancelled();
    st.elapsedMs = timer.elapsed();
    return result;
}

} // namespace

// ---------------------------------------------------------------------------

struct LibraryScanner::Private
{
    explicit Private(LibraryScanner *q_, QString path) : q(q_), dbPath(std::move(path)) {}

    LibraryScanner *q;
    QString dbPath;
    Database db; // main-thread connection: folder management only

    QThreadPool driverPool; // runs the scan itself (1 thread)
    QThreadPool readPool;   // parallel tag reading
    QFutureWatcher<ScanResult> futureWatcher;
    CancelFlag cancelFlag;
    bool scanning = false;

    bool pendingFull = false;
    QStringList pendingDirs;

    bool watching = false;
    QFileSystemWatcher fsWatcher;
    QSet<QString> knownDirs; // every directory seen by scans so far
    QSet<QString> changedDirs;
    QTimer debounce;
    QTimer fallbackRescan;
    bool fallbackWarned = false;

    ScanJob makeJob(bool full, const QStringList &dirs) const;
    void start(const ScanJob &job);
    void onFinished();
    void startPending();
    void updateKnownDirs(const ScanResult &result);
    void syncWatches();
};

ScanJob LibraryScanner::Private::makeJob(bool full, const QStringList &dirs) const
{
    ScanJob job;
    job.full = full;
    const QList<LibraryFolder> folders = db.libraryFolders();
    if (full) {
        for (const LibraryFolder &f : folders)
            job.roots << Root{f.id, f.path, f.path};
        return job;
    }

    // Drop directories that are inside another requested directory.
    QStringList sorted;
    for (const QString &dir : dirs)
        sorted << QDir::cleanPath(dir);
    sorted.sort();
    sorted.removeDuplicates();
    QStringList collapsed;
    for (const QString &dir : std::as_const(sorted)) {
        if (collapsed.isEmpty() || !(dir == collapsed.last() || dir.startsWith(collapsed.last() + QLatin1Char('/'))))
            collapsed << dir;
    }

    for (const QString &dir : std::as_const(collapsed)) {
        const LibraryFolder *owner = nullptr;
        for (const LibraryFolder &f : folders) {
            if (dir == f.path || dir.startsWith(f.path + QLatin1Char('/'))) {
                owner = &f;
                break;
            }
        }
        if (owner)
            job.roots << Root{owner->id, owner->path, dir};
        else
            qCDebug(lcScan) << "ignoring scan request outside the library:" << dir;
    }
    return job;
}

void LibraryScanner::Private::start(const ScanJob &job)
{
    if (job.roots.isEmpty()) {
        // Nothing to do; still report so callers waiting on scanFinished don't hang.
        ScanStats stats;
        stats.fullScan = job.full;
        Q_EMIT q->scanStarted();
        Q_EMIT q->scanFinished(stats);
        startPending();
        return;
    }

    cancelFlag = std::make_shared<std::atomic<bool>>(false);
    const bool wasScanning = scanning;
    scanning = true;
    if (!wasScanning)
        Q_EMIT q->scanningChanged(true);
    Q_EMIT q->scanStarted();

    QPointer<LibraryScanner> guard(q);
    ProgressFn progress = [guard](int processed, int total) {
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, processed, total] {
                if (guard)
                    Q_EMIT guard->progress(processed, total);
            },
            Qt::QueuedConnection);
    };
    futureWatcher.setFuture(QtConcurrent::run(&driverPool, &runScan, dbPath, job, cancelFlag, &readPool,
                                              std::move(progress)));
}

void LibraryScanner::Private::onFinished()
{
    const ScanResult result = futureWatcher.result();
    updateKnownDirs(result);
    if (watching)
        syncWatches();

    if (!result.error.isEmpty())
        Q_EMIT q->errorOccurred(result.error);
    if (!result.stats.unavailableFolders.isEmpty()) {
        Q_EMIT q->errorOccurred(QStringLiteral("Library folder not available (skipped): %1")
                                    .arg(result.stats.unavailableFolders.join(QStringLiteral(", "))));
    }

    const bool more = pendingFull || !pendingDirs.isEmpty();
    if (!more) {
        scanning = false;
        Q_EMIT q->scanFinished(result.stats);
        Q_EMIT q->scanningChanged(false);
        return;
    }
    Q_EMIT q->scanFinished(result.stats);
    startPending();
}

void LibraryScanner::Private::startPending()
{
    if (!pendingFull && pendingDirs.isEmpty()) {
        if (scanning) {
            scanning = false;
            Q_EMIT q->scanningChanged(false);
        }
        return;
    }
    const bool full = pendingFull;
    const QStringList dirs = pendingDirs;
    pendingFull = false;
    pendingDirs.clear();
    start(makeJob(full, dirs));
}

void LibraryScanner::Private::updateKnownDirs(const ScanResult &result)
{
    for (auto it = result.directories.cbegin(); it != result.directories.cend(); ++it) {
        const QString &scanDir = it.key();
        const QString prefix = scanDir + QLatin1Char('/');
        for (auto k = knownDirs.begin(); k != knownDirs.end();) {
            if (*k == scanDir || k->startsWith(prefix))
                k = knownDirs.erase(k);
            else
                ++k;
        }
        for (const QString &dir : it.value())
            knownDirs.insert(dir);
    }
}

void LibraryScanner::Private::syncWatches()
{
    const QStringList current = fsWatcher.directories();
    const QSet<QString> watched(current.cbegin(), current.cend());

    QStringList toRemove;
    for (const QString &dir : watched) {
        if (!knownDirs.contains(dir))
            toRemove << dir;
    }
    if (!toRemove.isEmpty())
        fsWatcher.removePaths(toRemove);

    QStringList toAdd;
    for (const QString &dir : std::as_const(knownDirs)) {
        if (!watched.contains(dir))
            toAdd << dir;
    }
    if (toAdd.isEmpty())
        return;

    QStringList failed = fsWatcher.addPaths(toAdd);
    // Directories deleted since the scan legitimately fail; anything else means we ran out
    // of inotify watches (fs.inotify.max_user_watches).
    failed.removeIf([](const QString &dir) { return !QFileInfo(dir).isDir(); });
    if (!failed.isEmpty()) {
        if (!fallbackRescan.isActive())
            fallbackRescan.start();
        if (!fallbackWarned) {
            fallbackWarned = true;
            const QString msg = QStringLiteral("Could not watch %1 of %2 folders for changes (inotify limit?). "
                                               "Rescanning the whole library every %3 minutes instead. "
                                               "Raise fs.inotify.max_user_watches to fix.")
                                    .arg(failed.size())
                                    .arg(knownDirs.size())
                                    .arg(fallbackRescan.interval() / 60000);
            qCWarning(lcScan).noquote() << msg;
            Q_EMIT q->errorOccurred(msg);
        }
    }
}

// ---------------------------------------------------------------------------

LibraryScanner::LibraryScanner(const QString &databasePath, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>(this, databasePath))
{
    d->driverPool.setMaxThreadCount(1);
    d->readPool.setMaxThreadCount(std::max(2, QThread::idealThreadCount()));

    connect(&d->futureWatcher, &QFutureWatcher<ScanResult>::finished, this, [this] { d->onFinished(); });

    d->debounce.setSingleShot(true);
    d->debounce.setInterval(2000);
    connect(&d->debounce, &QTimer::timeout, this, [this] {
        const QStringList dirs(d->changedDirs.cbegin(), d->changedDirs.cend());
        d->changedDirs.clear();
        scanPaths(dirs);
    });
    connect(&d->fsWatcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &dir) {
        d->changedDirs.insert(dir);
        d->debounce.start();
    });

    d->fallbackRescan.setInterval(15 * 60 * 1000);
    connect(&d->fallbackRescan, &QTimer::timeout, this, &LibraryScanner::scan);
}

LibraryScanner::~LibraryScanner()
{
    d->debounce.stop();
    d->fallbackRescan.stop();
    if (d->cancelFlag)
        d->cancelFlag->store(true);
    d->futureWatcher.waitForFinished();
    d->futureWatcher.disconnect();
}

bool LibraryScanner::open(QString *error)
{
    const QString dir = QFileInfo(d->dbPath).absolutePath();
    if (d->dbPath != QLatin1String(":memory:") && !QDir().mkpath(dir)) {
        if (error)
            *error = QStringLiteral("cannot create %1").arg(dir);
        return false;
    }
    return d->db.open(d->dbPath, error);
}

QStringList LibraryScanner::folders() const
{
    QStringList out;
    for (const LibraryFolder &f : d->db.libraryFolders())
        out << f.path;
    return out;
}

bool LibraryScanner::addFolder(const QString &path, QString *error)
{
    const QFileInfo fi(path);
    if (!fi.isDir()) {
        if (error)
            *error = QStringLiteral("%1 is not a folder").arg(path);
        return false;
    }
    const QString canonical = fi.canonicalFilePath();
    if (!d->db.addLibraryFolder(canonical, error))
        return false;
    scanPaths({canonical});
    return true;
}

bool LibraryScanner::removeFolder(const QString &path, QString *error)
{
    const QFileInfo fi(path);
    const QString key = fi.exists() ? fi.canonicalFilePath() : QDir::cleanPath(fi.absoluteFilePath());

    // The worker could re-insert tracks for this folder; stop it first.
    const bool wasScanning = d->scanning;
    if (wasScanning) {
        d->cancelFlag->store(true);
        d->futureWatcher.waitForFinished();
    }

    const bool ok = d->db.removeLibraryFolder(key, error);
    if (ok) {
        const QString prefix = key + QLatin1Char('/');
        d->knownDirs.removeIf([&](const QString &dir) { return dir == key || dir.startsWith(prefix); });
        d->pendingDirs.removeIf([&](const QString &dir) { return dir == key || dir.startsWith(prefix); });
        if (d->watching)
            d->syncWatches();
    }
    if (wasScanning)
        scan(); // resume what we interrupted (queued behind the cancelled scan's finish)
    return ok;
}

bool LibraryScanner::isScanning() const { return d->scanning; }

bool LibraryScanner::isWatching() const { return d->watching; }

void LibraryScanner::setWatching(bool watching)
{
    if (d->watching == watching)
        return;
    d->watching = watching;
    if (watching) {
        d->syncWatches();
    } else {
        const QStringList dirs = d->fsWatcher.directories();
        if (!dirs.isEmpty())
            d->fsWatcher.removePaths(dirs);
        d->debounce.stop();
        d->fallbackRescan.stop();
        d->changedDirs.clear();
    }
    Q_EMIT watchingChanged(watching);
}

void LibraryScanner::setWatchDebounceMs(int ms) { d->debounce.setInterval(ms); }

void LibraryScanner::setFallbackRescanMinutes(int minutes)
{
    d->fallbackRescan.setInterval(std::max(1, minutes) * 60 * 1000);
}

int LibraryScanner::watchedDirectoryCount() const
{
    return static_cast<int>(d->fsWatcher.directories().size());
}

bool LibraryScanner::isAudioFile(const QString &fileName)
{
    const qsizetype dot = fileName.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0) // no extension, or a dotfile like ".flac"
        return false;
    static const QSet<QString> exts = {
        QStringLiteral("flac"), QStringLiteral("mp3"), QStringLiteral("wav"),  QStringLiteral("m4a"),
        QStringLiteral("ogg"),  QStringLiteral("oga"), QStringLiteral("opus"), QStringLiteral("aif"),
        QStringLiteral("aiff"),
    };
    return exts.contains(fileName.mid(dot + 1).toLower());
}

void LibraryScanner::scan()
{
    if (d->scanning) {
        d->pendingFull = true;
        return;
    }
    d->start(d->makeJob(true, {}));
}

void LibraryScanner::scanPaths(const QStringList &directories)
{
    if (directories.isEmpty())
        return;
    if (d->scanning) {
        d->pendingDirs << directories;
        return;
    }
    d->start(d->makeJob(false, directories));
}

void LibraryScanner::cancel()
{
    d->pendingFull = false;
    d->pendingDirs.clear();
    if (d->cancelFlag)
        d->cancelFlag->store(true);
}

} // namespace mmm

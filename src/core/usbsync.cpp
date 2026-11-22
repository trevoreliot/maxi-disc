// SPDX-License-Identifier: GPL-3.0-or-later
#include "usbsync.h"

#include "database.h"
#include "devicescanner.h"
#include "pathlayout.h"
#include "transcodecache.h"
#include "transcoder.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QThread>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(lcSync, "mmm.sync")

namespace mmm {

namespace {

using Kind = SyncAction::Kind;

struct Prepared
{
    QString path;  // the cache entry
    QString error;
};

QString errnoText()
{
    return QString::fromLocal8Bit(std::strerror(errno));
}

// The bytes come from `read` (returns bytes read, 0 at the end, -1 on error).
class DeviceWriter
{
public:
    DeviceWriter(const std::atomic_bool &cancel, std::function<void(qint64)> onBytes)
        : m_cancel(cancel), m_onBytes(std::move(onBytes))
    {
    }

    bool canceled() const { return m_canceled; }
    bool deviceError() const { return m_deviceError; }
    const QString &error() const { return m_error; }

    // Writes `destAbs` through a hidden temp file in the same folder + rename. A failure to
    // read the source is a file error; a failure to write is a device error (stop the sync).
    bool write(const QString &destAbs, const std::function<qint64(char *, qint64)> &read)
    {
        m_canceled = m_deviceError = false;
        m_error.clear();
        const QFileInfo dest(destAbs);
        if (!QDir().mkpath(dest.path()))
            return deviceFail(QStringLiteral("cannot create the folder %1").arg(dest.path()));
        const QString temp = QDir(dest.path()).filePath(
            DeviceScanner::tempPrefix() + QString::number(QRandomGenerator::global()->generate(), 16));
        const QByteArray tempName = QFile::encodeName(temp);
        const int fd = ::open(tempName.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
        if (fd < 0)
            return deviceFail(QStringLiteral("cannot write to the device: %1").arg(errnoText()));
        auto abandon = [&] {
            ::close(fd);
            ::unlink(tempName.constData());
            return false;
        };
        QByteArray buffer(1 << 20, Qt::Uninitialized);
        while (true) {
            if (m_cancel.load()) {
                m_canceled = true;
                return abandon();
            }
            const qint64 n = read(buffer.data(), buffer.size());
            if (n < 0) {
                m_error = QStringLiteral("cannot read the source file");
                return abandon();
            }
            if (n == 0)
                break;
            qint64 done = 0;
            while (done < n) {
                const ssize_t w = ::write(fd, buffer.constData() + done, static_cast<size_t>(n - done));
                if (w < 0) {
                    if (errno == EINTR)
                        continue;
                    deviceFail(QStringLiteral("cannot write to the device: %1").arg(errnoText()));
                    return abandon();
                }
                done += w;
            }
            m_onBytes(n);
        }
        if (::fsync(fd) != 0) {
            deviceFail(QStringLiteral("cannot write to the device: %1").arg(errnoText()));
            return abandon();
        }
        if (::close(fd) != 0) {
            ::unlink(tempName.constData());
            return deviceFail(QStringLiteral("cannot write to the device: %1").arg(errnoText()));
        }
        if (::rename(tempName.constData(), QFile::encodeName(destAbs).constData()) != 0) {
            ::unlink(tempName.constData());
            return deviceFail(QStringLiteral("cannot rename on the device: %1").arg(errnoText()));
        }
        return true;
    }

private:
    bool deviceFail(const QString &message)
    {
        m_deviceError = true;
        m_error = message;
        return false;
    }

    const std::atomic_bool &m_cancel;
    std::function<void(qint64)> m_onBytes;
    bool m_canceled = false;
    bool m_deviceError = false;
    QString m_error;
};

ManifestEntry entryFor(const SyncPlan &plan, const SyncAction &a, const QString &relPath, const QFileInfo &written)
{
    ManifestEntry e;
    e.deviceId = plan.deviceId;
    e.relPath = relPath;
    e.kind = a.itemKind;
    e.sourcePath = a.track.path;
    e.sourceSize = a.sourceSize;
    e.sourceMtimeMs = a.sourceMtimeMs;
    e.outputSig = a.outputSig;
    e.destSize = written.size();
    e.destMtimeMs = written.lastModified().toMSecsSinceEpoch();
    e.title = a.track.title;
    e.artist = a.track.artist.isEmpty() ? a.track.albumArtist : a.track.artist;
    e.album = a.track.album;
    e.durationMs = a.track.durationMs;
    e.codec = a.codec;
    return e;
}

} // namespace

SyncResult UsbSync::run(const SyncPlan &plan, Database &db, TranscodeCache &cache, const std::atomic_bool &cancel,
                        const ProgressFn &progress, int prepareThreads)
{
    SyncResult result;
    const QDir mount(plan.mountPoint);
    if (plan.mountPoint.isEmpty() || !mount.exists() || !QFileInfo(plan.mountPoint).isWritable()) {
        result.error = QStringLiteral("The device isn't mounted (or is read-only).");
        return result;
    }
    const QString mountRoot = QDir::cleanPath(mount.absolutePath());
    const QString musicDir = PathLayout::cleanFolder(plan.profile.musicDir);
    const QString stopDir = musicDir.isEmpty() ? mountRoot : QDir::cleanPath(mount.filePath(musicDir));
    const auto key = [](const QString &rel) { return Database::pathKey(rel); };

    // Device paths come from the plan, but never let one escape the mount point.
    auto abs = [&](const QString &rel) -> QString {
        const QString p = QDir::cleanPath(mount.filePath(rel));
        return p.startsWith(mountRoot + QLatin1Char('/')) ? p : QString();
    };
    // Something is at `path` (a broken symlink counts: it's someone's).
    auto occupied = [](const QString &path) {
        const QFileInfo fi(path);
        return fi.exists() || fi.isSymLink();
    };
    // Removes the folders a removed/moved file leaves empty, up to the music folder.
    auto pruneDirs = [&](const QString &fileAbs) {
        QString dir = QFileInfo(fileAbs).path();
        while (dir.startsWith(stopDir + QLatin1Char('/')) && QDir().rmdir(dir))
            dir = QFileInfo(dir).path();
    };

    // --- Progress ---
    SyncProgress p;
    for (const SyncAction &a : plan.actions) {
        if (a.kind != Kind::Forget)
            ++p.filesTotal;
        p.bytesTotal += a.bytes;
    }
    QElapsedTimer sinceReport;
    sinceReport.start();
    auto report = [&](bool force) {
        if (progress && (force || sinceReport.elapsed() >= 100)) {
            progress(p);
            sinceReport.restart();
        }
    };
    report(true);

    // --- Preparation (transcodes, art changes) on a few threads, a little ahead of the copy ---
    std::atomic_bool stopPreparing{false};
    QThreadPool preparePool;
    preparePool.setMaxThreadCount(prepareThreads > 0 ? prepareThreads : std::clamp(QThread::idealThreadCount() - 1, 1, 4));
    QHash<qsizetype, QFuture<Prepared>> prepared;
    auto prepareOne = [&](qsizetype i) {
        const SyncAction &a = plan.actions[i];
        if (prepared.contains(i) || !a.prepare || (a.kind != Kind::Add && a.kind != Kind::Update))
            return;
        Transcoder::Request req;
        req.sourcePath = a.track.path;
        req.transcode = a.transcode;
        req.target = plan.profile.transcodeTarget;
        req.art = plan.profile.artworkPolicy;
        req.artPath = a.artPath;
        const QString cacheKey = a.cacheKey;
        const QString ext = QFileInfo(a.destRelPath).suffix();
        prepared.insert(i, QtConcurrent::run(&preparePool, [&cache, &stopPreparing, req, cacheKey, ext]() mutable {
            Prepared out;
            if (cache.size(cacheKey, ext) >= 0) {
                out.path = cache.path(cacheKey, ext);
                cache.touch(out.path);
                return out;
            }
            req.destPath = cache.tempPath(ext);
            if (!Transcoder::prepare(req, &stopPreparing, &out.error))
                return out;
            out.path = cache.insert(cacheKey, ext, req.destPath, &out.error);
            return out;
        }));
    };
    // Waits for the pool even when leaving early (no transcode keeps running past the sync).
    struct PoolGuard
    {
        std::atomic_bool &stop;
        QThreadPool &pool;
        ~PoolGuard()
        {
            stop = true;
            pool.waitForDone();
        }
    } poolGuard{stopPreparing, preparePool};

    DeviceWriter writer(cancel, [&](qint64 n) {
        p.bytesDone += n;
        result.bytesWritten += n;
        report(false);
    });
    auto finish = [&](const QString &error) {
        result.error = error;
        if (cancel.load())
            result.canceled = true;
        stopPreparing = true;
        p.currentFile.clear();
        p.stage = error.isEmpty() && !result.canceled ? QStringLiteral("Done") : QString();
        report(true);
        result.ok = !result.canceled && result.error.isEmpty();
        return result;
    };
    auto fileFailed = [&](const QString &what) {
        result.warnings << what;
        ++result.failed;
    };

    for (const QString &rel : plan.staleTempFiles) {
        if (const QString path = abs(rel); !path.isEmpty())
            QFile::remove(path);
    }

    // --- 1. Removals (and manifest rows of files that are gone) ---
    QList<qsizetype> rest;
    for (qsizetype i = 0; i < plan.actions.size(); ++i) {
        const SyncAction &a = plan.actions[i];
        if (a.kind != Kind::Remove && a.kind != Kind::Forget) {
            rest << i;
            continue;
        }
        if (cancel.load())
            return finish({});
        p.currentFile = a.oldRelPath;
        if (a.kind == Kind::Remove) {
            p.stage = QStringLiteral("Removing");
            report(true);
            const QString path = abs(a.oldRelPath);
            if (path.isEmpty()) {
                fileFailed(QStringLiteral("Not removed (outside the device): %1").arg(a.oldRelPath));
                continue;
            }
            if (occupied(path) && !QFile::remove(path))
                return finish(QStringLiteral("cannot delete %1 from the device").arg(a.oldRelPath));
            pruneDirs(path);
            ++result.removed;
            ++p.filesDone;
        }
        if (!db.removeManifestEntry(plan.deviceId, a.oldRelPath))
            return finish(QStringLiteral("cannot update the library database"));
    }

    // --- 2. Moves, writes and playlists, in plan order, except that a file goes to a path
    // another file still has to leave only after that one left: a Move's file is needed, so
    // wait for it (swaps: park one under a temp name); an Update's old file is rewritten from
    // the library anyway, so it may be replaced right away.
    QHash<QString, qsizetype> pendingOld; // path key → action still to leave that path
    QHash<qsizetype, QString> from;       // a Move's file now (moved aside to break a swap)
    QSet<QString> written;                // path keys written by this run (no longer the old owner's)
    QSet<qsizetype> oldTaken;             // Updates whose old file another action replaced
    auto relocates = [&](const SyncAction &a) {
        return !a.oldRelPath.isEmpty() && key(a.oldRelPath) != key(a.destRelPath)
            && (a.kind == Kind::Move || a.kind == Kind::Update);
    };
    for (qsizetype i : std::as_const(rest)) {
        const SyncAction &a = plan.actions[i];
        if (relocates(a)) {
            pendingOld.insert(key(a.oldRelPath), i);
            if (a.kind == Kind::Move)
                from.insert(i, abs(a.oldRelPath));
        }
    }

    enum class Outcome { Done, Failed, Deferred, Stop, Canceled };
    QString stopError;
    auto commit = [&](qsizetype i, const QString &to) -> bool {
        const SyncAction &a = plan.actions[i];
        const QFileInfo fi(to);
        if (!fi.exists()) {
            stopError = QStringLiteral("%1 disappeared from the device after writing it").arg(a.destRelPath);
            return false;
        }
        // The old row goes unless someone else wrote that path in this run (then it's theirs).
        const bool replaceOld = !a.oldRelPath.isEmpty() && !written.contains(key(a.oldRelPath)) && !oldTaken.contains(i);
        QString why;
        if (!db.putManifestEntry(entryFor(plan, a, a.destRelPath, fi), replaceOld ? a.oldRelPath : QString(), &why)) {
            stopError = QStringLiteral("cannot update the library database: %1").arg(why);
            return false;
        }
        written.insert(key(a.destRelPath));
        if (pendingOld.value(key(a.oldRelPath), -1) == i)
            pendingOld.remove(key(a.oldRelPath));
        return true;
    };

    auto execute = [&](qsizetype i) -> Outcome {
        const SyncAction &a = plan.actions[i];
        p.currentFile = a.destRelPath;
        const QString to = abs(a.destRelPath);
        if (to.isEmpty()) {
            fileFailed(QStringLiteral("Not written (outside the device): %1").arg(a.destRelPath));
            return Outcome::Failed;
        }
        const bool samePlace = !a.oldRelPath.isEmpty() && key(a.oldRelPath) == key(a.destRelPath);
        if (!samePlace && occupied(to)) {
            const qsizetype owner = pendingOld.value(key(a.destRelPath), -1);
            if (owner < 0 || owner == i) {
                fileFailed(QStringLiteral("Not written (the name is taken): %1").arg(a.destRelPath));
                return Outcome::Failed;
            }
            if (plan.actions[owner].kind == Kind::Move)
                return Outcome::Deferred;
            oldTaken.insert(owner); // an Update's old file: replaced here, rewritten there
            pendingOld.remove(key(a.destRelPath));
        }

        if (a.kind == Kind::Move) {
            p.stage = QStringLiteral("Moving");
            report(true);
            const QString source = from.value(i);
            if (source.isEmpty() || !occupied(source)) {
                // Gone since the plan: the next sync copies it again.
                db.removeManifestEntry(plan.deviceId, a.oldRelPath);
                fileFailed(QStringLiteral("Not moved (no longer on the device): %1").arg(a.oldRelPath));
                return Outcome::Failed;
            }
            if (!QDir().mkpath(QFileInfo(to).path())
                || ::rename(QFile::encodeName(source).constData(), QFile::encodeName(to).constData()) != 0) {
                stopError = QStringLiteral("cannot move %1 on the device: %2").arg(a.oldRelPath, errnoText());
                return Outcome::Stop;
            }
            pruneDirs(source);
            return commit(i, to) ? Outcome::Done : Outcome::Stop;
        }

        bool ok = false;
        if (a.itemKind == ManifestEntry::Kind::Playlist) {
            p.stage = QStringLiteral("Writing playlist");
            report(true);
            qint64 offset = 0;
            ok = writer.write(to, [&](char *data, qint64 max) {
                const qint64 n = std::min<qint64>(max, a.playlistData.size() - offset);
                std::memcpy(data, a.playlistData.constData() + offset, static_cast<size_t>(n));
                offset += n;
                return n;
            });
        } else {
            QString source = a.track.path;
            if (a.prepare) {
                p.stage = a.transcode ? QStringLiteral("Converting") : QStringLiteral("Preparing art");
                report(true);
                prepareOne(i);
                QFuture<Prepared> f = prepared.value(i);
                while (!f.isFinished()) {
                    if (cancel.load())
                        stopPreparing = true;
                    QThread::msleep(20);
                }
                const Prepared r = f.result();
                if (r.path.isEmpty()) {
                    if (cancel.load())
                        return Outcome::Canceled;
                    fileFailed(QStringLiteral("%1: %2").arg(a.track.path, r.error));
                    return Outcome::Failed;
                }
                source = r.path;
            }
            p.stage = QStringLiteral("Copying");
            report(true);
            QFile in(source);
            if (!in.open(QIODevice::ReadOnly)) {
                fileFailed(QStringLiteral("%1: %2").arg(source, in.errorString()));
                return Outcome::Failed;
            }
            ok = writer.write(to, [&in](char *data, qint64 max) { return in.read(data, max); });
        }
        if (!ok) {
            if (writer.canceled())
                return Outcome::Canceled;
            if (writer.deviceError()) {
                stopError = writer.error();
                return Outcome::Stop;
            }
            fileFailed(QStringLiteral("%1: %2").arg(a.track.path, writer.error()));
            return Outcome::Failed;
        }
        // An update to a new path: the old file goes (unless another action took that path).
        if (!a.oldRelPath.isEmpty() && !samePlace && !oldTaken.contains(i) && !written.contains(key(a.oldRelPath))) {
            const QString old = abs(a.oldRelPath);
            if (!old.isEmpty() && occupied(old)) {
                if (QFile::remove(old))
                    pruneDirs(old);
                else
                    oldTaken.insert(i); // keep its manifest row: the next sync removes it
            }
        }
        if (!commit(i, to))
            return Outcome::Stop;
        if (a.itemKind == ManifestEntry::Kind::Playlist)
            ++result.playlistsWritten;
        else if (a.kind == Kind::Add)
            ++result.added;
        else if (a.kind == Kind::Update)
            ++result.updated;
        return Outcome::Done;
    };

    const qsizetype lookahead = preparePool.maxThreadCount() * 2;
    QList<qsizetype> queue = rest;
    while (!queue.isEmpty()) {
        QList<qsizetype> deferred;
        bool progressed = false;
        for (qsizetype n = 0; n < queue.size(); ++n) {
            if (cancel.load())
                return finish({});
            for (qsizetype ahead = n; ahead < std::min(queue.size(), n + lookahead + 1); ++ahead)
                prepareOne(queue[ahead]);
            const qsizetype i = queue[n];
            switch (execute(i)) {
            case Outcome::Done:
                if (plan.actions[i].kind == Kind::Move)
                    ++result.moved;
                progressed = true;
                ++p.filesDone;
                report(false);
                break;
            case Outcome::Failed:
                if (pendingOld.value(key(plan.actions[i].oldRelPath), -1) == i)
                    pendingOld.remove(key(plan.actions[i].oldRelPath));
                progressed = true;
                ++p.filesDone;
                report(false);
                break;
            case Outcome::Deferred:
                deferred << i;
                break;
            case Outcome::Stop:
                return finish(stopError);
            case Outcome::Canceled:
                return finish({});
            }
        }
        if (!deferred.isEmpty() && !progressed) {
            // Every remaining action waits for a Move that waits too (a swap or a cycle):
            // move one of those files aside under a temp name to open the cycle.
            const qsizetype blocked = deferred.first();
            const qsizetype owner = pendingOld.value(key(plan.actions[blocked].destRelPath), -1);
            const QString source = from.value(owner);
            const QString aside = QFileInfo(source).dir().filePath(
                DeviceScanner::tempPrefix() + QString::number(QRandomGenerator::global()->generate(), 16));
            if (owner < 0 || ::rename(QFile::encodeName(source).constData(), QFile::encodeName(aside).constData()) != 0)
                return finish(QStringLiteral("cannot reorder files on the device: %1").arg(errnoText()));
            from[owner] = aside;
            pendingOld.remove(key(plan.actions[owner].oldRelPath));
        }
        queue = deferred;
    }
    return finish({});
}

} // namespace mmm

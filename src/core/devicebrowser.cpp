// SPDX-License-Identifier: GPL-3.0-or-later
#include "devicebrowser.h"

#include "database.h"
#include "devicescanner.h"
#include "libraryquery.h"
#include "libraryscanner.h"
#include "pathlayout.h"
#include "tagreader.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMetaObject>

#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(lcSync)

namespace mmm {

namespace {

const QChar kSep(0x1F);

QString key3(const QString &artist, const QString &album, const QString &title)
{
    return LibraryMatcher::normalize(artist) + kSep + LibraryMatcher::normalize(album) + kSep
        + LibraryMatcher::normalize(title);
}

QString key2(const QString &artist, const QString &title)
{
    return LibraryMatcher::normalize(artist) + kSep + LibraryMatcher::normalize(title);
}

} // namespace

LibraryMatcher::LibraryMatcher(const QList<TrackRow> &library)
{
    for (const TrackRow &t : library) {
        m_paths.insert(t.path);
        if (t.title.trimmed().isEmpty())
            continue;
        const QString albumArtist = t.albumArtist.trimmed().isEmpty() ? t.artist : t.albumArtist;
        // Artist + album + title only when all three are there: an empty album would let a
        // live version match the studio one, with no length check.
        if (!albumArtist.trimmed().isEmpty() && !t.album.trimmed().isEmpty())
            m_albumKeys.insert(key3(albumArtist, t.album, t.title));
        if (!t.artist.trimmed().isEmpty())
            m_titleKeys.insert(key2(t.artist, t.title), t.durationMs);
        if (!albumArtist.trimmed().isEmpty() && LibraryMatcher::normalize(albumArtist) != LibraryMatcher::normalize(t.artist))
            m_titleKeys.insert(key2(albumArtist, t.title), t.durationMs);
    }
}

bool LibraryMatcher::matches(const DeviceTrack &t, const QString &sourcePath) const
{
    if (!sourcePath.isEmpty() && m_paths.contains(sourcePath))
        return true;
    if (t.title.trimmed().isEmpty())
        return false;
    const QString albumArtist = t.albumArtist.trimmed().isEmpty() ? t.artist : t.albumArtist;
    if (!albumArtist.trimmed().isEmpty() && !t.album.trimmed().isEmpty()
        && m_albumKeys.contains(key3(albumArtist, t.album, t.title)))
        return true;
    if (t.durationMs <= 0)
        return false;
    for (const QString &artist : {t.artist, albumArtist}) {
        if (artist.trimmed().isEmpty())
            continue;
        const QList<qint64> durations = m_titleKeys.values(key2(artist, t.title));
        for (qint64 d : durations) {
            if (d > 0 && std::abs(d - t.durationMs) <= 3000)
                return true;
        }
    }
    return false;
}

DeviceBrowser::DeviceBrowser(QString databasePath, QObject *parent)
    : QObject(parent), m_databasePath(std::move(databasePath)), m_cancel(std::make_shared<std::atomic_bool>(false))
{
    m_pool.setMaxThreadCount(1);
    qRegisterMetaType<mmm::DeviceTrack>();
    qRegisterMetaType<QList<mmm::DeviceTrack>>();
    qRegisterMetaType<mmm::DeviceBrowseSummary>();
}

DeviceBrowser::~DeviceBrowser()
{
    *m_cancel = true;
    m_pool.waitForDone();
}

void DeviceBrowser::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    Q_EMIT busyChanged();
}

void DeviceBrowser::cancel()
{
    *m_cancel = true;
    ++m_generation; // whatever is still on its way is dropped
    setBusy(false);
}

void DeviceBrowser::start(const DeviceBrowseRequest &request)
{
    *m_cancel = true; // the previous browse (if any) stops at its next file
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const quint64 generation = ++m_generation;
    setBusy(true);
    m_pool.start([this, generation, request, path = m_databasePath, cancel = m_cancel] {
        // Queued back to this object's thread; dropped there if a newer browse started.
        auto post = [this, generation](auto fn) {
            QMetaObject::invokeMethod(this, [this, generation, fn] {
                if (generation == m_generation)
                    fn();
            }, Qt::QueuedConnection);
        };
        Database db;
        QString error;
        if (!db.open(path, &error)) {
            post([this, error] {
                m_busy = false;
                Q_EMIT failed(QStringLiteral("Cannot open the library: %1").arg(error));
                Q_EMIT busyChanged();
            });
            return;
        }
        const auto summary = browse(
            db, request, cancel.get(),
            [&](int total, const QString &dir, bool fellBack) {
                post([this, total, dir, fellBack] { Q_EMIT discovered(total, dir, fellBack); });
            },
            [&](const QList<DeviceTrack> &batch, int done, int total) {
                post([this, batch, done, total] { Q_EMIT batchReady(batch, done, total); });
            },
            &error);
        // The result first, then "not busy": listeners see a finished browse when busy flips.
        if (!error.isEmpty()) {
            post([this, error] {
                m_busy = false;
                Q_EMIT failed(error);
                Q_EMIT busyChanged();
            });
        } else if (summary) {
            post([this, s = *summary] {
                m_busy = false;
                Q_EMIT finished(s);
                Q_EMIT busyChanged();
            });
        }
    });
}

std::optional<DeviceBrowseSummary> DeviceBrowser::browse(Database &db, const DeviceBrowseRequest &r,
                                                         const std::atomic_bool *cancel, const DiscoveredFn &discovered,
                                                         const BatchFn &batch, QString *error)
{
    auto canceled = [cancel] { return cancel && cancel->load(); };
    DeviceBrowseSummary summary;

    // The profile's music folder, unless there's none, it's missing, or everything was asked for.
    const QString musicDir = PathLayout::cleanFolder(r.musicDir);
    if (!r.wholeDevice && !musicDir.isEmpty()) {
        if (QFileInfo(QDir(r.mountPoint).filePath(musicDir)).isDir())
            summary.scannedDir = musicDir;
        else
            summary.fellBack = true;
    }
    const DeviceScan scan = DeviceScanner::scan(r.mountPoint, summary.scannedDir, {}, cancel);
    if (scan.canceled || canceled())
        return std::nullopt;
    if (!scan.error.isEmpty()) {
        if (error)
            *error = scan.error;
        return std::nullopt;
    }
    QList<DeviceFile> audio;
    for (const DeviceFile &f : scan.files) {
        if (LibraryScanner::isAudioFile(QFileInfo(f.relPath).fileName()))
            audio << f;
    }
    std::sort(audio.begin(), audio.end(), [](const DeviceFile &a, const DeviceFile &b) { return a.relPath < b.relPath; });
    const int total = static_cast<int>(audio.size());
    if (discovered)
        discovered(total, summary.scannedDir, summary.fellBack);

    const QHash<QString, DeviceTrack> cached = db.deviceTracks(r.deviceId);
    QHash<QString, QString> sources; // path key → library source of the files this app wrote
    for (const ManifestEntry &m : db.manifest(r.deviceId)) {
        if (m.kind == ManifestEntry::Kind::Track)
            sources.insert(Database::pathKey(m.relPath), m.sourcePath);
    }
    const LibraryMatcher matcher(LibraryQuery(db).trackIdentities());

    auto finish = [&](DeviceTrack &t) {
        const QString k = Database::pathKey(t.relPath);
        const auto source = sources.constFind(k);
        t.synced = source != sources.cend();
        t.inLibrary = matcher.matches(t, t.synced ? *source : QString());
        ++summary.tracks;
        summary.bytes += t.size;
        summary.notInLibrary += t.inLibrary ? 0 : 1;
        summary.synced += t.synced ? 1 : 0;
    };

    // Cached files first: a device opened before shows up at once.
    QList<DeviceTrack> out;
    QList<DeviceFile> toRead;
    QSet<QString> present;
    int done = 0;
    for (const DeviceFile &f : std::as_const(audio)) {
        const QString k = Database::pathKey(f.relPath);
        present.insert(k);
        const auto c = cached.constFind(k);
        if (c != cached.cend() && c->size == f.size && c->mtimeMs == f.mtimeMs) {
            DeviceTrack t = *c;
            t.relPath = f.relPath;
            finish(t);
            out << t;
            ++summary.cacheHits;
            ++done;
            if (out.size() >= 1000) {
                if (batch)
                    batch(out, done, total);
                out.clear();
            }
        } else {
            toRead << f;
        }
    }
    if (!out.isEmpty() && batch)
        batch(out, done, total);
    out.clear();

    // Then the new and changed ones: read, cache and send a batch every 64 files or 150 ms.
    QList<DeviceTrack> toCache;
    QElapsedTimer sinceFlush;
    sinceFlush.start();
    auto flush = [&] {
        if (!toCache.isEmpty()) {
            QString why;
            if (!db.putDeviceTracks(r.deviceId, toCache, &why))
                qCWarning(lcSync).noquote() << "device tag cache:" << why;
        }
        if (!out.isEmpty() && batch)
            batch(out, done, total);
        out.clear();
        toCache.clear();
        sinceFlush.restart();
    };
    const QDir mount(r.mountPoint);
    for (const DeviceFile &f : std::as_const(toRead)) {
        if (canceled()) {
            flush(); // what was read is cached; the rest next time
            return std::nullopt;
        }
        DeviceTrack t;
        t.relPath = f.relPath;
        t.size = f.size;
        t.mtimeMs = f.mtimeMs;
        if (const auto tags = TagReader::read(mount.filePath(f.relPath))) {
            t.title = tags->title;
            t.artist = tags->artist;
            t.album = tags->album;
            t.albumArtist = tags->albumArtist;
            t.trackNumber = tags->trackNumber;
            t.discNumber = tags->discNumber;
            t.year = tags->year;
            t.durationMs = tags->durationMs;
            t.codec = tags->codec == QLatin1String("Unknown") ? QString() : tags->codec;
            t.bitrateKbps = tags->bitrateKbps;
        }
        ++summary.tagReads;
        toCache << t;
        finish(t);
        out << t;
        ++done;
        if (out.size() >= 64 || sinceFlush.elapsed() >= 150)
            flush();
    }
    flush();
    if (canceled())
        return std::nullopt;

    // Forget files that are gone, but only within what was read (a music-folder browse
    // keeps the rows of files elsewhere on the device), and not when nothing at all was found:
    // that's more likely an empty mount point left by an unmount than an emptied device.
    if (scan.files.isEmpty())
        return summary;
    const QString prefix = summary.scannedDir.isEmpty() ? QString() : Database::pathKey(summary.scannedDir + QLatin1Char('/'));
    QStringList gone;
    for (auto it = cached.cbegin(); it != cached.cend(); ++it) {
        if (!present.contains(it.key()) && it.key().startsWith(prefix))
            gone << it->relPath;
    }
    db.removeDeviceTracks(r.deviceId, gone);
    return summary;
}

} // namespace mmm

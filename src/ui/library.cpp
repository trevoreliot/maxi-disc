// SPDX-License-Identifier: GPL-3.0-or-later
#include "library.h"

#include "coverimageprovider.h"
#include "database.h"
#include "librarymodels.h"
#include "libraryscanner.h"
#include "m3u.h"

#include <QFileInfo>
#include <QImage>

#include <QJSEngine>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcLibrary, "mmm.library")

namespace {
Library *s_instance = nullptr;
}

Library::Library(const Options &options, QObject *parent)
    : QObject(parent)
    , m_databasePath(options.databasePath)
    , m_scanner(std::make_unique<mmm::LibraryScanner>(options.databasePath))
{
    Q_ASSERT_X(!s_instance, "Library", "only one Library may exist");
    s_instance = this;

    // Two readers: one long query (e.g. a big search) doesn't stall the other views.
    m_queryPool.setMaxThreadCount(2);
    m_writePool.setMaxThreadCount(1);

    QString error;
    m_ready = m_scanner->open(&error);
    if (m_ready) {
        m_db = std::make_unique<mmm::Database>();
        m_ready = m_db->open(m_databasePath, &error);
    }
    if (!m_ready) {
        setError(QStringLiteral("Cannot open the library database %1: %2").arg(m_databasePath, error));
        return;
    }

    connect(m_scanner.get(), &mmm::LibraryScanner::scanningChanged, this, &Library::scanningChanged);
    connect(m_scanner.get(), &mmm::LibraryScanner::progress, this, [this](int processed, int total) {
        m_scanProcessed = processed;
        m_scanTotal = total;
        Q_EMIT scanProgressChanged();
    });
    connect(m_scanner.get(), &mmm::LibraryScanner::scanFinished, this, &Library::onScanFinished);
    connect(m_scanner.get(), &mmm::LibraryScanner::errorOccurred, this, &Library::setError);

    refreshCounts();
    m_scanner->setWatching(options.watch);
    if (options.scanOnStart)
        m_scanner->scan();
}

Library::~Library()
{
    m_scanner.reset();           // cancels and joins a running scan
    m_writePool.waitForDone();   // finish a tag edit in progress
    m_queryPool.waitForDone();   // pool threads exit with the pool: thread_local DBs close there
    s_instance = nullptr;
}

Library *Library::instance()
{
    return s_instance;
}

Library *Library::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(s_instance, "Library::create", "construct Library before loading QML");
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

mmm::Database &Library::threadDatabase(const QString &path)
{
    // One connection per pool thread, closed when the thread exits.
    thread_local std::unique_ptr<mmm::Database> db;
    thread_local QString openPath;
    if (!db || openPath != path) {
        db = std::make_unique<mmm::Database>();
        QString error;
        if (!db->open(path, &error))
            qCWarning(lcLibrary) << "query thread cannot open" << path << ":" << error;
        openPath = path;
    }
    return *db;
}

bool Library::isReady() const { return m_ready; }
QString Library::errorString() const { return m_error; }
QString Library::databasePath() const { return m_databasePath; }
QStringList Library::folders() const { return m_ready ? m_scanner->folders() : QStringList(); }
bool Library::isScanning() const { return m_scanner->isScanning(); }
int Library::scanProcessed() const { return m_scanProcessed; }
int Library::scanTotal() const { return m_scanTotal; }
int Library::trackCount() const { return m_trackCount; }
int Library::albumCount() const { return m_albumCount; }

bool Library::addFolder(const QUrl &folder)
{
    QString error;
    const QString path = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
    if (!m_scanner->addFolder(path, &error)) {
        setError(error);
        return false;
    }
    Q_EMIT foldersChanged();
    return true;
}

bool Library::removeFolder(const QString &path)
{
    QString error;
    if (!m_scanner->removeFolder(path, &error)) {
        setError(error);
        return false;
    }
    Q_EMIT foldersChanged();
    refreshCounts();
    Q_EMIT changed();
    return true;
}

void Library::rescan()
{
    m_scanner->scan();
}

void Library::clearError()
{
    if (m_error.isEmpty())
        return;
    m_error.clear();
    Q_EMIT errorStringChanged();
}

void Library::setStatusMessage(const QString &message)
{
    m_statusMessage = message;
    Q_EMIT statusMessageChanged();
}

int Library::createPlaylist(const QString &name, const QStringList &paths)
{
    if (!m_db)
        return 0;
    QString error;
    const auto id = m_db->createPlaylist(name, paths, &error);
    if (!id) {
        setError(QStringLiteral("Cannot create playlist: %1").arg(error));
        return 0;
    }
    Q_EMIT changed();
    return static_cast<int>(*id);
}

bool Library::renamePlaylist(int id, const QString &name)
{
    const bool ok = m_db && m_db->renamePlaylist(id, name);
    if (ok)
        Q_EMIT changed();
    return ok;
}

bool Library::deletePlaylist(int id)
{
    const bool ok = m_db && m_db->deletePlaylist(id);
    if (ok)
        Q_EMIT changed();
    return ok;
}

bool Library::appendToPlaylist(int id, const QStringList &paths)
{
    QString error;
    const bool ok = m_db && m_db->appendToPlaylist(id, paths, &error);
    if (ok)
        Q_EMIT changed();
    else
        setError(QStringLiteral("Cannot add to playlist: %1").arg(error));
    return ok;
}

bool Library::removeFromPlaylist(int id, const QString &path, int occurrence)
{
    if (!m_db || occurrence < 0)
        return false;
    QStringList paths = m_db->playlistPaths(id);
    int seen = 0;
    for (qsizetype i = 0; i < paths.size(); ++i) {
        if (paths[i] == path && seen++ == occurrence) {
            paths.removeAt(i);
            QString error;
            if (!m_db->setPlaylistPaths(id, paths, &error)) {
                setError(QStringLiteral("Cannot change the playlist: %1").arg(error));
                return false;
            }
            Q_EMIT changed();
            return true;
        }
    }
    return false;
}

QList<mmm::TrackRow> Library::itemTracks(const mmm::LibraryQuery &q, const QString &kind, qint64 id)
{
    if (kind == QLatin1String("artist"))
        return q.artistTracks(id);
    if (kind == QLatin1String("genre"))
        return q.genreTracks(id);
    if (kind == QLatin1String("album"))
        return q.albumTracks(id);
    if (kind == QLatin1String("playlist"))
        return q.playlistTracks(id);
    if (kind == QLatin1String("track"))
        return q.tracksByIds({id});
    return {};
}

namespace {

QStringList pathsOf(const QList<mmm::TrackRow> &rows)
{
    QStringList out;
    for (const mmm::TrackRow &t : rows)
        out << t.path;
    return out;
}

QString tracksText(qsizetype n)
{
    return n == 1 ? QStringLiteral("1 track") : QStringLiteral("%1 tracks").arg(n);
}

} // namespace

void Library::addItemToPlaylist(int playlistId, const QString &kind, int id)
{
    runQuery([playlistId, kind, id](const mmm::LibraryQuery &q) {
        QString name;
        for (const mmm::PlaylistRow &p : q.playlists()) {
            if (p.id == playlistId)
                name = p.name;
        }
        return std::pair{pathsOf(itemTracks(q, kind, id)), name};
    }).then(this, [this, playlistId](const std::pair<QStringList, QString> &r) {
        if (r.first.isEmpty())
            return;
        if (appendToPlaylist(playlistId, r.first))
            setStatusMessage(QStringLiteral("Added %1 to “%2”").arg(tracksText(r.first.size()), r.second));
    });
}

void Library::createPlaylistFromItem(const QString &name, const QString &kind, int id)
{
    runQuery([kind, id](const mmm::LibraryQuery &q) { return pathsOf(itemTracks(q, kind, id)); })
        .then(this, [this, name](const QStringList &paths) {
            if (createPlaylist(name, paths) > 0)
                setStatusMessage(QStringLiteral("Made “%1” (%2)").arg(name, tracksText(paths.size())));
        });
}

void Library::requestAlbumInfo(int albumId, const QString &tag)
{
    runQuery([albumId](const mmm::LibraryQuery &q) { return q.album(albumId); })
        .then(this, [this, albumId, tag](const std::optional<mmm::AlbumRow> &album) {
            QVariantMap info;
            if (album) {
                info.insert(QStringLiteral("title"), album->title);
                info.insert(QStringLiteral("artist"), album->artist);
                info.insert(QStringLiteral("year"), album->year);
                info.insert(QStringLiteral("coverUrl"), AlbumListModel::coverUrl(*album));
            }
            Q_EMIT albumInfoReady(albumId, info, tag);
        });
}

void Library::requestItemPaths(const QString &kind, int id, const QString &tag)
{
    runQuery([kind, id](const mmm::LibraryQuery &q) { return pathsOf(itemTracks(q, kind, id)); })
        .then(this, [this, kind, id, tag](const QStringList &paths) { Q_EMIT itemPathsReady(kind, id, paths, tag); });
}

int Library::importPlaylist(const QUrl &file)
{
    return importPlaylists({file});
}

int Library::importPlaylists(const QList<QUrl> &files)
{
    int lastId = 0;
    int imported = 0;
    qsizetype found = 0;
    qsizetype total = 0;
    QStringList failed;
    for (const QUrl &file : files) {
        const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
        QList<mmm::M3u::Entry> entries;
        QString error;
        if (!mmm::M3u::read(path, &entries, &error)) {
            failed << QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), error);
            continue;
        }
        QStringList paths;
        for (const auto &e : std::as_const(entries))
            paths << e.path;
        const int id = createPlaylist(QFileInfo(path).completeBaseName(), paths);
        if (!id) {
            failed << QFileInfo(path).fileName();
            continue;
        }
        lastId = id;
        ++imported;
        total += paths.size();
        found += mmm::LibraryQuery(*m_db).tracksByPaths(paths).size();
    }
    if (!failed.isEmpty())
        setError(QStringLiteral("Couldn't import %1").arg(failed.join(QStringLiteral(", "))));
    if (imported > 0) {
        const QString what = imported == 1 ? QStringLiteral("Imported %1 tracks").arg(found)
                                           : QStringLiteral("Imported %1 playlists, %2 tracks").arg(imported).arg(found);
        setStatusMessage(found == total ? what
                                        : QStringLiteral("%1 (%2 of %3 not in the library)").arg(what).arg(total - found).arg(total));
    }
    return lastId;
}

bool Library::exportPlaylist(int id, const QUrl &file)
{
    if (!m_db)
        return false;
    const QStringList paths = m_db->playlistPaths(id);
    QHash<QString, mmm::TrackRow> known;
    for (const mmm::TrackRow &t : mmm::LibraryQuery(*m_db).tracksByPaths(paths))
        known.insert(t.path, t);

    QList<mmm::M3u::Entry> entries;
    for (const QString &p : paths) {
        mmm::M3u::Entry e{p, {}, -1};
        if (const auto it = known.constFind(p); it != known.cend()) {
            e.title = it->artist.isEmpty() ? it->title : it->artist + QStringLiteral(" - ") + it->title;
            e.durationMs = it->durationMs;
        }
        entries << e;
    }
    const QString out = file.isLocalFile() ? file.toLocalFile() : file.toString();
    QString error;
    if (!mmm::M3u::write(out, entries, false, &error)) {
        setError(QStringLiteral("Cannot write %1: %2").arg(out, error));
        return false;
    }
    setStatusMessage(QStringLiteral("Exported %1 tracks to %2").arg(entries.size()).arg(QFileInfo(out).fileName()));
    return true;
}

QFuture<mmm::TagEditResult> Library::editTags(const QStringList &paths, const mmm::TagChanges &changes)
{
    if (!m_ready) {
        mmm::TagEditResult r;
        for (const QString &p : paths)
            r.failures << p + QStringLiteral(": the library database isn't open");
        return QtFuture::makeReadyValueFuture(r);
    }
    return QtConcurrent::run(&m_writePool,
                             [dbPath = m_databasePath, paths, changes] {
                                 return mmm::editTracks(threadDatabase(dbPath), paths, changes);
                             })
        .then(this, [this, total = paths.size()](const mmm::TagEditResult &r) {
            if (r.failures.isEmpty())
                setStatusMessage(total == 1 ? QStringLiteral("Saved tags")
                                            : QStringLiteral("Saved tags of %1 tracks").arg(total));
            else
                setError(QStringLiteral("Could not save tags of %1 of %2 tracks. %3")
                             .arg(r.failures.size()).arg(total).arg(r.failures.front()));
            if (!r.written.isEmpty()) {
                refreshCounts();
                Q_EMIT tracksEdited(r.tracks);
                Q_EMIT changed();
            }
            return r;
        });
}

QFuture<mmm::ArtworkSaveResult> Library::saveAlbumArt(qint64 albumId, const QByteArray &image)
{
    if (!m_ready) {
        mmm::ArtworkSaveResult r;
        r.error = QStringLiteral("the library database isn't open");
        return QtFuture::makeReadyValueFuture(r);
    }
    return QtConcurrent::run(&m_writePool,
                             [dbPath = m_databasePath, albumId, image] {
                                 // The data must decode: a cut-off download still starts like a JPEG.
                                 if (QImage::fromData(image).isNull()) {
                                     mmm::ArtworkSaveResult bad;
                                     bad.error = QStringLiteral("the downloaded image is damaged or incomplete");
                                     return bad;
                                 }
                                 return mmm::ArtworkSaver::save(threadDatabase(dbPath), albumId, image);
                             })
        .then(this, [this, albumId](const mmm::ArtworkSaveResult &r) {
            if (!r.ok) {
                setError(QStringLiteral("Could not save the cover: %1").arg(r.error));
                return r;
            }
            if (r.warnings.isEmpty())
                setStatusMessage(QStringLiteral("Saved the cover (%1 tracks)").arg(r.edit.written.size()));
            else
                setStatusMessage(r.warnings.size() == 1
                                     ? QStringLiteral("Saved the cover, with a problem: %1").arg(r.warnings.front())
                                     : QStringLiteral("Saved the cover, with %1 problems. First: %2")
                                           .arg(r.warnings.size()).arg(r.warnings.front()));
            // Same file names, new pictures: make the views reload this album's art.
            CoverLoader::bumpRevision(albumId);
            refreshCounts();
            if (!r.edit.tracks.isEmpty())
                Q_EMIT tracksEdited(r.edit.tracks);
            Q_EMIT changed();
            return r;
        });
}

bool Library::setAlbumArtSkipped(qint64 albumId, bool skipped)
{
    const bool ok = m_db && m_db->setAlbumArtSkipped(albumId, skipped);
    if (ok)
        refreshCounts();
    return ok;
}

void Library::setError(const QString &message)
{
    qCWarning(lcLibrary).noquote() << message;
    m_error = message;
    Q_EMIT errorStringChanged();
}

void Library::refreshCounts()
{
    runQuery([](const mmm::LibraryQuery &q) { return std::pair{q.counts(), q.missingArtCounts()}; })
        .then(this, [this](const std::pair<mmm::LibraryCounts, std::pair<int, int>> &r) {
            const auto &[c, art] = r;
            if (c.tracks == m_trackCount && c.albums == m_albumCount && art.first == m_missingArt
                && art.second == m_skippedArt)
                return;
            m_trackCount = c.tracks;
            m_albumCount = c.albums;
            m_missingArt = art.first;
            m_skippedArt = art.second;
            Q_EMIT countsChanged();
        });
}

void Library::onScanFinished(const mmm::ScanStats &stats)
{
    m_scanProcessed = 0;
    m_scanTotal = 0;
    Q_EMIT scanProgressChanged();
    qCInfo(lcLibrary).noquote() << QStringLiteral("scan finished in %1 ms: %2 added, %3 updated, %4 removed, %5 failed")
                                       .arg(stats.elapsedMs)
                                       .arg(stats.added)
                                       .arg(stats.updated)
                                       .arg(stats.removed)
                                       .arg(stats.failed);
    if (stats.added + stats.updated + stats.removed > 0) {
        refreshCounts();
        Q_EMIT changed();
    }
}

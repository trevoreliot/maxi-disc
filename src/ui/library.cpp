// SPDX-License-Identifier: GPL-3.0-or-later
#include "library.h"

#include "database.h"
#include "libraryscanner.h"
#include "m3u.h"

#include <QFileInfo>

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

const mmm::Database &Library::threadDatabase(const QString &path)
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

int Library::importPlaylist(const QUrl &file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    QList<mmm::M3u::Entry> entries;
    QString error;
    if (!mmm::M3u::read(path, &entries, &error)) {
        setError(QStringLiteral("Cannot read %1: %2").arg(path, error));
        return 0;
    }
    QStringList paths;
    for (const auto &e : std::as_const(entries))
        paths << e.path;
    const int id = createPlaylist(QFileInfo(path).completeBaseName(), paths);
    if (id) {
        const qsizetype found = mmm::LibraryQuery(*m_db).tracksByPaths(paths).size();
        setStatusMessage(found == paths.size()
                             ? QStringLiteral("Imported %1 tracks").arg(found)
                             : QStringLiteral("Imported %1 of %2 tracks (%3 not in the library)")
                                   .arg(found).arg(paths.size()).arg(paths.size() - found));
    }
    return id;
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

void Library::setError(const QString &message)
{
    qCWarning(lcLibrary).noquote() << message;
    m_error = message;
    Q_EMIT errorStringChanged();
}

void Library::refreshCounts()
{
    runQuery([](const mmm::LibraryQuery &q) { return q.counts(); }).then(this, [this](mmm::LibraryCounts c) {
        if (c.tracks == m_trackCount && c.albums == m_albumCount)
            return;
        m_trackCount = c.tracks;
        m_albumCount = c.albums;
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

// SPDX-License-Identifier: GPL-3.0-or-later
#include "library.h"

#include "database.h"
#include "libraryscanner.h"

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

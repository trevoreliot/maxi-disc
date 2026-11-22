// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QFuture>
#include <QObject>
#include <QStringList>
#include <QThreadPool>
#include <QUrl>
#include <QtConcurrent>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <type_traits>

#include "artworksaver.h"
#include "libraryquery.h"
#include "tagwriter.h"

class QQmlEngine;
class QJSEngine;

namespace mmm {
class Database;
class LibraryScanner;
struct ScanStats;
} // namespace mmm

// The music library as seen by the UI (QML singleton `Library`).
//
// Owns the LibraryScanner (incremental scan at startup + folder watching) and a small
// thread pool for read queries, so list models never block the UI thread on SQLite.
// Exactly one instance; create it before the QML engine and destroy it after.
class Library : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool ready READ isReady CONSTANT FINAL)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged FINAL)
    Q_PROPERTY(QStringList folders READ folders NOTIFY foldersChanged FINAL)
    Q_PROPERTY(bool scanning READ isScanning NOTIFY scanningChanged FINAL)
    Q_PROPERTY(int scanProcessed READ scanProcessed NOTIFY scanProgressChanged FINAL)
    Q_PROPERTY(int scanTotal READ scanTotal NOTIFY scanProgressChanged FINAL)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY countsChanged FINAL)
    Q_PROPERTY(int albumCount READ albumCount NOTIFY countsChanged FINAL)
    // Albums with no cover art at all (artwork finder), not counting skipped ones; and skipped.
    Q_PROPERTY(int missingArtCount READ missingArtCount NOTIFY countsChanged FINAL)
    Q_PROPERTY(int skippedArtCount READ skippedArtCount NOTIFY countsChanged FINAL)
    // Short informational message for the status area (e.g. "Imported 12 of 14 tracks").
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged FINAL)

public:
    struct Options
    {
        QString databasePath;
        bool scanOnStart = true;
        bool watch = true;
    };

    explicit Library(const Options &options, QObject *parent = nullptr);
    ~Library() override;

    static Library *instance();
    // QML singleton factory: hands QML the existing instance (C++ keeps ownership).
    static Library *create(QQmlEngine *, QJSEngine *);

    bool isReady() const;
    QString errorString() const;
    QString databasePath() const;
    QStringList folders() const;
    bool isScanning() const;
    int scanProcessed() const;
    int scanTotal() const;
    int trackCount() const;
    int albumCount() const;
    int missingArtCount() const { return m_missingArt; }
    int skippedArtCount() const { return m_skippedArt; }
    QString statusMessage() const { return m_statusMessage; }

    Q_INVOKABLE bool addFolder(const QUrl &folder);
    Q_INVOKABLE bool removeFolder(const QString &path);
    Q_INVOKABLE void rescan();
    Q_INVOKABLE void clearError();

    // --- Playlists (UI thread; small writes). Ids are 0 on failure. ---
    Q_INVOKABLE int createPlaylist(const QString &name, const QStringList &paths = {});
    Q_INVOKABLE bool renamePlaylist(int id, const QString &name);
    Q_INVOKABLE bool deletePlaylist(int id);
    Q_INVOKABLE bool appendToPlaylist(int id, const QStringList &paths);
    // Imports an .m3u/.m3u8 as a new playlist named after the file. Entries that aren't in
    // the library are kept (they show up if the files are added later) and reported.
    Q_INVOKABLE int importPlaylist(const QUrl &file);
    // Imports several (the file dialog's multiple selection), each as its own playlist, with
    // one summary message. Returns the id of the last one imported, 0 if none was.
    Q_INVOKABLE int importPlaylists(const QList<QUrl> &files);
    // Writes the playlist as extended M3U with absolute paths.
    Q_INVOKABLE bool exportPlaylist(int id, const QUrl &file);

    // Writes tag changes to the files and updates the library (mmm::editTracks) on a
    // background thread. When done: status message or error, tracksEdited(), changed().
    QFuture<mmm::TagEditResult> editTags(const QStringList &paths, const mmm::TagChanges &changes);

    // Saves album art (mmm::ArtworkSaver: cover file + embedded) on the write thread. When
    // done: status message or error, tracksEdited(), changed().
    QFuture<mmm::ArtworkSaveResult> saveAlbumArt(qint64 albumId, const QByteArray &image);
    // The artwork finder's "skip" mark (UI thread; a small write).
    bool setAlbumArtSkipped(qint64 albumId, bool skipped);

    // Runs fn(const mmm::LibraryQuery &) on the query pool with that thread's own
    // connection. Continue with .then(context, ...) to get the result on a QObject's thread.
    template<typename Fn>
    auto runQuery(Fn fn) -> QFuture<std::invoke_result_t<Fn, const mmm::LibraryQuery &>>
    {
        return QtConcurrent::run(&m_queryPool, [path = m_databasePath, fn = std::move(fn)] {
            return fn(mmm::LibraryQuery(threadDatabase(path)));
        });
    }

Q_SIGNALS:
    // Library contents changed (scan with changes, folder added/removed): reload views.
    void changed();
    // Tracks whose tags were edited, as stored now (Playback refreshes the queue with them).
    void tracksEdited(const QList<mmm::TrackRow> &tracks);
    void errorStringChanged();
    void foldersChanged();
    void scanningChanged();
    void scanProgressChanged();
    void countsChanged();
    void statusMessageChanged();

private:
    static mmm::Database &threadDatabase(const QString &path);
    void setError(const QString &message);
    void setStatusMessage(const QString &message);
    void refreshCounts();
    void onScanFinished(const mmm::ScanStats &stats);

    QString m_databasePath;
    bool m_ready = false;
    QString m_error;
    int m_scanProcessed = 0;
    int m_scanTotal = 0;
    int m_trackCount = 0;
    int m_albumCount = 0;
    int m_missingArt = 0;
    int m_skippedArt = 0;
    QString m_statusMessage;
    QThreadPool m_queryPool;
    QThreadPool m_writePool; // one thread: tag edits run one after another
    std::unique_ptr<mmm::LibraryScanner> m_scanner;
    std::unique_ptr<mmm::Database> m_db; // UI-thread connection for playlist edits
};

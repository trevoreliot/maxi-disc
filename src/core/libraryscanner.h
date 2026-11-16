// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

namespace mmm {

struct ScanStats
{
    int filesSeen = 0;   // audio files found on disk in the scanned folders
    int added = 0;
    int updated = 0;     // mtime or size changed → re-read
    int unchanged = 0;
    int removed = 0;     // in the DB but gone from disk
    int failed = 0;      // couldn't be read (not audio, corrupt, unreadable)
    qint64 elapsedMs = 0;
    bool cancelled = false;
    bool fullScan = false;
    QStringList failedPaths;  // first 100 failures, for display
    QStringList unavailableFolders; // library folders that didn't exist (e.g. unmounted)
};

// Keeps the SQLite library in sync with the library folders.
//
// Scans run on a background thread with their own Database connection; tags are read
// in parallel on a separate pool and written in batches. Only files whose mtime or size
// changed are re-read. Signals are delivered on the scanner's (creating) thread.
//
// Requests made while a scan runs are coalesced and run once it finishes.
// A library folder that's missing entirely (e.g. an unmounted drive) is skipped, never
// treated as "all its tracks were deleted".
class LibraryScanner : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool scanning READ isScanning NOTIFY scanningChanged FINAL)
    Q_PROPERTY(bool watching READ isWatching WRITE setWatching NOTIFY watchingChanged FINAL)

public:
    explicit LibraryScanner(const QString &databasePath, QObject *parent = nullptr);
    ~LibraryScanner() override; // cancels and waits for a running scan

    LibraryScanner(const LibraryScanner &) = delete;
    LibraryScanner &operator=(const LibraryScanner &) = delete;

    // Opens the database (creating / migrating it). Call before anything else.
    bool open(QString *error = nullptr);

    QStringList folders() const;
    // Adds a library folder (canonicalised) and schedules a scan of it.
    bool addFolder(const QString &path, QString *error = nullptr);
    // Removes a library folder and its tracks. Cancels and waits for a running scan.
    bool removeFolder(const QString &path, QString *error = nullptr);

    bool isScanning() const;

    // Folder monitoring: rescans directories shortly after files in them change.
    bool isWatching() const;
    void setWatching(bool watching);
    void setWatchDebounceMs(int ms);     // default 2000
    void setFallbackRescanMinutes(int minutes); // periodic full rescan if inotify watches run out; default 15
    int watchedDirectoryCount() const;

    // Supported audio file extension (case-insensitive)?
    static bool isAudioFile(const QString &fileName);

public Q_SLOTS:
    // Incremental scan of all library folders.
    void scan();
    // Incremental scan limited to these directories (recursive). Each must be inside a
    // library folder; directories that no longer exist have their tracks removed.
    void scanPaths(const QStringList &directories);
    void cancel();

Q_SIGNALS:
    void scanningChanged(bool scanning);
    void watchingChanged(bool watching);
    void scanStarted();
    // `total` = files that need reading; walking the folders reports total = 0.
    void progress(int processed, int total);
    void scanFinished(const mmm::ScanStats &stats);
    void errorOccurred(const QString &message);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace mmm

Q_DECLARE_METATYPE(mmm::ScanStats)

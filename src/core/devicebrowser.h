// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QThreadPool>

#include <atomic>
#include <functional>
#include <memory>

#include "synctypes.h"

namespace mmm {

class Database;

// Is a device file a song the library has? (M9b) 1. a file this app synced, whose source is
// still a library track; 2. the same album artist (or artist) + album + title, ignoring case
// and extra spaces; 3. the same artist + title and a length within 3 s (compilations whose
// album artist differs). Untagged files never match. Built once per browse; pure.
class LibraryMatcher
{
public:
    explicit LibraryMatcher(const QList<TrackRow> &library);
    // `sourcePath`: the manifest's source of this file if the app wrote it ("" if not).
    bool matches(const DeviceTrack &t, const QString &sourcePath = {}) const;

    static QString normalize(const QString &s) { return s.simplified().toCaseFolded(); }

private:
    QSet<QString> m_paths;
    QSet<QString> m_albumKeys;                // artist ␟ album ␟ title
    QMultiHash<QString, qint64> m_titleKeys;  // artist ␟ title → duration
};

// What one browse found.
struct DeviceBrowseSummary
{
    QString scannedDir;     // "" = the whole device
    bool fellBack = false;  // the profile's music folder is missing: read the whole device
    int tracks = 0;
    int notInLibrary = 0;
    int synced = 0;
    int cacheHits = 0;      // tags taken from the cache
    int tagReads = 0;       // files read
    qint64 bytes = 0;
};

struct DeviceBrowseRequest
{
    QString deviceId;
    QString mountPoint;
    QString musicDir;          // the profile's ("" = none: the whole device)
    bool wholeDevice = false;  // read everything even if there's a music folder
};

// The on-device browser's reader (M9b): lists a device's audio files (DeviceScanner) and their
// tags, from the device_file_tags cache when a file's size and mtime are unchanged, else read
// with TagLib and cached. Results come in batches as they're ready; cache rows of files that
// are gone are pruned after a complete browse. Runs on its own worker thread (one browse at
// a time; a new one cancels the last), each with its own Database; signals are queued.
class DeviceBrowser : public QObject
{
    Q_OBJECT

public:
    explicit DeviceBrowser(QString databasePath, QObject *parent = nullptr);
    ~DeviceBrowser() override; // cancels and waits

    // Starts browsing (cancels one that's running). Results: discovered, batches, finished.
    void start(const DeviceBrowseRequest &request);
    void cancel();
    bool isBusy() const { return m_busy; }

    using DiscoveredFn = std::function<void(int total, const QString &dir, bool fellBack)>;
    using BatchFn = std::function<void(const QList<DeviceTrack> &batch, int done, int total)>;
    // The browse itself, on the calling thread (the worker; tests). nullopt if canceled;
    // `error` set if the device can't be read.
    static std::optional<DeviceBrowseSummary> browse(Database &db, const DeviceBrowseRequest &request,
                                                     const std::atomic_bool *cancel, const DiscoveredFn &discovered,
                                                     const BatchFn &batch, QString *error = nullptr);

Q_SIGNALS:
    void busyChanged();
    void discovered(int total, const QString &dir, bool fellBack);
    void batchReady(const QList<mmm::DeviceTrack> &batch, int done, int total);
    void finished(const mmm::DeviceBrowseSummary &summary);
    void failed(const QString &error);

private:
    void setBusy(bool busy);

    QString m_databasePath;
    QThreadPool m_pool;
    std::shared_ptr<std::atomic_bool> m_cancel;
    quint64 m_generation = 0;
    bool m_busy = false;
};

} // namespace mmm

Q_DECLARE_METATYPE(mmm::DeviceBrowseSummary)

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

#include "libraryquery.h"

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

    Q_INVOKABLE bool addFolder(const QUrl &folder);
    Q_INVOKABLE bool removeFolder(const QString &path);
    Q_INVOKABLE void rescan();

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
    void errorStringChanged();
    void foldersChanged();
    void scanningChanged();
    void scanProgressChanged();
    void countsChanged();

private:
    static const mmm::Database &threadDatabase(const QString &path);
    void setError(const QString &message);
    void refreshCounts();
    void onScanFinished(const mmm::ScanStats &stats);

    QString m_databasePath;
    bool m_ready = false;
    QString m_error;
    int m_scanProcessed = 0;
    int m_scanTotal = 0;
    int m_trackCount = 0;
    int m_albumCount = 0;
    QThreadPool m_queryPool;
    std::unique_ptr<mmm::LibraryScanner> m_scanner;
};

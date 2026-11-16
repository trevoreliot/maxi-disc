// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "tracktags.h"

#include <QList>
#include <QSqlDatabase>
#include <QString>

#include <optional>

namespace mmm {

// One SQLite connection to the library database (WAL mode, foreign keys on).
// A Database object must only be used from the thread that opened it; use one
// instance per worker thread. Not copyable.
class Database
{
public:
    Database();
    ~Database();
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    // Opens (creating if needed) and migrates to the latest schema.
    // `path` may be ":memory:". Fails if the file was written by a newer schema.
    bool open(const QString &path, QString *error = nullptr);
    bool isOpen() const;
    void close();

    int schemaVersion() const;
    static int latestSchemaVersion();

    QSqlDatabase connection() const { return m_db; }

    // --- Tracks ---
    // Inserts or updates the track at `path` (matched exactly), creating artist /
    // album / genre rows as needed and updating the search index.
    // Uses a SAVEPOINT, so callers may wrap many calls in one outer transaction.
    std::optional<qint64> upsertTrack(const QString &path, qint64 fileMtime, qint64 fileSize,
                                      const TrackTags &tags, QString *error = nullptr);
    bool removeTrack(const QString &path);
    std::optional<qint64> trackIdByPath(const QString &path) const;

    // Full-text search over title / artist / album / album artist / genre / composer.
    // Every word is a prefix match ("beat abb" finds "The Beatles – Abbey Road").
    // Returns track ids, best match first.
    QList<qint64> search(const QString &text, int limit = 500) const;

    // Turns user input into a safe FTS5 MATCH expression (exposed for tests).
    static QString toFtsQuery(const QString &text);

private:
    bool migrate(QString *error);
    bool exec(const QString &sql, QString *error);

    QString m_connectionName;
    QSqlDatabase m_db;
};

} // namespace mmm

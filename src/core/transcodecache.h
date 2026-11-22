// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace mmm {

// Files prepared for devices (MP3 transcodes, copies with resized or stripped art), kept so
// a re-sync or a second device doesn't convert them again (M9). Keyed by key(): the source
// file's path, size and mtime plus how the output is made, so an edited source or another
// profile setting never hits a stale entry. Flat folder of "<key>.<ext>" files.
//
// Thread-safe as far as the filesystem is: entries appear atomically (rename) and are
// read-only once there. evict() deletes files, so call it only when no sync is running.
class TranscodeCache
{
public:
    static constexpr qint64 kDefaultMaxBytes = 2LL * 1024 * 1024 * 1024;

    explicit TranscodeCache(QString dir, qint64 maxBytes = kDefaultMaxBytes);

    QString dir() const { return m_dir; }
    qint64 maxBytes() const { return m_maxBytes; }

    static QString key(const QString &sourcePath, qint64 sourceSize, qint64 sourceMtimeMs, const QString &outputSig);

    // Where the entry lives (whether or not it exists yet).
    QString path(const QString &key, const QString &extension) const;
    // The entry's size, or -1 if it isn't cached.
    qint64 size(const QString &key, const QString &extension) const;
    // A unique hidden name in the cache folder to prepare an entry in (same filesystem, so
    // insert() can rename it). Creates the folder.
    QString tempPath(const QString &extension) const;
    // Moves a prepared file into the cache. Returns the entry's path, "" on failure.
    QString insert(const QString &key, const QString &extension, const QString &preparedPath,
                   QString *error = nullptr);
    // Marks an entry as just used (eviction removes the least recently used first).
    void touch(const QString &entryPath) const;

    qint64 totalBytes() const;
    // Deletes the least recently used entries until the cache fits maxBytes, and leftovers
    // of interrupted preparations. Returns how many files were deleted.
    int evict();

private:
    QString m_dir;
    qint64 m_maxBytes;
};

} // namespace mmm

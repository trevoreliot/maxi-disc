// SPDX-License-Identifier: GPL-3.0-or-later
#include "transcodecache.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace mmm {

namespace {

const QString kTempPrefix = QStringLiteral(".prep-");

} // namespace

TranscodeCache::TranscodeCache(QString dir, qint64 maxBytes)
    : m_dir(std::move(dir)), m_maxBytes(maxBytes)
{
}

QString TranscodeCache::key(const QString &sourcePath, qint64 sourceSize, qint64 sourceMtimeMs, const QString &outputSig)
{
    const QString material = QStringLiteral("%1\n%2\n%3\n%4").arg(sourcePath).arg(sourceSize).arg(sourceMtimeMs).arg(outputSig);
    return QString::fromLatin1(QCryptographicHash::hash(material.toUtf8(), QCryptographicHash::Sha1).toHex());
}

QString TranscodeCache::path(const QString &key, const QString &extension) const
{
    return QDir(m_dir).filePath(key + QLatin1Char('.') + extension);
}

qint64 TranscodeCache::size(const QString &key, const QString &extension) const
{
    const QFileInfo info(path(key, extension));
    return info.isFile() ? info.size() : -1;
}

QString TranscodeCache::tempPath(const QString &extension) const
{
    QDir().mkpath(m_dir);
    const QString random = QString::number(QRandomGenerator::global()->generate64(), 16);
    return QDir(m_dir).filePath(kTempPrefix + random + QLatin1Char('.') + extension);
}

QString TranscodeCache::insert(const QString &key, const QString &extension, const QString &preparedPath, QString *error)
{
    const QString target = path(key, extension);
    std::error_code ec;
    std::filesystem::rename(QFile::encodeName(preparedPath).toStdString(), QFile::encodeName(target).toStdString(), ec);
    if (ec) {
        if (error)
            *error = QStringLiteral("cannot store %1 in the cache: %2").arg(target, QString::fromStdString(ec.message()));
        QFile::remove(preparedPath);
        return {};
    }
    return target;
}

void TranscodeCache::touch(const QString &entryPath) const
{
    QFile f(entryPath);
    if (f.open(QIODevice::ReadWrite))
        f.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
}

qint64 TranscodeCache::totalBytes() const
{
    qint64 total = 0;
    QDirIterator it(m_dir, QDir::Files | QDir::Hidden);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

int TranscodeCache::evict()
{
    struct Entry
    {
        QString path;
        qint64 size;
        QDateTime used;
    };
    QList<Entry> entries;
    qint64 total = 0;
    int removed = 0;
    QDirIterator it(m_dir, QDir::Files | QDir::Hidden);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        if (info.fileName().startsWith(kTempPrefix)) {
            // No sync is running (see the class comment): an interrupted preparation.
            removed += QFile::remove(info.filePath()) ? 1 : 0;
            continue;
        }
        entries << Entry{info.filePath(), info.size(), info.lastModified()};
        total += info.size();
    }
    if (total <= m_maxBytes)
        return removed;
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.used < b.used; });
    for (const Entry &e : std::as_const(entries)) {
        if (total <= m_maxBytes)
            break;
        if (QFile::remove(e.path)) {
            total -= e.size;
            ++removed;
        }
    }
    return removed;
}

} // namespace mmm

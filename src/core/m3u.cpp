// SPDX-License-Identifier: GPL-3.0-or-later
#include "m3u.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringDecoder>
#include <QUrl>

namespace mmm::M3u {

namespace {

QString decode(const QByteArray &raw, bool forceUtf8)
{
    QByteArray bytes = raw;
    if (bytes.startsWith("\xEF\xBB\xBF"))
        bytes.remove(0, 3);
    QStringDecoder utf8(QStringDecoder::Utf8);
    QString text = utf8.decode(bytes);
    if (forceUtf8 || !utf8.hasError())
        return text;
    return QString::fromLatin1(bytes); // legacy .m3u from Windows-era tools
}

// An entry's file. Playlists made on Windows or by portable players use backslashes and
// paths rooted on their own drive ("\Music\Artist\Album\01.mp3", "E:\Music\…"). When such a
// path doesn't exist as written, it's tried below each folder above the playlist, and with its
// leading folders dropped (longest match first): a playlist in <root>/Music/Playlists finds
// "\Music\A\b.mp3" at <root>/Music/A/b.mp3. Anything not found is returned as written.
QString resolve(const QString &line, const QDir &base)
{
    QString p = line;
    p.replace(QLatin1Char('\\'), QLatin1Char('/')); // a backslash in a music file name is all but unheard of
    const bool drive = p.size() >= 2 && p.at(1) == QLatin1Char(':') && p.at(0).isLetter();
    if (drive)
        p = p.mid(2);
    const bool rooted = p.startsWith(QLatin1Char('/'));
    const QString asWritten = QDir::cleanPath(rooted ? p : base.absoluteFilePath(p));
    if (QFileInfo::exists(asWritten) || (!rooted && !drive))
        return asWritten;

    const QStringList parts = QDir::cleanPath(p).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    // Keep a folder with the file name (unless that's all there is): a bare name could match
    // some other file that happens to be called the same.
    const qsizetype maxSkip = std::max<qsizetype>(0, parts.size() - 2);
    for (qsizetype skip = 0; skip <= maxSkip; ++skip) {
        const QString tail = parts.mid(skip).join(QLatin1Char('/'));
        for (QDir dir = base;;) {
            const QString candidate = QDir::cleanPath(dir.absoluteFilePath(tail));
            if (QFileInfo::exists(candidate))
                return candidate;
            if (!dir.cdUp())
                break;
        }
    }
    return asWritten;
}

} // namespace

bool read(const QString &playlistPath, QList<Entry> *entries, QString *error)
{
    QFile f(playlistPath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    const bool utf8 = playlistPath.endsWith(QLatin1String(".m3u8"), Qt::CaseInsensitive);
    const QString text = decode(f.readAll(), utf8);
    const QDir base = QFileInfo(playlistPath).absoluteDir();

    entries->clear();
    Entry pending;
    for (QString line : text.split(QLatin1Char('\n'))) {
        line = line.trimmed(); // also drops the \r of CRLF files
        if (line.isEmpty())
            continue;
        if (line.startsWith(QLatin1Char('#'))) {
            if (line.startsWith(QLatin1String("#EXTINF:"), Qt::CaseInsensitive)) {
                // #EXTINF:<seconds>[ attrs],<title>
                const QString rest = line.mid(8);
                const qsizetype comma = rest.indexOf(QLatin1Char(','));
                bool ok = false;
                const double secs = rest.left(comma < 0 ? rest.size() : comma).section(QLatin1Char(' '), 0, 0).toDouble(&ok);
                pending.durationMs = ok && secs >= 0 ? static_cast<qint64>(secs * 1000) : -1;
                pending.title = comma < 0 ? QString() : rest.mid(comma + 1).trimmed();
            }
            continue;
        }

        QString path;
        if (line.contains(QLatin1String("://"))) {
            const QUrl url(line);
            if (!url.isLocalFile()) {
                pending = {};
                continue; // streams aren't library tracks
            }
            path = url.toLocalFile();
        } else {
            path = resolve(line, base);
        }
        pending.path = QDir::cleanPath(path);
        entries->append(pending);
        pending = {};
    }
    return true;
}

QByteArray format(const QString &playlistPath, const QList<Entry> &entries, bool relative)
{
    const QDir base = QFileInfo(playlistPath).absoluteDir();
    QString out = QStringLiteral("#EXTM3U\n");
    for (const Entry &e : entries) {
        const qint64 secs = e.durationMs >= 0 ? (e.durationMs + 500) / 1000 : -1;
        out += QStringLiteral("#EXTINF:%1,%2\n").arg(secs).arg(e.title);
        out += (relative ? base.relativeFilePath(e.path) : QDir::cleanPath(e.path)) + QLatin1Char('\n');
    }
    return out.toUtf8();
}

bool write(const QString &playlistPath, const QList<Entry> &entries, bool relative, QString *error)
{
    QSaveFile f(playlistPath);
    if (!f.open(QIODevice::WriteOnly) || f.write(format(playlistPath, entries, relative)) < 0 || !f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

} // namespace mmm::M3u

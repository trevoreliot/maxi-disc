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
            path = QDir::fromNativeSeparators(line);
            if (QDir::isRelativePath(path))
                path = base.absoluteFilePath(path);
        }
        pending.path = QDir::cleanPath(path);
        entries->append(pending);
        pending = {};
    }
    return true;
}

bool write(const QString &playlistPath, const QList<Entry> &entries, bool relative, QString *error)
{
    const QDir base = QFileInfo(playlistPath).absoluteDir();
    QString out = QStringLiteral("#EXTM3U\n");
    for (const Entry &e : entries) {
        const qint64 secs = e.durationMs >= 0 ? (e.durationMs + 500) / 1000 : -1;
        out += QStringLiteral("#EXTINF:%1,%2\n").arg(secs).arg(e.title);
        out += (relative ? base.relativeFilePath(e.path) : QDir::cleanPath(e.path)) + QLatin1Char('\n');
    }

    QSaveFile f(playlistPath);
    if (!f.open(QIODevice::WriteOnly) || f.write(out.toUtf8()) < 0 || !f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

} // namespace mmm::M3u

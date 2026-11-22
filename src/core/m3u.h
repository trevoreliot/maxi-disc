// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace mmm::M3u {

struct Entry
{
    QString path;          // absolute when reading; as written (absolute or relative) when writing
    QString title;         // from #EXTINF ("Artist - Title"), may be empty
    qint64 durationMs = -1;
};

// Reads an .m3u / .m3u8 playlist. Relative paths resolve against the playlist's folder;
// Windows / portable-player paths ("\Music\A\b.mp3", "E:\Music\…") are found below the
// folders above the playlist when they don't exist as written;
// file:// URLs become local paths; other URLs (http…) are skipped. Handles CRLF, a UTF-8
// BOM and #EXTINF. .m3u8 is UTF-8; .m3u is read as UTF-8 if valid, else Latin-1.
// Returns false (with `error`) if the file can't be read.
bool read(const QString &playlistPath, QList<Entry> *entries, QString *error = nullptr);

// Writes an extended M3U (UTF-8, LF). With `relative`, paths are written relative to the
// playlist's folder (portable, used for USB sync); otherwise absolute. Atomic (temp + rename).
bool write(const QString &playlistPath, const QList<Entry> &entries, bool relative, QString *error = nullptr);

// The bytes write() puts in a playlist at `playlistPath` (USB sync compares them with what's
// on the device). The paths need not exist.
QByteArray format(const QString &playlistPath, const QList<Entry> &entries, bool relative);

} // namespace mmm::M3u

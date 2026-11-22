// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "libraryquery.h"
#include "tracktags.h"

#include <optional>

namespace mmm {

class Database;

// A tag edit: only the fields that are set get written; the rest of the file's tags are
// kept as they are. An empty string / 0 / false removes the tag.
struct TagChanges
{
    std::optional<QString> title;
    std::optional<QString> artist;
    std::optional<QString> album;
    std::optional<QString> albumArtist;
    std::optional<QString> genre;
    std::optional<QString> composer;
    std::optional<QString> comment;
    std::optional<int> year;
    std::optional<int> trackNumber;
    std::optional<int> trackTotal;
    std::optional<int> discNumber;
    std::optional<int> discTotal;
    std::optional<bool> compilation;
    // Replaces the front cover (JPEG or PNG data). Other picture types are kept, except in
    // MP4 files, which can't tell pictures apart: there it replaces all of them.
    std::optional<EmbeddedPicture> frontCover;

    bool isEmpty() const
    {
        return !title && !artist && !album && !albumArtist && !genre && !composer && !comment && !year
            && !trackNumber && !trackTotal && !discNumber && !discTotal && !compilation && !frontCover;
    }
};

// Writes tags with TagLib, for every format TagReader reads.
//
// The file is never edited in place: the edit is made on a copy next to it (a reflink where
// the filesystem supports it), which then replaces the original with rename(). A crash or
// a full disk can't leave a half-written file, and a player that has the file open keeps
// reading the old data. The file's inode changes, so hard links to it keep the old tags.
// Stateless and thread-safe.
class TagWriter
{
public:
    static bool write(const QString &path, const TagChanges &changes, QString *error = nullptr);

    // USB sync (M9): tags of a file prepared for a device, written in place (it's a private
    // copy in the transcode cache). `tagSource` non-empty: copy every tag from that file first
    // (a fresh transcode has none); MP3s get ID3v2.3, which more players read. Art: Keep the
    // pictures (the source's, after a copy), Replace them all with `cover`, or Strip them.
    enum class DeviceArt { Keep, Replace, Strip };
    static bool writeDeviceTags(const QString &path, const QString &tagSource, DeviceArt art,
                                const EmbeddedPicture &cover = {}, QString *error = nullptr);
};

struct TagEditResult
{
    QStringList written;   // files whose tags were changed
    QStringList failures;  // "path: reason"
    QList<TrackRow> tracks; // the written tracks as the library has them now
};

// Writes `changes` to each file, then re-reads it and updates its library entry
// (Database::updateEditedTrack), and finally prunes albums/artists/genres left empty.
// Blocking file I/O: run it on a worker thread, with that thread's Database.
TagEditResult editTracks(Database &db, const QStringList &paths, const TagChanges &changes);

} // namespace mmm

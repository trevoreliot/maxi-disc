// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QStringList>

#include "tagwriter.h"

namespace mmm {

class Database;

struct ArtworkSaveResult
{
    bool ok = false;          // the cover was stored somewhere (a folder file and/or a track)
    QString error;            // why nothing was stored, when !ok
    QStringList coverFiles;   // cover.jpg / cover.png files written
    QStringList warnings;     // partial failures and skipped folders, readable
    TagEditResult edit;       // the embedding pass (written, failures, tracks as stored now)
};

// Saves album art the way the artwork finder stores it (docs/decisions/0003):
// - `cover.jpg` (or `cover.png` for PNG data) in every folder that holds only this album's
//   tracks, written atomically and replacing an existing file of that name. Folders shared
//   with other albums get no file: it would become their folder art too.
// - The picture embedded as the front cover of every track of the album (TagWriter).
// - The album's folder art in the library set to the first file written.
// Blocking file I/O: run it on a worker thread, with that thread's Database.
class ArtworkSaver
{
public:
    // Biggest image accepted (Cover Art Archive 1200 px thumbnails are ~0.1–0.5 MB).
    static constexpr qsizetype kMaxImageBytes = 16 * 1024 * 1024;

    static ArtworkSaveResult save(Database &db, qint64 albumId, const QByteArray &image);

    // "image/jpeg" / "image/png" from the data's signature, "" for anything else.
    static QString imageMimeType(QByteArrayView data);
};

} // namespace mmm

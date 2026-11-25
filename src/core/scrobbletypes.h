// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

#include "libraryquery.h"

// Scrobbling (M14, ListenBrainz): what's sent about a play. Plain values.
namespace mmm {

// The tags a scrobble carries (a snapshot: later tag edits don't change a queued one).
struct ScrobbleTrack
{
    QString artist;
    QString title;
    QString album;
    QString albumArtist;
    int trackNumber = 0;
    qint64 durationMs = 0;

    static ScrobbleTrack from(const TrackRow &row)
    {
        return {row.artist, row.title, row.album, row.albumArtist, row.trackNumber, row.durationMs};
    }
    friend bool operator==(const ScrobbleTrack &, const ScrobbleTrack &) = default;
};

// A play to report (queued until ListenBrainz has it).
struct PendingScrobble
{
    qint64 id = 0;          // the queue's row (0: not queued yet)
    ScrobbleTrack track;
    qint64 startedAt = 0;   // when the play started, UTC seconds since the epoch
    int attempts = 0;       // sends that failed for a reason worth retrying
    friend bool operator==(const PendingScrobble &, const PendingScrobble &) = default;
};

} // namespace mmm

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <optional>

#include "scrobbletypes.h"

namespace mmm {

// When a play counts (M14): listened to for half its length or 4 minutes, whichever comes first
// (ListenBrainz's guideline), and the track has an artist and a title and is longer than 30 s
// (the scrobblers' usual rule: skits and jingles don't count). Listened = time actually playing
// (pauses don't count; seeking doesn't add or take away). The listen's time is when the play
// started playing. Each play counts once; a replay (repeat one) is a new play; new tags for the
// playing track (an edit) are not.
//
// Plain logic, no timers: the caller reports events and arms a timer for msUntilDue(). Every
// event that can complete a play returns its scrobble when it's due (at most once per play).
class ScrobbleTracker
{
public:
    using Clock = std::function<qint64()>;
    // `monotonicMs`: elapsed time (default: QElapsedTimer's; doesn't run while suspended).
    // `utcSeconds`: wall clock (default: QDateTime::currentSecsSinceEpoch()).
    explicit ScrobbleTracker(Clock monotonicMs = {}, Clock utcSeconds = {});

    static bool eligible(const ScrobbleTrack &track);
    static qint64 thresholdMs(qint64 durationMs); // half, at most 4 minutes

    // A new play of `track` (`playing`: it's playing already). The play before it ends first.
    std::optional<PendingScrobble> start(const ScrobbleTrack &track, bool playing);
    std::optional<PendingScrobble> setPlaying(bool playing);
    // The playing track's tags changed: the play goes on (its scrobble gets the new tags).
    void retag(const ScrobbleTrack &track);
    // The length became known (the tags had none): the threshold follows.
    void setDuration(qint64 durationMs);
    // Is it due now? (The caller's timer for msUntilDue() fired.)
    std::optional<PendingScrobble> check();
    // The play ended (end of the queue, cleared, the app quitting).
    std::optional<PendingScrobble> stop();

    // How long until it's due, if it's playing and can still be scrobbled; else nullopt.
    std::optional<qint64> msUntilDue() const;
    bool hasPlay() const { return m_active; }
    bool isPlaying() const { return m_active && m_playingSince >= 0; }
    const ScrobbleTrack &track() const { return m_track; }
    qint64 listenedMs() const;

private:
    std::optional<PendingScrobble> due();

    Clock m_mono;
    Clock m_utc;
    bool m_active = false;
    ScrobbleTrack m_track;
    qint64 m_listened = 0;      // before the current stretch of playing
    qint64 m_playingSince = -1; // monotonic ms; -1: not playing
    qint64 m_startedAt = -1;    // UTC s, set when it first plays
    bool m_scrobbled = false;
};

} // namespace mmm

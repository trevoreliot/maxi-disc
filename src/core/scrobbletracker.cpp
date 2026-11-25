// SPDX-License-Identifier: GPL-3.0-or-later
#include "scrobbletracker.h"

#include <QDateTime>
#include <QElapsedTimer>

#include <algorithm>
#include <memory>

namespace mmm {

namespace {

constexpr qint64 kMinDurationMs = 30'000;
constexpr qint64 kMaxThresholdMs = 240'000;

} // namespace

ScrobbleTracker::ScrobbleTracker(Clock monotonicMs, Clock utcSeconds)
    : m_mono(std::move(monotonicMs))
    , m_utc(std::move(utcSeconds))
{
    if (!m_mono) {
        auto timer = std::make_shared<QElapsedTimer>();
        timer->start();
        m_mono = [timer] { return timer->elapsed(); };
    }
    if (!m_utc)
        m_utc = [] { return QDateTime::currentSecsSinceEpoch(); };
}

bool ScrobbleTracker::eligible(const ScrobbleTrack &track)
{
    return track.durationMs > kMinDurationMs && !track.artist.trimmed().isEmpty() && !track.title.trimmed().isEmpty();
}

qint64 ScrobbleTracker::thresholdMs(qint64 durationMs)
{
    return std::min(durationMs / 2, kMaxThresholdMs);
}

qint64 ScrobbleTracker::listenedMs() const
{
    return m_listened + (m_playingSince >= 0 ? m_mono() - m_playingSince : 0);
}

std::optional<PendingScrobble> ScrobbleTracker::due()
{
    if (!m_active || m_scrobbled || m_startedAt < 0 || !eligible(m_track))
        return std::nullopt;
    if (listenedMs() < thresholdMs(m_track.durationMs))
        return std::nullopt;
    m_scrobbled = true;
    return PendingScrobble{0, m_track, m_startedAt, 0};
}

std::optional<PendingScrobble> ScrobbleTracker::start(const ScrobbleTrack &track, bool playing)
{
    std::optional<PendingScrobble> before = stop();
    m_active = true;
    m_track = track;
    m_listened = 0;
    m_playingSince = -1;
    m_startedAt = -1;
    m_scrobbled = false;
    setPlaying(playing);
    return before;
}

std::optional<PendingScrobble> ScrobbleTracker::setPlaying(bool playing)
{
    if (!m_active || playing == (m_playingSince >= 0))
        return std::nullopt;
    if (playing) {
        m_playingSince = m_mono();
        if (m_startedAt < 0)
            m_startedAt = m_utc(); // the play starts when it first plays, not when it was loaded
        return std::nullopt;
    }
    std::optional<PendingScrobble> s = due(); // counted up to the pause
    m_listened += m_mono() - m_playingSince;
    m_playingSince = -1;
    return s;
}

void ScrobbleTracker::retag(const ScrobbleTrack &track)
{
    if (!m_active)
        return;
    const qint64 duration = track.durationMs > 0 ? track.durationMs : m_track.durationMs;
    m_track = track;
    m_track.durationMs = duration;
}

void ScrobbleTracker::setDuration(qint64 durationMs)
{
    // Only when the tags didn't say: the player's length can belong to the next track already
    // (it may arrive before that track's play starts).
    if (m_active && durationMs > 0 && m_track.durationMs <= 0)
        m_track.durationMs = durationMs;
}

std::optional<PendingScrobble> ScrobbleTracker::check()
{
    return due();
}

std::optional<PendingScrobble> ScrobbleTracker::stop()
{
    if (!m_active)
        return std::nullopt;
    std::optional<PendingScrobble> s = due();
    m_active = false;
    m_playingSince = -1;
    return s;
}

std::optional<qint64> ScrobbleTracker::msUntilDue() const
{
    if (!isPlaying() || m_scrobbled || !eligible(m_track))
        return std::nullopt;
    return std::max<qint64>(0, thresholdMs(m_track.durationMs) - listenedMs());
}

} // namespace mmm

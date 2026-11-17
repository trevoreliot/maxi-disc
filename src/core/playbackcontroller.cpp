// SPDX-License-Identifier: GPL-3.0-or-later
#include "playbackcontroller.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcPlayback, "mmm.playback")

namespace mmm {

namespace {
constexpr qint64 kRestartThresholdMs = 3000;

QUrl urlFor(const TrackRow &t)
{
    return QUrl::fromLocalFile(t.path);
}
} // namespace

PlaybackController::PlaybackController(const PlayerOptions &options, QObject *parent)
    : QObject(parent)
    , m_player(options)
{
    connect(&m_player, &Player::advancedToNext, this, &PlaybackController::onAdvanced);
    connect(&m_player, &Player::endOfStream, this, &PlaybackController::onEndOfStream);
    connect(&m_player, &Player::errorOccurred, this, &PlaybackController::onError);
    connect(&m_player, &Player::playbackStateChanged, this, [this](Player::PlaybackState s) {
        if (s == Player::PlaybackState::Playing)
            m_consecutiveErrors = 0;
    });

    connect(&m_queue, &PlayQueue::upcomingChanged, this, &PlaybackController::syncNextSource);
    connect(&m_queue, &PlayQueue::currentRemoved, this, [this] {
        // Removing the playing track skips to whatever is now current.
        const bool wasActive = m_player.playbackState() != Player::PlaybackState::Stopped;
        if (m_queue.current() && wasActive)
            startCurrent();
        else
            stop();
    });
}

PlaybackController::~PlaybackController() = default;

bool PlaybackController::canGoNext() const
{
    // Mirrors next(): forward one, wrapping only with repeat-all (repeat-one still skips).
    const int cur = m_queue.currentIndex();
    if (cur < 0)
        return false;
    return cur + 1 < m_queue.count() || (m_queue.repeatMode() == PlayQueue::RepeatMode::All && m_queue.count() > 1);
}

bool PlaybackController::canGoPrevious() const
{
    return m_queue.current() != nullptr;
}

void PlaybackController::playTracks(const QList<TrackRow> &tracks, int start)
{
    m_queue.setItems(tracks, start);
    startCurrent();
}

void PlaybackController::playIndex(int index)
{
    if (!m_queue.at(index))
        return;
    m_queue.setCurrentIndex(index);
    startCurrent();
}

void PlaybackController::next()
{
    if (!canGoNext())
        return;
    // Unlike the automatic advance, an explicit "next" skips even in repeat-one.
    playIndex((m_queue.currentIndex() + 1) % m_queue.count());
}

void PlaybackController::previous()
{
    const int prev = m_queue.previousIndex();
    if (m_player.position() > kRestartThresholdMs || prev < 0) {
        m_player.seek(0);
        return;
    }
    playIndex(prev);
}

void PlaybackController::play()
{
    if (m_player.playbackState() == Player::PlaybackState::Paused) {
        m_player.play();
        return;
    }
    if (m_player.playbackState() == Player::PlaybackState::Playing)
        return;
    if (!m_queue.current() && m_queue.count() > 0)
        m_queue.setCurrentIndex(0);
    if (m_queue.current())
        startCurrent();
}

void PlaybackController::pause()
{
    if (m_player.playbackState() == Player::PlaybackState::Playing)
        m_player.pause();
}

void PlaybackController::togglePlayPause()
{
    if (m_player.playbackState() == Player::PlaybackState::Playing)
        pause();
    else
        play();
}

void PlaybackController::stop()
{
    m_player.stop();
}

void PlaybackController::seek(qint64 positionMs)
{
    m_player.seek(positionMs);
}

void PlaybackController::clearQueue()
{
    m_player.stop();
    m_player.setSource({});
    m_queue.clear();
    if (m_announcedTrackId != -1) {
        m_announcedTrackId = -1;
        Q_EMIT currentTrackChanged();
    }
}

void PlaybackController::startCurrent()
{
    const TrackRow *t = m_queue.current();
    if (!t) {
        m_player.stop();
        return;
    }
    m_player.setSource(urlFor(*t)); // also clears the Player's next source
    syncNextSource();
    m_player.play();
    m_announcedTrackId = t->id;
    Q_EMIT currentTrackChanged();
}

void PlaybackController::syncNextSource()
{
    if (m_player.source().isEmpty())
        return;
    // Never hand playbin3 the current URI as its gapless next: replaying a stream into
    // itself isn't reliable (it ends early). Looping one track (repeat-one, or repeat-all
    // with a single track) restarts it at end-of-stream instead: a tiny gap, by design.
    const int next = m_queue.nextIndex();
    const TrackRow *n = next != m_queue.currentIndex() ? m_queue.at(next) : nullptr;
    m_player.setNextSource(n ? urlFor(*n) : QUrl());
}

void PlaybackController::onAdvanced(const QUrl &url)
{
    // The queue wins. Normally GStreamer started exactly the queue's next track. If the
    // queue changed after GStreamer had already taken the next URL (e.g. a track was
    // inserted or removed), play what the queue says instead: one audible restart, rare.
    const int expected = m_queue.nextIndex();
    const TrackRow *e = m_queue.at(expected);
    if (!e) {
        qCDebug(lcPlayback) << "gapless track no longer queued; stopping:" << url;
        stop();
        return;
    }
    if (urlFor(*e) != url) {
        qCDebug(lcPlayback) << "queue changed after hand-off; switching to" << e->path;
        playIndex(expected);
        return;
    }
    m_queue.setCurrentIndex(expected); // emits upcomingChanged → syncNextSource()
    syncNextSource();                  // repeat-one: index unchanged, still needs re-queuing
    m_announcedTrackId = e->id;
    Q_EMIT currentTrackChanged();
}

void PlaybackController::onEndOfStream()
{
    // No gapless next was taken (end of queue, or the queue changed too late): continue
    // the normal way if there's somewhere to go.
    const int n = m_queue.nextIndex();
    if (n >= 0 && n != m_queue.currentIndex())
        playIndex(n);
    else if (n >= 0) // repeat-one
        startCurrent();
}

void PlaybackController::onError(const QString &message)
{
    qCWarning(lcPlayback) << "cannot play" << (m_queue.current() ? m_queue.current()->path : QString()) << ":"
                          << message;
    // Skip unplayable tracks, but don't spin forever if nothing in the queue plays.
    if (++m_consecutiveErrors >= std::max(1, m_queue.count()))
        return;
    const int n = m_queue.currentIndex() + 1;
    if (n < m_queue.count())
        playIndex(n);
    else if (m_queue.repeatMode() == PlayQueue::RepeatMode::All && m_queue.count() > 0)
        playIndex(0);
}

} // namespace mmm

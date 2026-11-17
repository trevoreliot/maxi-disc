// SPDX-License-Identifier: GPL-3.0-or-later
#include "playbackcontroller.h"

#include <QLoggingCategory>
#include <QTimer>

Q_LOGGING_CATEGORY(lcPlayback, "mmm.playback")

namespace mmm {

namespace {
constexpr qint64 kRestartThresholdMs = 3000;
// How long to let playbin3 finish a gapless switch before changing pipeline state. Changing
// state (READY) while it is still handling the switch can deadlock inside playbin3: the main
// thread waits in the state change while a streaming thread waits for playbin3's lock.
constexpr int kSwitchSettleMs = 300;

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
    ++m_actionGeneration;
    m_player.stop();
}

void PlaybackController::seek(qint64 positionMs)
{
    m_player.seek(positionMs);
}

void PlaybackController::clearQueue()
{
    ++m_actionGeneration;
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
    ++m_actionGeneration;
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
    // Everything that follows the current track, in order (a few is plenty: GStreamer only
    // runs ahead by its buffering). Repeat-one: nothing (looping restarts at end-of-stream;
    // playbin3 can't reliably play a stream gaplessly into itself, and Player won't chain a
    // file onto itself either). Repeat-all wraps around but stops before the current track.
    constexpr int kLookahead = 8;
    QList<QUrl> upcoming;
    if (m_queue.repeatMode() != PlayQueue::RepeatMode::One && m_queue.currentIndex() >= 0) {
        const int n = m_queue.count();
        const bool wrap = m_queue.repeatMode() == PlayQueue::RepeatMode::All;
        for (int step = 1; step < n && upcoming.size() < kLookahead; ++step) {
            const int i = m_queue.currentIndex() + step;
            if (i >= n && !wrap)
                break;
            upcoming << urlFor(*m_queue.at(i % n));
        }
    }
    m_player.setUpcomingSources(upcoming);
}

void PlaybackController::onAdvanced(const QUrl &url)
{
    // The queue wins. Normally GStreamer started exactly the queue's next track. If the
    // queue changed after GStreamer had already taken the next URL (a track was inserted or
    // removed in the last seconds of the previous one), correct to what the queue says, but
    // only once the switch has settled (see kSwitchSettleMs): never change pipeline state
    // while handling a stream start.
    const int expected = m_queue.nextIndex();
    const TrackRow *e = m_queue.at(expected);
    if (!e || urlFor(*e) != url) {
        qCDebug(lcPlayback) << "queue changed after hand-off; correcting after the switch:" << url;
        deferCorrection();
        return;
    }
    m_queue.setCurrentIndex(expected); // emits upcomingChanged → syncNextSource()
    syncNextSource();                  // repeat-one: index unchanged, still needs re-queuing
    m_announcedTrackId = e->id;
    Q_EMIT currentTrackChanged();
}

void PlaybackController::deferCorrection()
{
    const quint64 generation = m_actionGeneration;
    QTimer::singleShot(kSwitchSettleMs, this, [this, generation] {
        if (generation != m_actionGeneration)
            return; // the user did something else meanwhile
        const int n = m_queue.nextIndex();
        if (n >= 0 && n != m_queue.currentIndex())
            playIndex(n);
        else
            stop();
    });
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

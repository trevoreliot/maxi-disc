// SPDX-License-Identifier: GPL-3.0-or-later
#include "player.h"

#include "playerengine.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QMap>
#include <QMetaObject>
#include <QMutex>
#include <QSet>
#include <QThread>
#include <QTimer>

#include <algorithm>

Q_LOGGING_CATEGORY(lcPlayer, "mmm.player")

namespace mmm {

namespace {

// A command the engine hasn't acknowledged within this long means it's stuck in GStreamer.
constexpr qint64 kCommandStallMs = 3000;
// play() that hasn't reached PLAYING within this long (no error/EOS either) is stuck too.
constexpr qint64 kStartStallMs = 6000;
constexpr int kWatchdogTickMs = 250;
// More recoveries than this within the window means something is badly broken (e.g. the
// audio device): give up with an error instead of looping.
constexpr int kMaxRecoveries = 3;
constexpr qint64 kRecoveryWindowMs = 30000;
// Normal shutdown waits this long for the engine thread before abandoning it.
constexpr int kShutdownWaitMs = 2000;

// Engines whose thread may still be running after their Player gave up on them. Never
// destroyed while running (that would abort); they remove themselves when the thread ends.
// Reachable from here, so LeakSanitizer doesn't report a stuck one as a leak.
struct Graveyard
{
    QMutex mutex;
    QSet<QThread *> threads;
};

Graveyard &graveyard()
{
    static Graveyard g;
    return g;
}

void bury(detail::PlayerEngine *engine, QThread *thread)
{
    {
        QMutexLocker lock(&graveyard().mutex);
        graveyard().threads.insert(thread);
    }
    // When (if) it finishes: delete it on the thread that owns the QThread object.
    QObject *context = QCoreApplication::instance() ? static_cast<QObject *>(QCoreApplication::instance()) : nullptr;
    QObject::connect(thread, &QThread::finished, engine, &QObject::deleteLater);
    if (context) {
        QObject::connect(thread, &QThread::finished, context, [thread] {
            {
                QMutexLocker lock(&graveyard().mutex);
                graveyard().threads.remove(thread);
            }
            thread->deleteLater();
        });
    }
    QMetaObject::invokeMethod(engine, &detail::PlayerEngine::shutdown, Qt::QueuedConnection);
}

} // namespace

struct Player::Private
{
    explicit Private(Player *q_, const PlayerOptions &o) : q(q_), options(o) {}

    Player *q;
    PlayerOptions options;

    detail::PlayerEngine *engine = nullptr;
    QThread *thread = nullptr;
    std::shared_ptr<detail::GaplessState> gapless;

    // Cached, authoritative-for-the-UI state.
    QUrl source;
    PlaybackState state = PlaybackState::Stopped;
    PlaybackState target = PlaybackState::Stopped;
    qint64 position = 0;
    qint64 duration = -1;
    bool seekable = false;
    double volume = 1.0;
    ReplayGainMode rgMode = ReplayGainMode::Album;
    quint64 epoch = 0; // current stream; events for older ones are ignored

    // Watchdog.
    quint64 nextSeq = 0;
    QMap<quint64, qint64> pending; // seq → sent at (ms, `clock`)
    qint64 startDeadline = -1;     // play() must reach Playing before this (ms, `clock`)
    QElapsedTimer clock;
    QTimer watchdog;
    QList<qint64> recoveries; // times (ms, `clock`)

    void createEngine();
    void releaseEngine(bool waitForIt);
    template<typename Method, typename... Args>
    void send(Method method, Args... args);
    void checkWatchdog();
    void recover(const QString &reason);

    void setState(PlaybackState s);
    void setPosition(qint64 ms);
    void setDuration(qint64 ms);
    void setSeekable(bool s);
    bool isCurrent(const detail::PlayerEngine *e, quint64 ev) const { return e == engine && ev == epoch; }
};

void Player::Private::createEngine()
{
    gapless = std::make_shared<detail::GaplessState>();
    {
        QMutexLocker lock(&gapless->mutex);
        gapless->epoch = epoch;
        gapless->lastQueuedUri = source;
    }
    thread = new QThread;
    thread->setObjectName(QStringLiteral("mmm-player-engine"));
    engine = new detail::PlayerEngine(options, gapless);
    engine->moveToThread(thread);
    thread->start();

    using E = detail::PlayerEngine;
    E *e = engine;
    // Every handler checks it's still talking to the current engine and stream.
    connect(e, &E::commandDone, q, [this, e](quint64 seq) {
        if (e == engine)
            pending.remove(seq);
    });
    connect(e, &E::stateChanged, q, [this, e](quint64 ev, PlaybackState s) {
        if (!isCurrent(e, ev))
            return;
        if (s == PlaybackState::Playing)
            startDeadline = -1;
        setState(s);
    });
    connect(e, &E::positionChanged, q, [this, e](quint64 ev, qint64 ms) {
        if (isCurrent(e, ev))
            setPosition(ms);
    });
    connect(e, &E::durationChanged, q, [this, e](quint64 ev, qint64 ms) {
        if (isCurrent(e, ev))
            setDuration(ms);
    });
    connect(e, &E::seekableChanged, q, [this, e](quint64 ev, bool s) {
        if (isCurrent(e, ev))
            setSeekable(s);
    });
    connect(e, &E::seeked, q, [this, e](quint64 ev, qint64 ms) {
        if (!isCurrent(e, ev))
            return;
        setPosition(ms);
        Q_EMIT q->seeked(ms);
    });
    connect(e, &E::advanced, q, [this, e](quint64 ev, const QUrl &url) {
        if (!isCurrent(e, ev))
            return;
        source = url;
        setPosition(0);
        setDuration(-1);
        Q_EMIT q->sourceChanged(source);
        Q_EMIT q->advancedToNext(source);
    });
    connect(e, &E::endOfStream, q, [this, e](quint64 ev) {
        if (!isCurrent(e, ev))
            return;
        target = PlaybackState::Stopped;
        startDeadline = -1;
        setPosition(0);
        setState(PlaybackState::Stopped);
        Q_EMIT q->endOfStream();
    });
    connect(e, &E::errorOccurred, q, [this, e](quint64 ev, const QString &message) {
        if (!isCurrent(e, ev))
            return;
        target = PlaybackState::Stopped;
        startDeadline = -1;
        setPosition(0);
        setState(PlaybackState::Stopped);
        Q_EMIT q->errorOccurred(message);
    });

    // Bring the new engine up to date (no-ops for a fresh Player).
    send(&E::setVolume, volume);
    send(&E::setReplayGainMode, rgMode);
}

// Hands the engine over to shut down. With `waitForIt` (destruction), waits briefly for a
// clean stop; an engine that doesn't stop in time (or isn't waited for) is buried: it shuts
// down whenever it gets unstuck, without this Player.
void Player::Private::releaseEngine(bool waitForIt)
{
    if (!engine)
        return;
    detail::PlayerEngine *e = engine;
    QThread *t = thread;
    engine = nullptr;
    thread = nullptr;
    pending.clear();
    QObject::disconnect(e, nullptr, q, nullptr);
    e->abandon();

    if (waitForIt) {
        QMetaObject::invokeMethod(e, &detail::PlayerEngine::shutdown, Qt::QueuedConnection);
        if (t->wait(QDeadlineTimer(kShutdownWaitMs))) {
            delete e; // its thread has finished; nothing else refers to it
            delete t;
            return;
        }
        qCWarning(lcPlayer) << "playback engine did not shut down in time; abandoning it";
    }
    bury(e, t);
}

template<typename Method, typename... Args>
void Player::Private::send(Method method, Args... args)
{
    if (!engine)
        return;
    const quint64 seq = ++nextSeq;
    pending.insert(seq, clock.elapsed());
    if (!watchdog.isActive())
        watchdog.start();
    QMetaObject::invokeMethod(engine, method, Qt::QueuedConnection, seq, args...);
}

void Player::Private::checkWatchdog()
{
    const qint64 now = clock.elapsed();
    if (!pending.isEmpty() && now - pending.first() > kCommandStallMs) {
        recover(QStringLiteral("a playback command did not complete within %1 ms").arg(kCommandStallMs));
        return;
    }
    if (startDeadline >= 0 && now > startDeadline) {
        recover(QStringLiteral("playback did not start within %1 ms").arg(kStartStallMs));
        return;
    }
    if (pending.isEmpty() && startDeadline < 0)
        watchdog.stop();
}

void Player::Private::recover(const QString &reason)
{
    const qint64 now = clock.elapsed();
    recoveries.removeIf([now](qint64 t) { return now - t > kRecoveryWindowMs; });
    recoveries << now;

    // Collect what should follow the current track: anything the old engine had already
    // handed to GStreamer but not started is still "upcoming" from the user's point of view.
    QList<QUrl> upcoming;
    {
        QMutexLocker lock(&gapless->mutex);
        upcoming = gapless->committed + gapless->upcoming;
    }
    const PlaybackState resumeAs = target;
    const qint64 resumeAt = position;

    qCWarning(lcPlayer).noquote() << QStringLiteral("playback engine stalled (%1); replacing it").arg(reason);
    releaseEngine(false);
    startDeadline = -1;
    ++epoch;
    createEngine();

    if (recoveries.size() > kMaxRecoveries) {
        target = PlaybackState::Stopped;
        setState(PlaybackState::Stopped);
        Q_EMIT q->errorOccurred(QStringLiteral("The audio system stopped responding repeatedly; playback was stopped."));
        return;
    }

    if (!source.isEmpty()) {
        {
            QMutexLocker lock(&gapless->mutex);
            gapless->upcoming = upcoming;
        }
        send(&detail::PlayerEngine::setSource, epoch, source, rgMode);
        if (resumeAt > 0)
            send(&detail::PlayerEngine::seek, resumeAt);
        if (resumeAs == PlaybackState::Playing) {
            target = PlaybackState::Playing;
            send(&detail::PlayerEngine::play);
            startDeadline = clock.elapsed() + kStartStallMs;
        } else if (resumeAs == PlaybackState::Paused) {
            target = PlaybackState::Paused;
            send(&detail::PlayerEngine::pause);
        }
    }
    Q_EMIT q->engineRecovered(reason);
}

void Player::Private::setState(PlaybackState s)
{
    if (state == s)
        return;
    state = s;
    Q_EMIT q->playbackStateChanged(s);
}

void Player::Private::setPosition(qint64 ms)
{
    if (position == ms)
        return;
    position = ms;
    Q_EMIT q->positionChanged(ms);
}

void Player::Private::setDuration(qint64 ms)
{
    if (duration == ms)
        return;
    duration = ms;
    Q_EMIT q->durationChanged(ms);
}

void Player::Private::setSeekable(bool s)
{
    if (seekable == s)
        return;
    seekable = s;
    Q_EMIT q->seekableChanged(s);
}

// ---------------------------------------------------------------------------

Player::Player(QObject *parent)
    : Player(PlayerOptions{}, parent)
{
}

Player::Player(const PlayerOptions &options, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>(this, options))
{
    d->clock.start();
    d->watchdog.setInterval(kWatchdogTickMs);
    connect(&d->watchdog, &QTimer::timeout, this, [this] { d->checkWatchdog(); });
    d->createEngine();
}

Player::~Player()
{
    d->watchdog.stop();
    d->releaseEngine(true);
}

QUrl Player::source() const { return d->source; }

void Player::setSource(const QUrl &url)
{
    ++d->epoch;
    {
        QMutexLocker lock(&d->gapless->mutex);
        d->gapless->epoch = d->epoch;
        d->gapless->upcoming.clear();
        d->gapless->committed.clear();
        d->gapless->lastQueuedUri = url;
    }
    d->target = PlaybackState::Stopped;
    d->startDeadline = -1;
    d->setState(PlaybackState::Stopped);
    d->setPosition(0);
    d->setDuration(-1);
    d->setSeekable(false);
    const bool changed = d->source != url;
    d->source = url;
    d->send(&detail::PlayerEngine::setSource, d->epoch, url, d->rgMode);
    if (changed)
        Q_EMIT sourceChanged(url);
}

void Player::setUpcomingSources(const QList<QUrl> &upcoming)
{
    QMutexLocker lock(&d->gapless->mutex);
    // Skip what's already been handed over (if the caller's list starts with it).
    qsizetype skip = 0;
    while (skip < d->gapless->committed.size() && skip < upcoming.size() && upcoming[skip] == d->gapless->committed[skip])
        ++skip;
    if (skip < d->gapless->committed.size()) {
        // The caller's order disagrees with what GStreamer already has; nothing can be taken
        // back. Queue the caller's list after it; PlaybackController corrects the order when
        // the committed track starts.
        skip = 0;
    }
    d->gapless->upcoming = upcoming.mid(skip);
}

QUrl Player::nextSource() const
{
    QMutexLocker lock(&d->gapless->mutex);
    return d->gapless->upcoming.value(0);
}

Player::ReplayGainMode Player::replayGainMode() const { return d->rgMode; }

void Player::setReplayGainMode(ReplayGainMode mode)
{
    if (d->rgMode == mode)
        return;
    d->rgMode = mode;
    d->send(&detail::PlayerEngine::setReplayGainMode, mode);
    Q_EMIT replayGainModeChanged(mode);
}

double Player::replayGainDb() const
{
    return d->rgMode == ReplayGainMode::Off ? 0.0 : d->gapless->replayGainDb.load();
}

Player::PlaybackState Player::playbackState() const { return d->state; }
qint64 Player::position() const { return d->position; }
qint64 Player::duration() const { return d->duration; }
bool Player::isSeekable() const { return d->seekable; }
double Player::volume() const { return d->volume; }

void Player::setVolume(double volume)
{
    volume = std::clamp(volume, 0.0, 1.0);
    if (qFuzzyCompare(1.0 + d->volume, 1.0 + volume))
        return;
    d->volume = volume;
    d->send(&detail::PlayerEngine::setVolume, volume);
    Q_EMIT volumeChanged(volume);
}

void Player::play()
{
    if (d->source.isEmpty())
        return;
    d->target = PlaybackState::Playing;
    d->send(&detail::PlayerEngine::play);
    if (d->state != PlaybackState::Playing) {
        d->startDeadline = d->clock.elapsed() + kStartStallMs;
        if (!d->watchdog.isActive())
            d->watchdog.start();
    }
}

void Player::pause()
{
    if (d->source.isEmpty())
        return;
    d->target = PlaybackState::Paused;
    d->startDeadline = -1;
    d->send(&detail::PlayerEngine::pause);
}

void Player::stop()
{
    ++d->epoch;
    {
        // Tracks already handed to playbin3 are cancelled by stopping; the upcoming list
        // stays (it's still what follows the current source when play() resumes it).
        QMutexLocker lock(&d->gapless->mutex);
        d->gapless->epoch = d->epoch;
        d->gapless->upcoming = d->gapless->committed + d->gapless->upcoming;
        d->gapless->committed.clear();
        d->gapless->lastQueuedUri = d->source;
    }
    d->target = PlaybackState::Stopped;
    d->startDeadline = -1;
    d->setPosition(0);
    d->setState(PlaybackState::Stopped);
    d->send(&detail::PlayerEngine::stop, d->epoch, d->source, d->rgMode);
}

void Player::seek(qint64 positionMs)
{
    positionMs = std::max<qint64>(positionMs, 0);
    if (d->duration >= 0)
        positionMs = std::min(positionMs, d->duration);
    d->setPosition(positionMs);
    d->send(&detail::PlayerEngine::seek, positionMs);
}

} // namespace mmm

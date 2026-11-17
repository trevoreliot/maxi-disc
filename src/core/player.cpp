// SPDX-License-Identifier: GPL-3.0-or-later
#include "player.h"

#include "gstinit.h"

#include <QLoggingCategory>
#include <QMetaObject>
#include <QMutex>
#include <QTimer>

#include <gst/gst.h>

#include <algorithm>
#include <atomic>
#include <memory>

Q_LOGGING_CATEGORY(lcPlayer, "mmm.player")

namespace mmm {

namespace {

constexpr int kPositionIntervalMs = 100;

// playbin3 "flags": audio only, software volume. (GstPlayFlags lives in a plugin header.)
constexpr guint kPlayFlagAudio = 1U << 1;
constexpr guint kPlayFlagSoftVolume = 1U << 4;

struct MessageUnref
{
    void operator()(GstMessage *m) const { gst_message_unref(m); }
};
// shared_ptr so it can be captured in a copyable lambda for a queued invocation.
using MessagePtr = std::shared_ptr<GstMessage>;

constexpr qint64 nsToMs(gint64 ns) { return ns / GST_MSECOND; }
constexpr gint64 msToNs(qint64 ms) { return ms * GST_MSECOND; }

} // namespace

struct Player::Private
{
    explicit Private(Player *q_) : q(q_) {}

    Player *q;
    GstElement *pipeline = nullptr;
    QTimer positionTimer;

    QUrl source;
    PlaybackState state = PlaybackState::Stopped;
    PlaybackState target = PlaybackState::Stopped; // what the user last asked for
    qint64 position = 0;
    qint64 duration = -1;
    bool seekable = false;
    bool prerolled = false;   // pipeline reached PAUSED/PLAYING for the current source
    qint64 pendingSeek = -1;
    double volume = 1.0;
    // Bumped whenever the current stream ends or is replaced (setSource, stop, EOS, error).
    // Each bus message is tagged with the generation it was posted in; messages from an
    // older generation are stale (e.g. an EOS or duplicate ERROR from the previous track)
    // and are ignored.
    std::atomic<quint64> generation{0};

    // Gapless: guarded by nextMutex (about-to-finish runs on a streaming thread).
    mutable QMutex nextMutex;
    QList<QUrl> upcoming;   // still to hand over, in order
    QList<QUrl> committed;  // handed to playbin3, not started yet; FIFO, popped per STREAM_START
    QUrl lastQueuedUri;     // the current source, or the last committed one
    // Whether the current source's own STREAM_START has been handled (UI thread only).
    // about-to-finish can commit the next URI before that first message is processed
    // (short local files are read instantly), so only *later* stream starts are advances.
    bool currentStreamStarted = false;

    ReplayGainMode rgMode = ReplayGainMode::Album;
    bool rgFilterActive = false;    // whether the pipeline currently has the rgvolume filter
    GstElement *rgVolume = nullptr; // owned ref while the filter is installed

    static GstBusSyncReply busSyncHandler(GstBus *bus, GstMessage *msg, gpointer self);
    static void onAboutToFinish(GstElement *playbin, gpointer self);
    void applyAudioFilter(); // only while the pipeline is in NULL/READY
    void handleMessage(GstMessage *msg);

    void setState(PlaybackState s);
    void setPosition(qint64 ms);
    void setDuration(qint64 ms);
    void setSeekable(bool s);
    void updatePosition();
    void queryDurationAndSeekable();
    void resetStreamInfo();
    void invalidatePendingMessages() { generation.fetch_add(1, std::memory_order_relaxed); }
    bool changeState(GstState s);
};

// Runs on GStreamer streaming threads (or the caller's thread). Forwards every message
// to the Player's thread. Returning GST_BUS_DROP transfers ownership of `msg` to us
// (see GstBusSyncHandler docs), so adopt that ref; the queued functor releases it,
// including when the event is discarded because the Player was destroyed.
GstBusSyncReply Player::Private::busSyncHandler(GstBus *, GstMessage *msg, gpointer self)
{
    auto *d = static_cast<Private *>(self);
    MessagePtr ref(msg, MessageUnref{});
    const quint64 gen = d->generation.load(std::memory_order_relaxed);
    QMetaObject::invokeMethod(
        d->q,
        [d, ref, gen] {
            if (gen == d->generation.load(std::memory_order_relaxed))
                d->handleMessage(ref.get());
        },
        Qt::QueuedConnection);
    return GST_BUS_DROP;
}

// Streaming thread: the current URI has been fully read. Setting "uri" now makes playbin3
// continue with it seamlessly.
void Player::Private::onAboutToFinish(GstElement *playbin, gpointer self)
{
    auto *d = static_cast<Private *>(self);
    QByteArray uri;
    {
        QMutexLocker lock(&d->nextMutex);
        if (d->upcoming.isEmpty() || d->upcoming.first() == d->lastQueuedUri)
            return; // nothing next, or the same file again (no self-gapless): end normally
        const QUrl next = d->upcoming.takeFirst();
        d->committed.append(next);
        d->lastQueuedUri = next;
        uri = next.toEncoded();
    }
    g_object_set(playbin, "uri", uri.constData(), nullptr);
}

void Player::Private::applyAudioFilter()
{
    const bool want = rgMode != ReplayGainMode::Off;
    if (want == rgFilterActive)
        return;
    if (rgVolume) {
        gst_object_unref(rgVolume);
        rgVolume = nullptr;
    }
    if (!want) {
        g_object_set(pipeline, "audio-filter", nullptr, nullptr);
        rgFilterActive = false;
        return;
    }
    GError *err = nullptr;
    GstElement *bin = gst_parse_bin_from_description("rgvolume name=rg ! rglimiter", TRUE, &err);
    if (!bin) {
        qCWarning(lcPlayer) << "ReplayGain unavailable (gst-plugins-good):" << (err ? err->message : "");
        g_clear_error(&err);
        return;
    }
    g_clear_error(&err);
    rgVolume = gst_bin_get_by_name(GST_BIN(bin), "rg"); // our ref
    g_object_set(rgVolume, "album-mode", rgMode == ReplayGainMode::Album, nullptr);
    g_object_set(pipeline, "audio-filter", bin, nullptr); // playbin takes the floating ref
    rgFilterActive = true;
}

void Player::Private::handleMessage(GstMessage *msg)
{
    const bool fromPipeline = GST_MESSAGE_SRC(msg) == GST_OBJECT(pipeline);

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS:
        changeState(GST_STATE_READY);
        invalidatePendingMessages();
        currentStreamStarted = false;
        target = PlaybackState::Stopped;
        prerolled = false;
        setPosition(0);
        setState(PlaybackState::Stopped);
        Q_EMIT q->endOfStream();
        break;

    case GST_MESSAGE_ERROR: {
        GError *err = nullptr;
        gchar *debug = nullptr;
        gst_message_parse_error(msg, &err, &debug);
        const QString text = QString::fromUtf8(err ? err->message : "unknown error");
        qCWarning(lcPlayer) << "GStreamer error:" << text << "|" << debug;
        g_clear_error(&err);
        g_free(debug);

        changeState(GST_STATE_NULL);
        invalidatePendingMessages(); // one errorOccurred per failure, not one per element
        currentStreamStarted = false;
        target = PlaybackState::Stopped;
        prerolled = false;
        pendingSeek = -1;
        setPosition(0);
        setState(PlaybackState::Stopped);
        Q_EMIT q->errorOccurred(text);
        break;
    }

    case GST_MESSAGE_WARNING: {
        GError *err = nullptr;
        gchar *debug = nullptr;
        gst_message_parse_warning(msg, &err, &debug);
        qCWarning(lcPlayer) << "GStreamer warning:" << (err ? err->message : "") << "|" << debug;
        g_clear_error(&err);
        g_free(debug);
        break;
    }

    case GST_MESSAGE_STATE_CHANGED: {
        if (!fromPipeline)
            break;
        GstState oldState, newState, pending;
        gst_message_parse_state_changed(msg, &oldState, &newState, &pending);
        if (newState == GST_STATE_PLAYING)
            setState(PlaybackState::Playing);
        else if (newState == GST_STATE_PAUSED && target == PlaybackState::Paused)
            setState(PlaybackState::Paused);
        else if (newState <= GST_STATE_READY)
            setState(PlaybackState::Stopped);
        break;
    }

    case GST_MESSAGE_ASYNC_DONE:
        if (!fromPipeline)
            break;
        prerolled = true;
        queryDurationAndSeekable();
        if (pendingSeek >= 0) {
            const qint64 ms = pendingSeek;
            pendingSeek = -1;
            q->seek(ms);
        } else {
            updatePosition();
        }
        break;

    case GST_MESSAGE_DURATION_CHANGED:
        queryDurationAndSeekable();
        break;

    case GST_MESSAGE_STREAM_START: {
        if (!fromPipeline)
            break;
        if (!currentStreamStarted) {
            currentStreamStarted = true; // the current source itself starting, never an advance
            break;
        }
        QUrl next;
        {
            QMutexLocker lock(&nextMutex);
            if (!committed.isEmpty())
                next = committed.takeFirst();
        }
        if (next.isEmpty())
            break;
        source = next;
        setPosition(0);
        setDuration(-1); // re-queried as soon as the new stream reports it
        queryDurationAndSeekable();
        Q_EMIT q->sourceChanged(source);
        Q_EMIT q->advancedToNext(source);
        break;
    }

    default:
        break;
    }
}

bool Player::Private::changeState(GstState s)
{
    if (gst_element_set_state(pipeline, s) == GST_STATE_CHANGE_FAILURE) {
        // The bus normally carries an ERROR message with details; that's what we report.
        qCWarning(lcPlayer) << "state change failed:" << gst_element_state_get_name(s);
        return false;
    }
    return true;
}

void Player::Private::setState(PlaybackState s)
{
    if (s == PlaybackState::Playing)
        positionTimer.start();
    else
        positionTimer.stop();

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

void Player::Private::updatePosition()
{
    if (duration < 0)
        queryDurationAndSeekable(); // after a gapless switch the duration arrives a bit later
    gint64 pos = 0;
    if (gst_element_query_position(pipeline, GST_FORMAT_TIME, &pos) && pos >= 0)
        setPosition(nsToMs(pos));
}

void Player::Private::queryDurationAndSeekable()
{
    gint64 dur = 0;
    if (gst_element_query_duration(pipeline, GST_FORMAT_TIME, &dur) && dur >= 0)
        setDuration(nsToMs(dur));

    GstQuery *query = gst_query_new_seeking(GST_FORMAT_TIME);
    if (gst_element_query(pipeline, query)) {
        gboolean canSeek = FALSE;
        gst_query_parse_seeking(query, nullptr, &canSeek, nullptr, nullptr);
        setSeekable(canSeek != FALSE);
    }
    gst_query_unref(query);
}

void Player::Private::resetStreamInfo()
{
    prerolled = false;
    pendingSeek = -1;
    setPosition(0);
    setDuration(-1);
    setSeekable(false);
}

// ---------------------------------------------------------------------------

Player::Player(QObject *parent)
    : Player(PlayerOptions{}, parent)
{
}

Player::Player(const PlayerOptions &options, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>(this))
{
    gst::ensureInitialized();

    d->pipeline = gst_element_factory_make("playbin3", "mmm-player");
    if (!d->pipeline)
        qFatal("GStreamer element 'playbin3' is missing (install gst-plugins-base)");
    gst_object_ref_sink(d->pipeline);

    g_object_set(d->pipeline, "flags", kPlayFlagAudio | kPlayFlagSoftVolume, nullptr);
    g_signal_connect(d->pipeline, "about-to-finish", G_CALLBACK(&Private::onAboutToFinish), d.get());
    d->applyAudioFilter();

    if (!options.audioSink.isEmpty()) {
        GError *err = nullptr;
        GstElement *sink = gst_parse_bin_from_description(options.audioSink.constData(), TRUE, &err);
        if (sink) {
            g_object_set(d->pipeline, "audio-sink", sink, nullptr); // playbin takes the floating ref
        } else {
            qCWarning(lcPlayer) << "invalid audio sink" << options.audioSink << ":"
                                << (err ? err->message : "");
        }
        g_clear_error(&err);
    }

    GstBus *bus = gst_element_get_bus(d->pipeline);
    gst_bus_set_sync_handler(bus, &Private::busSyncHandler, d.get(), nullptr);
    gst_object_unref(bus);

    d->positionTimer.setInterval(kPositionIntervalMs);
    connect(&d->positionTimer, &QTimer::timeout, this, [this] { d->updatePosition(); });
}

Player::~Player()
{
    d->positionTimer.stop();
    // NULL joins all streaming threads, so no bus callback can run after this.
    gst_element_set_state(d->pipeline, GST_STATE_NULL);
    GstBus *bus = gst_element_get_bus(d->pipeline);
    gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
    gst_object_unref(bus);
    if (d->rgVolume)
        gst_object_unref(d->rgVolume);
    gst_object_unref(d->pipeline);
    // Queued message functors still pending for `this` are discarded by ~QObject,
    // which releases their message refs.
}

QUrl Player::source() const { return d->source; }

void Player::setSource(const QUrl &url)
{
    d->changeState(GST_STATE_READY); // streaming threads stopped: no about-to-finish now
    d->invalidatePendingMessages();
    {
        QMutexLocker lock(&d->nextMutex);
        d->upcoming.clear();
        d->committed.clear();
        d->lastQueuedUri = url;
    }
    d->currentStreamStarted = false;
    d->applyAudioFilter();
    d->target = PlaybackState::Stopped;
    d->setState(PlaybackState::Stopped);
    d->resetStreamInfo();

    const bool changed = d->source != url;
    d->source = url;
    const QByteArray uri = url.toEncoded();
    g_object_set(d->pipeline, "uri", uri.isEmpty() ? nullptr : uri.constData(), nullptr);

    if (changed)
        Q_EMIT sourceChanged(url);
}

void Player::setUpcomingSources(const QList<QUrl> &upcoming)
{
    QMutexLocker lock(&d->nextMutex);
    // Skip what's already been handed over (if the caller's list starts with it).
    qsizetype skip = 0;
    while (skip < d->committed.size() && skip < upcoming.size() && upcoming[skip] == d->committed[skip])
        ++skip;
    if (skip < d->committed.size()) {
        // The caller's order disagrees with what GStreamer already has; nothing can be taken
        // back. Queue the caller's list after it; PlaybackController corrects the order when
        // the committed track starts.
        skip = 0;
    }
    d->upcoming = upcoming.mid(skip);
}

QUrl Player::nextSource() const
{
    QMutexLocker lock(&d->nextMutex);
    return d->upcoming.value(0);
}

Player::ReplayGainMode Player::replayGainMode() const { return d->rgMode; }

void Player::setReplayGainMode(ReplayGainMode mode)
{
    if (d->rgMode == mode)
        return;
    d->rgMode = mode;
    if (d->rgVolume && mode != ReplayGainMode::Off)
        g_object_set(d->rgVolume, "album-mode", mode == ReplayGainMode::Album, nullptr);
    if (d->state == PlaybackState::Stopped)
        d->applyAudioFilter(); // pipeline is at READY/NULL: safe to swap the filter
    Q_EMIT replayGainModeChanged(mode);
}

double Player::replayGainDb() const
{
    if (!d->rgVolume || d->rgMode == ReplayGainMode::Off)
        return 0.0;
    gdouble gain = 0.0;
    g_object_get(d->rgVolume, "result-gain", &gain, nullptr);
    return gain;
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
    g_object_set(d->pipeline, "volume", volume, nullptr);
    Q_EMIT volumeChanged(volume);
}

void Player::play()
{
    if (d->source.isEmpty())
        return;
    d->target = PlaybackState::Playing;
    d->changeState(GST_STATE_PLAYING);
}

void Player::pause()
{
    if (d->source.isEmpty())
        return;
    d->target = PlaybackState::Paused;
    if (d->changeState(GST_STATE_PAUSED) && d->state == PlaybackState::Playing) {
        // Already prerolled: no async transition, so report immediately.
        d->updatePosition();
    }
}

void Player::stop()
{
    d->target = PlaybackState::Stopped;
    d->changeState(GST_STATE_READY);
    d->invalidatePendingMessages();
    {
        // Tracks already handed to playbin3 are cancelled by stopping; the upcoming list
        // stays (it's still what follows the current source when play() resumes it).
        QMutexLocker lock(&d->nextMutex);
        d->upcoming = d->committed + d->upcoming;
        d->committed.clear();
        d->lastQueuedUri = d->source;
    }
    // Committing the next track overwrote playbin3's "uri"; point it back at the current
    // source, or play() after stop() would start the next track instead.
    const QByteArray uri = d->source.toEncoded();
    g_object_set(d->pipeline, "uri", uri.isEmpty() ? nullptr : uri.constData(), nullptr);
    d->currentStreamStarted = false;
    d->applyAudioFilter();
    d->prerolled = false;
    d->pendingSeek = -1;
    d->setPosition(0);
    d->setState(PlaybackState::Stopped);
}

void Player::seek(qint64 positionMs)
{
    positionMs = std::max<qint64>(positionMs, 0);
    if (d->duration >= 0)
        positionMs = std::min(positionMs, d->duration);

    if (!d->prerolled) {
        d->pendingSeek = positionMs; // applied (and seeked() emitted) once prerolled
        return;
    }
    const auto flags = static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE);
    if (gst_element_seek_simple(d->pipeline, GST_FORMAT_TIME, flags, msToNs(positionMs))) {
        d->setPosition(positionMs);
        Q_EMIT seeked(positionMs);
    } else
        qCWarning(lcPlayer) << "seek failed:" << positionMs << "ms";
}

} // namespace mmm

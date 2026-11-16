// SPDX-License-Identifier: GPL-3.0-or-later
#include "player.h"

#include "gstinit.h"

#include <QLoggingCategory>
#include <QMetaObject>
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

    static GstBusSyncReply busSyncHandler(GstBus *bus, GstMessage *msg, gpointer self);
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

void Player::Private::handleMessage(GstMessage *msg)
{
    const bool fromPipeline = GST_MESSAGE_SRC(msg) == GST_OBJECT(pipeline);

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS:
        changeState(GST_STATE_READY);
        invalidatePendingMessages();
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
    gst_object_unref(d->pipeline);
    // Queued message functors still pending for `this` are discarded by ~QObject,
    // which releases their message refs.
}

QUrl Player::source() const { return d->source; }

void Player::setSource(const QUrl &url)
{
    d->changeState(GST_STATE_READY);
    d->invalidatePendingMessages();
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
        d->pendingSeek = positionMs;
        return;
    }
    const auto flags = static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE);
    if (gst_element_seek_simple(d->pipeline, GST_FORMAT_TIME, flags, msToNs(positionMs)))
        d->setPosition(positionMs);
    else
        qCWarning(lcPlayer) << "seek failed:" << positionMs << "ms";
}

} // namespace mmm

// SPDX-License-Identifier: GPL-3.0-or-later
#include "playerengine.h"

#include "gstinit.h"

#include <QLoggingCategory>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include <gst/gst.h>

Q_DECLARE_LOGGING_CATEGORY(lcPlayer) // defined in player.cpp

namespace mmm::detail {

namespace {

constexpr int kPositionIntervalMs = 100;

// playbin3 "flags": audio only, software volume. (GstPlayFlags lives in a plugin header.)
constexpr guint kPlayFlagAudio = 1U << 1;
constexpr guint kPlayFlagSoftVolume = 1U << 4;

struct MessageUnref
{
    void operator()(GstMessage *m) const { gst_message_unref(m); }
};
using MessagePtr = std::shared_ptr<GstMessage>;

constexpr qint64 nsToMs(gint64 ns) { return ns / GST_MSECOND; }
constexpr gint64 msToNs(qint64 ms) { return ms * GST_MSECOND; }

} // namespace

// GStreamer callbacks with their exact C signatures (no function-pointer casts).
struct EngineCallbacks
{
    static GstBusSyncReply busSync(GstBus *bus, GstMessage *msg, gpointer self);
    static void aboutToFinish(GstElement *playbin, gpointer self);
};

PlayerEngine::PlayerEngine(const PlayerOptions &options, std::shared_ptr<GaplessState> gapless)
    : m_options(options)
    , m_gapless(std::move(gapless))
{
    gst::ensureInitialized();

    m_pipeline = gst_element_factory_make("playbin3", nullptr);
    if (!m_pipeline)
        qFatal("GStreamer element 'playbin3' is missing (install gst-plugins-base)");
    gst_object_ref_sink(m_pipeline);
    g_object_set(m_pipeline, "flags", kPlayFlagAudio | kPlayFlagSoftVolume, nullptr);
    g_signal_connect(m_pipeline, "about-to-finish", G_CALLBACK(&EngineCallbacks::aboutToFinish), this);
    applyAudioFilter();

    if (!options.audioSink.isEmpty()) {
        GError *err = nullptr;
        GstElement *sink = gst_parse_bin_from_description(options.audioSink.constData(), TRUE, &err);
        if (sink)
            g_object_set(m_pipeline, "audio-sink", sink, nullptr); // playbin takes the floating ref
        else
            qCWarning(lcPlayer) << "invalid audio sink" << options.audioSink << ":" << (err ? err->message : "");
        g_clear_error(&err);
    }

    GstBus *bus = gst_element_get_bus(m_pipeline);
    gst_bus_set_sync_handler(bus, &EngineCallbacks::busSync, this, nullptr);
    gst_object_unref(bus);

    // A child, so it moves to the engine thread with us.
    m_positionTimer = new QTimer(this);
    m_positionTimer->setInterval(kPositionIntervalMs);
    connect(m_positionTimer, &QTimer::timeout, this, &PlayerEngine::updatePosition);
}

PlayerEngine::~PlayerEngine()
{
    if (m_pipeline) {
        // Normally shutdown() already did this on the engine thread.
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        GstBus *bus = gst_element_get_bus(m_pipeline);
        gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
        gst_object_unref(bus);
        if (m_rgVolume)
            gst_object_unref(m_rgVolume);
        gst_object_unref(m_pipeline);
    }
}

// Streaming threads (or the engine thread). Forwards every message to the engine thread.
// Returning GST_BUS_DROP hands ownership of `msg` to us; the queued functor releases it,
// even when the event is discarded because the engine is gone.
GstBusSyncReply EngineCallbacks::busSync(GstBus *, GstMessage *msg, gpointer self)
{
    auto *e = static_cast<PlayerEngine *>(self);
    MessagePtr ref(msg, MessageUnref{});
    const quint64 gen = e->m_busGeneration.load();
    QMetaObject::invokeMethod(
        e,
        [e, ref, gen] {
            if (gen == e->m_busGeneration.load())
                e->handleMessage(ref.get());
        },
        Qt::QueuedConnection);
    return GST_BUS_DROP;
}

// Streaming thread: the current URI has been fully read; setting "uri" now continues gaplessly.
void EngineCallbacks::aboutToFinish(GstElement *playbin, gpointer self)
{
    auto *e = static_cast<PlayerEngine *>(self);
    if (e->m_abandoned.load())
        return;
    QByteArray uri;
    {
        GaplessState &g = *e->m_gapless;
        QMutexLocker lock(&g.mutex);
        if (g.epoch != e->m_streamEpoch.load())
            return; // the lists already belong to a newer stream
        if (g.upcoming.isEmpty() || g.upcoming.first() == g.lastQueuedUri)
            return; // nothing next, or the same file again (no self-gapless): end normally
        const QUrl next = g.upcoming.takeFirst();
        g.committed.append(next);
        g.lastQueuedUri = next;
        uri = next.toEncoded();
    }
    g_object_set(playbin, "uri", uri.constData(), nullptr);
}

bool PlayerEngine::skip(quint64)
{
    if (!m_abandoned.load())
        return false;
    shutdown();
    return true;
}

bool PlayerEngine::changeState(int gstState)
{
    if (m_options.beforeStateChange)
        m_options.beforeStateChange(gstState); // test hook: may block to simulate a stall
    const auto s = static_cast<GstState>(gstState);
    if (gst_element_set_state(m_pipeline, s) == GST_STATE_CHANGE_FAILURE) {
        // The bus normally carries an ERROR with details; that's what we report.
        qCWarning(lcPlayer) << "state change failed:" << gst_element_state_get_name(s);
        return false;
    }
    return true;
}

void PlayerEngine::applyAudioFilter()
{
    const bool want = m_rgMode != Player::ReplayGainMode::Off;
    if (want == m_rgFilterActive)
        return;
    if (m_rgVolume) {
        gst_object_unref(m_rgVolume);
        m_rgVolume = nullptr;
    }
    if (!want) {
        g_object_set(m_pipeline, "audio-filter", nullptr, nullptr);
        m_rgFilterActive = false;
        m_gapless->replayGainDb.store(0.0);
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
    m_rgVolume = gst_bin_get_by_name(GST_BIN(bin), "rg"); // our ref
    g_object_set(m_rgVolume, "album-mode", m_rgMode == Player::ReplayGainMode::Album, nullptr);
    g_object_set(m_pipeline, "audio-filter", bin, nullptr); // playbin takes the floating ref
    m_rgFilterActive = true;
}

void PlayerEngine::setEngineState(Player::PlaybackState s)
{
    if (s == Player::PlaybackState::Playing)
        m_positionTimer->start();
    else
        m_positionTimer->stop();
    if (m_state == s)
        return;
    m_state = s;
    Q_EMIT stateChanged(m_streamEpoch.load(), s);
}

void PlayerEngine::updatePosition()
{
    if (m_duration < 0)
        queryDurationAndSeekable(); // after a gapless switch the duration arrives a bit later
    gint64 pos = 0;
    if (gst_element_query_position(m_pipeline, GST_FORMAT_TIME, &pos) && pos >= 0 && nsToMs(pos) != m_position) {
        m_position = nsToMs(pos);
        Q_EMIT positionChanged(m_streamEpoch.load(), m_position);
    }
    if (m_rgVolume) {
        gdouble gain = 0.0;
        g_object_get(m_rgVolume, "result-gain", &gain, nullptr);
        m_gapless->replayGainDb.store(gain);
    }
}

void PlayerEngine::queryDurationAndSeekable()
{
    gint64 dur = 0;
    if (gst_element_query_duration(m_pipeline, GST_FORMAT_TIME, &dur) && dur >= 0 && nsToMs(dur) != m_duration) {
        m_duration = nsToMs(dur);
        Q_EMIT durationChanged(m_streamEpoch.load(), m_duration);
    }
    GstQuery *query = gst_query_new_seeking(GST_FORMAT_TIME);
    if (gst_element_query(m_pipeline, query)) {
        gboolean canSeek = FALSE;
        gst_query_parse_seeking(query, nullptr, &canSeek, nullptr, nullptr);
        if ((canSeek != FALSE) != m_seekable) {
            m_seekable = canSeek != FALSE;
            Q_EMIT seekableChanged(m_streamEpoch.load(), m_seekable);
        }
    }
    gst_query_unref(query);
}

void PlayerEngine::doSeek(qint64 ms)
{
    if (!m_prerolled) {
        m_pendingSeek = ms; // applied once prerolled
        return;
    }
    const auto flags = static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE);
    if (gst_element_seek_simple(m_pipeline, GST_FORMAT_TIME, flags, msToNs(ms))) {
        m_position = ms;
        Q_EMIT seeked(m_streamEpoch.load(), ms);
    } else {
        qCWarning(lcPlayer) << "seek failed:" << ms << "ms";
    }
}

void PlayerEngine::resetStream(quint64 epoch)
{
    m_busGeneration.fetch_add(1); // drop queued messages from the previous stream
    m_streamEpoch.store(epoch);
    m_currentStreamStarted = false;
    m_prerolled = false;
    m_pendingSeek = -1;
    m_position = -1;
    m_duration = -1;
    m_seekable = false;
    m_target = Player::PlaybackState::Stopped;
    m_state = Player::PlaybackState::Stopped; // Player already shows Stopped
    m_positionTimer->stop();
}

void PlayerEngine::handleMessage(GstMessage *msg)
{
    if (m_abandoned.load())
        return;
    const bool fromPipeline = GST_MESSAGE_SRC(msg) == GST_OBJECT(m_pipeline);
    const quint64 epoch = m_streamEpoch.load();

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS:
        changeState(GST_STATE_READY);
        m_busGeneration.fetch_add(1);
        m_currentStreamStarted = false;
        m_target = Player::PlaybackState::Stopped;
        m_prerolled = false;
        m_state = Player::PlaybackState::Stopped;
        m_positionTimer->stop();
        Q_EMIT endOfStream(epoch);
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
        m_busGeneration.fetch_add(1); // one error per failure, not one per element
        m_currentStreamStarted = false;
        m_target = Player::PlaybackState::Stopped;
        m_prerolled = false;
        m_pendingSeek = -1;
        m_state = Player::PlaybackState::Stopped;
        m_positionTimer->stop();
        Q_EMIT errorOccurred(epoch, text);
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
            setEngineState(Player::PlaybackState::Playing);
        else if (newState == GST_STATE_PAUSED && m_target == Player::PlaybackState::Paused)
            setEngineState(Player::PlaybackState::Paused);
        else if (newState <= GST_STATE_READY)
            setEngineState(Player::PlaybackState::Stopped);
        break;
    }

    case GST_MESSAGE_ASYNC_DONE:
        if (!fromPipeline)
            break;
        m_prerolled = true;
        queryDurationAndSeekable();
        if (m_pendingSeek >= 0) {
            const qint64 ms = m_pendingSeek;
            m_pendingSeek = -1;
            doSeek(ms);
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
        if (!m_currentStreamStarted) {
            m_currentStreamStarted = true; // the current source itself starting: never an advance
            break;
        }
        QUrl next;
        {
            QMutexLocker lock(&m_gapless->mutex);
            if (!m_gapless->committed.isEmpty())
                next = m_gapless->committed.takeFirst();
        }
        if (next.isEmpty())
            break;
        m_position = 0;
        m_duration = -1; // re-queried as soon as the new stream reports it
        Q_EMIT advanced(epoch, next);
        queryDurationAndSeekable();
        break;
    }

    default:
        break;
    }
}

// --- Commands (engine thread) ----------------------------------------------

void PlayerEngine::setSource(quint64 seq, quint64 epoch, const QUrl &url, Player::ReplayGainMode rg)
{
    if (skip(seq))
        return;
    changeState(GST_STATE_READY); // streaming threads stopped: no about-to-finish now
    resetStream(epoch);
    {
        // A late about-to-finish from the old stream may have committed something (only if
        // the epochs matched, i.e. never for this new epoch); clear it to be sure.
        QMutexLocker lock(&m_gapless->mutex);
        if (m_gapless->epoch == epoch)
            m_gapless->committed.clear();
    }
    m_rgMode = rg;
    applyAudioFilter();
    const QByteArray uri = url.toEncoded();
    g_object_set(m_pipeline, "uri", uri.isEmpty() ? nullptr : uri.constData(), nullptr);
    Q_EMIT commandDone(seq);
}

void PlayerEngine::play(quint64 seq)
{
    if (skip(seq))
        return;
    m_target = Player::PlaybackState::Playing;
    changeState(GST_STATE_PLAYING);
    Q_EMIT commandDone(seq);
}

void PlayerEngine::pause(quint64 seq)
{
    if (skip(seq))
        return;
    m_target = Player::PlaybackState::Paused;
    if (changeState(GST_STATE_PAUSED) && m_state == Player::PlaybackState::Playing)
        updatePosition(); // already prerolled: report where we stopped right away
    Q_EMIT commandDone(seq);
}

void PlayerEngine::stop(quint64 seq, quint64 epoch, const QUrl &current, Player::ReplayGainMode rg)
{
    if (skip(seq))
        return;
    changeState(GST_STATE_READY);
    resetStream(epoch);
    // Committing the next track overwrote playbin3's "uri"; point it back at the current
    // source, or play() after stop() would start the next track instead.
    const QByteArray uri = current.toEncoded();
    g_object_set(m_pipeline, "uri", uri.isEmpty() ? nullptr : uri.constData(), nullptr);
    m_rgMode = rg;
    applyAudioFilter();
    Q_EMIT commandDone(seq);
}

void PlayerEngine::seek(quint64 seq, qint64 positionMs)
{
    if (skip(seq))
        return;
    doSeek(positionMs);
    Q_EMIT commandDone(seq);
}

void PlayerEngine::setVolume(quint64 seq, double volume)
{
    if (skip(seq))
        return;
    g_object_set(m_pipeline, "volume", volume, nullptr);
    Q_EMIT commandDone(seq);
}

void PlayerEngine::setReplayGainMode(quint64 seq, Player::ReplayGainMode mode)
{
    if (skip(seq))
        return;
    m_rgMode = mode;
    if (m_rgVolume && mode != Player::ReplayGainMode::Off)
        g_object_set(m_rgVolume, "album-mode", mode == Player::ReplayGainMode::Album, nullptr);
    if (m_state == Player::PlaybackState::Stopped)
        applyAudioFilter(); // pipeline is at READY/NULL: safe to swap the filter
    Q_EMIT commandDone(seq);
}

void PlayerEngine::shutdown()
{
    if (m_pipeline) {
        m_positionTimer->stop();
        // NULL joins all streaming threads, so no bus callback can run after this. (If this
        // blocks forever, the engine was stalled: Player has already moved on without it.)
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        GstBus *bus = gst_element_get_bus(m_pipeline);
        gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
        gst_object_unref(bus);
        if (m_rgVolume) {
            gst_object_unref(m_rgVolume);
            m_rgVolume = nullptr;
        }
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
    }
    QThread::currentThread()->quit();
}

} // namespace mmm::detail

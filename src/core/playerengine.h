// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to Player: not part of mmm_core's public API.

#include "player.h"
#include "spectrum.h"

#include <QList>
#include <QMutex>
#include <QObject>
#include <QUrl>

#include <atomic>
#include <deque>
#include <memory>
#include <vector>

class QTimer;
typedef struct _GstElement GstElement;
typedef struct _GstMessage GstMessage;
typedef struct _GstBus GstBus;

namespace mmm::detail {

// Gapless bookkeeping shared between Player (UI thread), the engine thread and GStreamer's
// streaming thread (about-to-finish). Guarded by `mutex`.
struct GaplessState
{
    QMutex mutex;
    QList<QUrl> upcoming;   // still to hand over, in order
    QList<QUrl> committed;  // handed to playbin3, not started yet (FIFO, popped per STREAM_START)
    QUrl lastQueuedUri;     // the current source, or the last committed one
    // The stream these lists belong to (Player bumps it on setSource/stop). about-to-finish
    // only commits when the engine is playing that same stream, so a late callback from the
    // previous stream can't take an item meant for the new one.
    quint64 epoch = 0;

    std::atomic<double> replayGainDb{0.0}; // written by the engine, read by Player
};

// Owns one playbin3 pipeline and runs every GStreamer call on its own thread (see
// docs/decisions/0001). Commands arrive as queued slot calls with a sequence number and are
// acknowledged with commandDone(seq). Events carry the stream epoch they belong to.
// If Player decides the engine has stalled it abandons it: the engine then shuts down
// (if it ever gets the chance) and never talks to that Player again.
class PlayerEngine : public QObject
{
    Q_OBJECT

public:
    PlayerEngine(const PlayerOptions &options, std::shared_ptr<GaplessState> gapless);
    ~PlayerEngine() override;

    // Any thread: from now on, ignore commands and only shut down.
    void abandon() { m_abandoned.store(true); }

public Q_SLOTS:
    void setSource(quint64 seq, quint64 epoch, const QUrl &url, mmm::Player::ReplayGainMode rg);
    void play(quint64 seq);
    void pause(quint64 seq);
    void stop(quint64 seq, quint64 epoch, const QUrl &current, mmm::Player::ReplayGainMode rg);
    void seek(quint64 seq, qint64 positionMs);
    void setVolume(quint64 seq, double volume);
    void setReplayGainMode(quint64 seq, mmm::Player::ReplayGainMode mode);
    // Q3: the analyser's `spectrum` element posts (and spectrumLevels is emitted) only while active.
    void setSpectrumActive(quint64 seq, bool active);
    // Tears the pipeline down and quits the engine's thread.
    void shutdown();

Q_SIGNALS:
    void commandDone(quint64 seq);
    void stateChanged(quint64 epoch, mmm::Player::PlaybackState state);
    void positionChanged(quint64 epoch, qint64 positionMs);
    void durationChanged(quint64 epoch, qint64 durationMs);
    void seekableChanged(quint64 epoch, bool seekable);
    void seeked(quint64 epoch, qint64 positionMs);
    void advanced(quint64 epoch, const QUrl &url);
    void endOfStream(quint64 epoch);
    void errorOccurred(quint64 epoch, const QString &message);
    // Spectrum bands of the audio being heard now (each result is held until its running-time
    // is reached on the pipeline clock, so the bars don't run ahead of the sound).
    void spectrumLevels(quint64 epoch, const mmm::SpectrumLevels &levels);

private:
    friend struct EngineCallbacks; // GStreamer callbacks (exact C signatures, in the .cpp)
    // `spectrumRate`: for a spectrum message, the rate it analysed (read where it was posted).
    void handleMessage(GstMessage *msg, quint64 spectrumGeneration, int spectrumRate);
    void handleSpectrum(GstMessage *msg, quint64 spectrumGeneration, int sampleRate);
    void flushDueSpectrum();
    // Drops held results and any still on their way (they belong to audio that won't be heard).
    void resetSpectrum();
    bool changeState(int gstState);
    void applyAudioFilter();
    void releaseFilter();
    void setEngineState(Player::PlaybackState s);
    void updatePosition();
    void queryDurationAndSeekable();
    void doSeek(qint64 ms);
    void resetStream(quint64 epoch);
    bool skip(quint64 seq); // true when abandoned (and acks nothing)

    PlayerOptions m_options;
    std::shared_ptr<GaplessState> m_gapless;
    std::atomic<bool> m_abandoned{false};

    GstElement *m_pipeline = nullptr;
    // The audio-filter bin: [rgvolume ! rglimiter !] [audioconvert ! capsfilter ! spectrum].
    GstElement *m_rgVolume = nullptr; // our ref while the ReplayGain part is installed
    GstElement *m_spectrum = nullptr; // our ref while the spectrum part is installed
    bool m_filterInstalled = false;
    bool m_rgFilterActive = false;
    bool m_haveReplayGain = false; // the elements exist (gst-plugins-good)
    bool m_haveSpectrum = false;
    Player::ReplayGainMode m_rgMode = Player::ReplayGainMode::Album;

    struct PendingSpectrum
    {
        quint64 heardAtNs; // running-time when the analysed audio has all been heard
        SpectrumLevels levels;
    };
    std::atomic<bool> m_spectrumActive{false};      // read by busSync (streaming threads)
    std::atomic<quint64> m_spectrumGeneration{0};  // bumped by resetSpectrum()
    std::deque<PendingSpectrum> m_pendingSpectrum;  // oldest first
    std::vector<float> m_spectrumBins;              // reused for each message
    QTimer *m_spectrumTimer = nullptr;
    // From a seek until playback runs again: the pipeline re-prerolls while still "playing" with
    // the old base time, so clock − base_time isn't the new running-time yet. Results wait.
    bool m_spectrumSeeking = false;

    QTimer *m_positionTimer = nullptr;
    std::atomic<quint64> m_streamEpoch{0}; // read by about-to-finish (streaming thread)
    std::atomic<quint64> m_busGeneration{0};
    Player::PlaybackState m_target = Player::PlaybackState::Stopped;
    Player::PlaybackState m_state = Player::PlaybackState::Stopped;
    bool m_prerolled = false;
    bool m_currentStreamStarted = false;
    qint64 m_pendingSeek = -1;
    qint64 m_position = -1;
    qint64 m_duration = -1;
    bool m_seekable = false;
};

} // namespace mmm::detail

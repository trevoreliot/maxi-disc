// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "spectrum.h"

#include <QByteArray>
#include <QObject>
#include <QUrl>

#include <functional>
#include <memory>

namespace mmm {

struct PlayerOptions
{
    // gst-launch style description of the audio sink, e.g. "fakesink sync=true".
    // Empty = GStreamer's default (autoaudiosink → PipeWire/Pulse/ALSA).
    QByteArray audioSink{};

    // Test hook: called on the engine thread just before every GStreamer state change, with
    // the target GstState as an int. Tests block in it to simulate a GStreamer stall.
    std::function<void(int gstState)> beforeStateChange{};
};

// Audio player wrapping GStreamer playbin3, with gapless continuation (setUpcomingSources).
// Lives on (and must be used from) the thread that created it. No call ever blocks on
// GStreamer: the pipeline lives on an internal engine thread, getters return cached state
// that the engine keeps up to date, and commands are queued to it. If the engine stalls
// (a GStreamer deadlock: docs/decisions/0001), it is abandoned and a fresh one resumes the
// current track where it was; engineRecovered() is emitted. Times are in milliseconds.
class Player : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged FINAL)
    Q_PROPERTY(PlaybackState playbackState READ playbackState NOTIFY playbackStateChanged FINAL)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged FINAL)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged FINAL)
    Q_PROPERTY(bool seekable READ isSeekable NOTIFY seekableChanged FINAL)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged FINAL)
    Q_PROPERTY(ReplayGainMode replayGainMode READ replayGainMode WRITE setReplayGainMode
                   NOTIFY replayGainModeChanged FINAL)
    Q_PROPERTY(bool analyzerActive READ analyzerActive WRITE setAnalyzerActive NOTIFY analyzerActiveChanged FINAL)

public:
    enum class PlaybackState { Stopped, Playing, Paused };
    Q_ENUM(PlaybackState)

    // Off: no gain. Track/Album: apply the file's REPLAYGAIN_* tags (album falls back to
    // track gain when there's no album gain), with a limiter to prevent clipping.
    enum class ReplayGainMode { Off, Track, Album };
    Q_ENUM(ReplayGainMode)

    explicit Player(QObject *parent = nullptr);
    explicit Player(const PlayerOptions &options, QObject *parent = nullptr);
    ~Player() override;

    Player(const Player &) = delete;
    Player &operator=(const Player &) = delete;

    QUrl source() const;
    // Stops playback and loads `url` (local file or any GStreamer URI). Does not autoplay.
    // Also discards any next source.
    void setSource(const QUrl &url);

    // Gapless playback: the sources that should follow the current one, in order. Each is
    // handed to GStreamer when the previous one has been fully read (playbin3
    // "about-to-finish", on a streaming thread) and starts with no gap; advancedToNext() is
    // emitted at each actual boundary. The whole list is given up front because for short
    // tracks GStreamer may need the one after next before the UI thread has even seen the
    // next one start. Sources already handed over can't be taken back; if `upcoming` begins
    // with them they're skipped, so callers can always pass "everything after the current
    // track". A source equal to the one before it is not chained (self-gapless is unreliable
    // in playbin3): playback ends there and the caller restarts it. Empty = stop at the end.
    void setUpcomingSources(const QList<QUrl> &upcoming);
    void setNextSource(const QUrl &url) { setUpcomingSources(url.isEmpty() ? QList<QUrl>{} : QList<QUrl>{url}); }
    // First source still waiting to be handed over (empty if none).
    QUrl nextSource() const;

    PlaybackState playbackState() const;
    qint64 position() const;  // ms
    qint64 duration() const;  // ms, -1 if unknown
    bool isSeekable() const;

    double volume() const;    // linear 0.0–1.0
    void setVolume(double volume);

    // Track ↔ Album switches immediately; turning ReplayGain on or off takes effect at the
    // next setSource()/stop() (GStreamer only allows changing the filter chain then).
    ReplayGainMode replayGainMode() const;
    void setReplayGainMode(ReplayGainMode mode);
    // Gain currently applied by ReplayGain, in dB (0 when off or untagged).
    double replayGainDb() const;

    // Q3: while active, spectrumLevels() reports the audio being heard, ~30 times a second while
    // playing (an analyser in the filter chain before the volume, so volume doesn't change it).
    // Off by default; switches immediately; a replaced engine keeps it.
    bool analyzerActive() const;
    void setAnalyzerActive(bool active);

public Q_SLOTS:
    void play();
    void pause();
    void stop();
    // Seeks to `positionMs`. If the stream isn't ready yet the seek is applied once it is.
    void seek(qint64 positionMs);

Q_SIGNALS:
    void sourceChanged(const QUrl &source);
    void playbackStateChanged(mmm::Player::PlaybackState state);
    void positionChanged(qint64 positionMs);
    // An explicit seek moved the position (not normal playback progress). For MPRIS Seeked.
    void seeked(qint64 positionMs);
    void durationChanged(qint64 durationMs);
    void seekableChanged(bool seekable);
    void volumeChanged(double volume);
    void replayGainModeChanged(mmm::Player::ReplayGainMode mode);
    void analyzerActiveChanged(bool active);
    // The spectrum of what's being heard now (only while analyzerActive and playing).
    void spectrumLevels(const mmm::SpectrumLevels &levels);
    // The next source started playing gaplessly; source() is now `url` (sourceChanged
    // is emitted too). Not emitted for setSource().
    void advancedToNext(const QUrl &url);
    // The last source ended and there was no next source.
    void endOfStream();
    void errorOccurred(const QString &message);
    // The engine stalled and was replaced; playback resumes where it was. For logging/tests.
    void engineRecovered(const QString &reason);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace mmm

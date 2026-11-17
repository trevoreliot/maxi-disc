// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QObject>
#include <QUrl>

#include <memory>

namespace mmm {

struct PlayerOptions
{
    // gst-launch style description of the audio sink, e.g. "fakesink sync=true".
    // Empty = GStreamer's default (autoaudiosink → PipeWire/Pulse/ALSA).
    QByteArray audioSink;
};

// Single-track audio player wrapping GStreamer playbin3.
// Lives on (and must be used from) the thread that created it; GStreamer bus messages
// are marshalled onto that thread. Times are in milliseconds.
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

    // Gapless playback: the source to continue with when the current one ends. It is handed
    // to GStreamer shortly before the end (playbin3 "about-to-finish") and starts with no
    // gap; advancedToNext() is emitted at the actual boundary. Once handed over it can no
    // longer be changed for this track; an empty URL means "stop at the end".
    void setNextSource(const QUrl &url);
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
    // The next source started playing gaplessly; source() is now `url` (sourceChanged
    // is emitted too). Not emitted for setSource().
    void advancedToNext(const QUrl &url);
    // The last source ended and there was no next source.
    void endOfStream();
    void errorOccurred(const QString &message);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace mmm

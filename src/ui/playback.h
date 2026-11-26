// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QtQmlIntegration/qqmlintegration.h>

#include <deque>
#include <memory>

#include "playbackcontroller.h"

class QQmlEngine;
class QJSEngine;
class TrackListModel;

namespace mmm {
class MprisService;
}

// Makes the core PlayQueue (Playback.queue) a known QML type, so its properties are typed.
struct PlayQueueForeign
{
    Q_GADGET
    QML_FOREIGN(mmm::PlayQueue)
    QML_NAMED_ELEMENT(PlayQueue)
    QML_UNCREATABLE("Use Playback.queue")
};

// Playback as seen by QML (singleton `Playback`): the PlaybackController's state flattened
// into properties, plus commands. Also owns the MPRIS service. Volume, repeat mode and
// ReplayGain mode persist (QSettings). Create it before the QML engine, like Library.
class Playback : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool playing READ isPlaying NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool paused READ isPaused NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY currentChanged FINAL)
    Q_PROPERTY(int currentTrackId READ currentTrackId NOTIFY currentChanged FINAL)
    // The current track's file and album (Q2: the now-playing menu); "" / 0 with none.
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentChanged FINAL)
    Q_PROPERTY(int currentAlbumId READ currentAlbumId NOTIFY currentChanged FINAL)
    Q_PROPERTY(QString title READ title NOTIFY currentChanged FINAL)
    Q_PROPERTY(QString artist READ artist NOTIFY currentChanged FINAL)
    Q_PROPERTY(QString album READ album NOTIFY currentChanged FINAL)
    Q_PROPERTY(QString coverUrl READ coverUrl NOTIFY currentChanged FINAL)
    // Q4, the transport bar's disc: the cover cut round, and the part of the cover URL that only
    // changes with the picture (coverUrl changes per track when the art is embedded).
    Q_PROPERTY(QString discCoverUrl READ discCoverUrl NOTIFY currentChanged FINAL)
    Q_PROPERTY(QString coverKey READ coverKey NOTIFY currentChanged FINAL)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged FINAL)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged FINAL)
    Q_PROPERTY(bool seekable READ isSeekable NOTIFY seekableChanged FINAL)
    Q_PROPERTY(bool canGoNext READ canGoNext NOTIFY navigationChanged FINAL)
    Q_PROPERTY(bool canGoPrevious READ canGoPrevious NOTIFY navigationChanged FINAL)
    // 0..1 slider position; the Player gets a cubic (perceptual) mapping of it.
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged FINAL)
    // 0 off, 1 all, 2 one (PlayQueue::RepeatMode)
    Q_PROPERTY(int repeatMode READ repeatMode WRITE setRepeatMode NOTIFY repeatModeChanged FINAL)
    // 0 off, 1 track, 2 album (Player::ReplayGainMode)
    Q_PROPERTY(int replayGainMode READ replayGainMode WRITE setReplayGainMode NOTIFY replayGainModeChanged FINAL)
    Q_PROPERTY(mmm::PlayQueue *queue READ queue CONSTANT FINAL)
    // Q3, the transport bar's analyser: kSpectrumBandCount band levels (0..1) of what's being
    // heard, ~30 times a second while playing; all 0 when paused, stopped or off.
    Q_PROPERTY(QList<qreal> levels READ levels NOTIFY levelsChanged FINAL)
    // The user's on/off switch (persisted).
    Q_PROPERTY(bool analyzerEnabled READ analyzerEnabled WRITE setAnalyzerEnabled NOTIFY analyzerEnabledChanged FINAL)
    // Main sets it: false while the window is minimised or hidden, so nothing is analysed or
    // animated for nobody (the analyser, Q4's disc).
    Q_PROPERTY(bool windowVisible READ windowVisible WRITE setWindowVisible NOTIFY windowVisibleChanged FINAL)
    // Q4: the disc spins while playing (persisted; off = it stays still).
    Q_PROPERTY(bool discSpinEnabled READ discSpinEnabled WRITE setDiscSpinEnabled NOTIFY discSpinEnabledChanged FINAL)

public:
    struct Options
    {
        mmm::PlayerOptions player;
        bool mpris = true;
        bool persistSettings = true;
    };

    explicit Playback(const Options &options, QObject *parent = nullptr);
    ~Playback() override;

    static Playback *instance();
    static Playback *create(QQmlEngine *, QJSEngine *);

    mmm::PlaybackController *controller() { return &m_controller; }
    mmm::MprisService *mpris() { return m_mpris.get(); }

    bool isPlaying() const;
    bool isPaused() const;
    bool hasTrack() const;
    int currentTrackId() const;
    QString currentPath() const { return hasTrack() ? m_controller.currentTrack()->path : QString(); }
    int currentAlbumId() const { return hasTrack() ? static_cast<int>(m_controller.currentTrack()->albumId) : 0; }
    QString title() const;
    QString artist() const;
    QString album() const;
    QString coverUrl() const;
    QString discCoverUrl() const;
    QString coverKey() const;
    qint64 position() const;
    qint64 duration() const;
    bool isSeekable() const;
    bool canGoNext() const;
    bool canGoPrevious() const;
    double volume() const;
    void setVolume(double sliderValue);
    int repeatMode() const;
    void setRepeatMode(int mode);
    int replayGainMode() const;
    void setReplayGainMode(int mode);
    mmm::PlayQueue *queue() { return m_controller.queue(); }
    QList<qreal> levels() const { return m_levels; }
    bool analyzerEnabled() const { return m_analyzerEnabled; }
    void setAnalyzerEnabled(bool enabled);
    bool windowVisible() const { return m_windowVisible; }
    void setWindowVisible(bool visible);
    bool discSpinEnabled() const { return m_discSpinEnabled; }
    void setDiscSpinEnabled(bool enabled);

    // Play `model`'s rows (replacing the queue), starting at `index`.
    Q_INVOKABLE void playFromModel(TrackListModel *model, int index = 0);
    // Add `model`'s rows (or just row `index` if >= 0) to the queue: at the end, or right after
    // the current track with `playNext`.
    Q_INVOKABLE void enqueueFromModel(TrackListModel *model, bool playNext = false, int index = -1);
    // Some of `model`'s rows (the marked tracks), in list order whatever order they're given in.
    Q_INVOKABLE void enqueueRows(TrackListModel *model, const QList<int> &rows, bool playNext = false);
    // What a browse row, album tile or lone track stands for: kind "artist", "genre", "album",
    // "playlist" or "track" + its id. Its tracks are read on the library's query pool; requests
    // apply in the order they were made (a Play then an Add: the Add lands in the new queue).
    // An explicit replace (playFromModel, clearQueue) drops the ones still loading.
    Q_INVOKABLE void playItem(const QString &kind, int id);
    Q_INVOKABLE void enqueueItem(const QString &kind, int id, bool playNext = false);
    // Moves queue row `index` to play right after the current track.
    Q_INVOKABLE void moveToPlayNext(int index);
    Q_INVOKABLE void playPause();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(qint64 positionMs);
    Q_INVOKABLE void playQueueIndex(int index);
    Q_INVOKABLE void removeFromQueue(int index);
    Q_INVOKABLE void moveInQueue(int from, int to);
    Q_INVOKABLE void clearQueue();
    Q_INVOKABLE void cycleRepeatMode();
    // Saves the queue as a new playlist; returns its id (0 on failure).
    Q_INVOKABLE int saveQueueAsPlaylist(const QString &name);

Q_SIGNALS:
    void stateChanged();
    void currentChanged();
    void positionChanged();
    void durationChanged();
    void seekableChanged();
    void navigationChanged();
    void volumeChanged();
    void repeatModeChanged();
    void replayGainModeChanged();
    void levelsChanged();
    void analyzerEnabledChanged();
    void windowVisibleChanged();
    void discSpinEnabledChanged();
    void raiseRequested(); // MPRIS Raise
    void quitRequested();  // MPRIS Quit

private:
    struct Pending
    {
        enum Op { Play, Append, Next } op = Append;
        bool ready = false;
        QList<mmm::TrackRow> rows;
    };
    void saveSettings() const;
    void updateAnalyzer();
    void setLevels(const mmm::SpectrumLevels &levels);
    void clearLevels();
    void request(Pending::Op op, const QString &kind, int id);
    void add(Pending::Op op, const QList<mmm::TrackRow> &rows); // in turn after the pending ones
    void flushPending();
    void dropPending();

    mmm::PlaybackController m_controller;
    std::unique_ptr<mmm::MprisService> m_mpris;
    bool m_persist;
    std::deque<std::shared_ptr<Pending>> m_pending; // oldest first
    quint64 m_pendingEpoch = 0;
    QList<qreal> m_levels;
    bool m_analyzerEnabled = true;
    bool m_windowVisible = true;
    bool m_discSpinEnabled = true;
};

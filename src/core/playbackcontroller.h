// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "libraryquery.h"
#include "player.h"
#include "playqueue.h"

#include <QObject>

namespace mmm {

// Plays the PlayQueue through the Player: starts the current track, keeps the Player's
// gapless next source in sync with the queue, advances on track boundaries, and skips
// tracks that fail to play. This is the one place the UI, MPRIS (and later the Emacs
// D-Bus interface) send playback commands to.
class PlaybackController : public QObject
{
    Q_OBJECT

public:
    explicit PlaybackController(const PlayerOptions &options = {}, QObject *parent = nullptr);
    ~PlaybackController() override;

    Player *player() { return &m_player; }
    const Player *player() const { return &m_player; }
    PlayQueue *queue() { return &m_queue; }
    const PlayQueue *queue() const { return &m_queue; }

    // Track currently loaded in the Player (null when nothing is).
    const TrackRow *currentTrack() const { return m_queue.current(); }
    bool canGoNext() const;
    bool canGoPrevious() const;

public Q_SLOTS:
    // Replaces the queue and starts playing `start`.
    void playTracks(const QList<mmm::TrackRow> &tracks, int start = 0);
    void playIndex(int index);
    void next();
    // Restarts the current track if more than 3 s in (or there's no previous), else goes back.
    void previous();
    void play();     // resumes, or starts the current/first queued track
    void pause();
    void togglePlayPause();
    void stop();
    void seek(qint64 positionMs);
    // Stops and empties the queue.
    void clearQueue();

Q_SIGNALS:
    // The current track changed (new track started, or cleared → null). For now-playing UI,
    // MPRIS metadata, and later Last.fm / Discord.
    void currentTrackChanged();

private:
    void startCurrent();
    void syncNextSource();
    void onAdvanced(const QUrl &url);
    void onEndOfStream();
    void onError(const QString &message);

    Player m_player;
    PlayQueue m_queue;
    int m_consecutiveErrors = 0;
    qint64 m_announcedTrackId = -1;
};

} // namespace mmm

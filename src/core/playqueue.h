// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "libraryquery.h"

#include <QAbstractListModel>
#include <QList>

namespace mmm {

// The play queue: an ordered list of tracks with a current position and a repeat mode.
// A plain model (no playback); PlaybackController drives the Player from it.
// Roles: trackId, path, title, artist, album, albumId, albumArtist, trackNumber,
// discNumber, durationMs, coverPath, artSourcePath, isCurrent.
class PlayQueue : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged FINAL)
    Q_PROPERTY(RepeatMode repeatMode READ repeatMode WRITE setRepeatMode NOTIFY repeatModeChanged FINAL)
    Q_PROPERTY(qint64 totalDurationMs READ totalDurationMs NOTIFY countChanged FINAL)

public:
    enum class RepeatMode { Off, All, One };
    Q_ENUM(RepeatMode)

    enum Role {
        TrackIdRole = Qt::UserRole + 1,
        PathRole,
        TitleRole,
        ArtistRole,
        AlbumRole,
        AlbumIdRole,
        AlbumArtistRole,
        TrackNumberRole,
        DiscNumberRole,
        DurationMsRole,
        CoverPathRole,
        ArtSourcePathRole,
        IsCurrentRole,
    };

    explicit PlayQueue(QObject *parent = nullptr);

    int count() const { return static_cast<int>(m_items.size()); }
    const QList<TrackRow> &items() const { return m_items; }
    const TrackRow *at(int index) const;

    int currentIndex() const { return m_current; }
    // -1 = nothing current. Out-of-range indexes are ignored.
    void setCurrentIndex(int index);
    const TrackRow *current() const { return at(m_current); }

    RepeatMode repeatMode() const { return m_repeat; }
    void setRepeatMode(RepeatMode mode);

    // Where playback goes after the current track: same track (One), wraps (All), or -1 at
    // the end (Off). previousIndex(): one back, wrapping only with All; -1 at the start.
    int nextIndex() const;
    int previousIndex() const;

    qint64 totalDurationMs() const;

    // Replaces the queue; `start` becomes current (clamped; -1 if empty).
    void setItems(const QList<TrackRow> &items, int start = 0);
    void append(const QList<TrackRow> &items);
    // Inserts right after the current track (or at the start if nothing is current).
    void insertNext(const QList<TrackRow> &items);
    void removeAt(int index);
    // Moves one item; the current track stays current.
    void move(int from, int to);
    void clear();

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void countChanged();
    void currentIndexChanged(int index);
    void repeatModeChanged(mmm::PlayQueue::RepeatMode mode);
    // The track that would play after the current one may have changed (content, order,
    // current index or repeat mode). PlaybackController re-queues the gapless next track.
    void upcomingChanged();
    // The current track was removed; the following one (if any) is now current.
    // PlaybackController treats this as a skip.
    void currentRemoved();

private:
    void insertAt(int row, const QList<TrackRow> &items);
    void emitCurrentRowChanged(int row);

    QList<TrackRow> m_items;
    int m_current = -1;
    RepeatMode m_repeat = RepeatMode::Off;
};

} // namespace mmm

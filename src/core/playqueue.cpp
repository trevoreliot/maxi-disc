// SPDX-License-Identifier: GPL-3.0-or-later
#include "playqueue.h"

#include <QHash>

namespace mmm {

PlayQueue::PlayQueue(QObject *parent)
    : QAbstractListModel(parent)
{
}

const TrackRow *PlayQueue::at(int index) const
{
    return index >= 0 && index < m_items.size() ? &m_items[index] : nullptr;
}

void PlayQueue::emitCurrentRowChanged(int row)
{
    if (row >= 0 && row < m_items.size())
        Q_EMIT dataChanged(index(row), index(row), {IsCurrentRole});
}

void PlayQueue::setCurrentIndex(int idx)
{
    if (idx < -1 || idx >= count() || idx == m_current)
        return;
    const int old = m_current;
    m_current = idx;
    emitCurrentRowChanged(old);
    emitCurrentRowChanged(m_current);
    Q_EMIT currentIndexChanged(m_current);
    Q_EMIT upcomingChanged();
}

void PlayQueue::setRepeatMode(RepeatMode mode)
{
    if (m_repeat == mode)
        return;
    m_repeat = mode;
    Q_EMIT repeatModeChanged(mode);
    Q_EMIT upcomingChanged();
}

int PlayQueue::nextIndex() const
{
    if (m_current < 0)
        return -1;
    switch (m_repeat) {
    case RepeatMode::One:
        return m_current;
    case RepeatMode::All:
        return (m_current + 1) % count();
    case RepeatMode::Off:
        break;
    }
    return m_current + 1 < count() ? m_current + 1 : -1;
}

int PlayQueue::previousIndex() const
{
    if (m_current < 0)
        return -1;
    if (m_current > 0)
        return m_current - 1;
    return m_repeat == RepeatMode::All ? count() - 1 : -1;
}

qint64 PlayQueue::totalDurationMs() const
{
    qint64 total = 0;
    for (const TrackRow &t : m_items)
        total += t.durationMs;
    return total;
}

bool PlayQueue::updateTracks(const QList<TrackRow> &rows)
{
    QHash<QString, const TrackRow *> byPath;
    for (const TrackRow &r : rows)
        byPath.insert(r.path, &r);
    bool currentChanged = false;
    for (int i = 0; i < count(); ++i) {
        const TrackRow *r = byPath.value(m_items.at(i).path);
        if (!r)
            continue;
        m_items[i] = *r;
        Q_EMIT dataChanged(index(i), index(i));
        currentChanged = currentChanged || i == m_current;
    }
    return currentChanged;
}

void PlayQueue::setItems(const QList<TrackRow> &items, int start)
{
    beginResetModel();
    m_items = items;
    m_current = m_items.isEmpty() ? -1 : std::clamp(start, 0, count() - 1);
    endResetModel();
    Q_EMIT countChanged();
    Q_EMIT currentIndexChanged(m_current);
    Q_EMIT upcomingChanged();
}

void PlayQueue::insertAt(int row, const QList<TrackRow> &items)
{
    if (items.isEmpty())
        return;
    const int n = static_cast<int>(items.size());
    beginInsertRows({}, row, row + n - 1);
    for (int i = 0; i < n; ++i)
        m_items.insert(row + i, items[i]);
    const bool shifted = m_current >= row;
    if (shifted)
        m_current += n;
    endInsertRows();
    Q_EMIT countChanged();
    if (shifted)
        Q_EMIT currentIndexChanged(m_current);
    Q_EMIT upcomingChanged();
}

void PlayQueue::append(const QList<TrackRow> &items)
{
    insertAt(count(), items);
}

void PlayQueue::insertNext(const QList<TrackRow> &items)
{
    insertAt(m_current + 1, items);
}

void PlayQueue::removeAt(int idx)
{
    if (idx < 0 || idx >= count())
        return;
    const bool wasCurrent = idx == m_current;
    beginRemoveRows({}, idx, idx);
    m_items.removeAt(idx);
    if (idx < m_current)
        --m_current;
    else if (wasCurrent && m_current >= count())
        m_current = count() - 1; // removed the last track: the new last one (or none)
    endRemoveRows();
    Q_EMIT countChanged();
    if (idx < m_current + 1 || wasCurrent) {
        emitCurrentRowChanged(m_current);
        Q_EMIT currentIndexChanged(m_current);
    }
    if (wasCurrent)
        Q_EMIT currentRemoved();
    Q_EMIT upcomingChanged();
}

void PlayQueue::move(int from, int to)
{
    if (from < 0 || from >= count() || to < 0 || to >= count() || from == to)
        return;
    // beginMoveRows wants the destination as "insert before this row" in the old numbering.
    if (!beginMoveRows({}, from, from, {}, to > from ? to + 1 : to))
        return;
    m_items.move(from, to);
    const int old = m_current;
    if (m_current == from)
        m_current = to;
    else if (from < m_current && to >= m_current)
        --m_current;
    else if (from > m_current && to <= m_current)
        ++m_current;
    endMoveRows();
    if (old != m_current)
        Q_EMIT currentIndexChanged(m_current);
    Q_EMIT upcomingChanged();
}

void PlayQueue::moveToNext(int from)
{
    if (from < 0 || from >= count() || from == m_current)
        return;
    // Taken out from before the current track, the slot after it is one lower.
    const int to = m_current < 0 ? 0 : (from < m_current ? m_current : m_current + 1);
    move(from, to);
}

void PlayQueue::clear()
{
    setItems({}, -1);
}

QVariantMap PlayQueue::get(int row) const
{
    QVariantMap out;
    if (row < 0 || row >= count())
        return out;
    const QModelIndex i = index(row);
    const QHash<int, QByteArray> roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        out.insert(QString::fromLatin1(it.value()), data(i, it.key()));
    return out;
}

int PlayQueue::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant PlayQueue::data(const QModelIndex &idx, int role) const
{
    const TrackRow *t = idx.isValid() ? at(idx.row()) : nullptr;
    if (!t)
        return {};
    switch (role) {
    case TrackIdRole: return static_cast<int>(t->id);
    case PathRole: return t->path;
    case Qt::DisplayRole:
    case TitleRole: return t->title;
    case ArtistRole: return t->artist;
    case AlbumRole: return t->album;
    case AlbumIdRole: return static_cast<int>(t->albumId);
    case AlbumArtistRole: return t->albumArtist;
    case TrackNumberRole: return t->trackNumber;
    case DiscNumberRole: return t->discNumber;
    case DurationMsRole: return t->durationMs;
    case CoverPathRole: return t->coverPath;
    case ArtSourcePathRole: return t->artSourcePath();
    case IsCurrentRole: return idx.row() == m_current;
    default: return {};
    }
}

QHash<int, QByteArray> PlayQueue::roleNames() const
{
    return {
        {TrackIdRole, "trackId"},         {PathRole, "path"},
        {TitleRole, "title"},             {ArtistRole, "artist"},
        {AlbumRole, "album"},             {AlbumIdRole, "albumId"},
        {AlbumArtistRole, "albumArtist"}, {TrackNumberRole, "trackNumber"},
        {DiscNumberRole, "discNumber"},   {DurationMsRole, "durationMs"},
        {CoverPathRole, "coverPath"},     {ArtSourcePathRole, "artSourcePath"},
        {IsCurrentRole, "isCurrent"},
    };
}

} // namespace mmm

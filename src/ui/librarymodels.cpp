// SPDX-License-Identifier: GPL-3.0-or-later
#include "librarymodels.h"

#include "appinfo.h"
#include "coverimageprovider.h"

#include <QSet>
#include <QTimer>

// --- LibraryListModel -------------------------------------------------------

LibraryListModel::LibraryListModel(QObject *parent)
    : QAbstractListModel(parent)
{
    if (Library *lib = Library::instance())
        connect(lib, &Library::changed, this, &LibraryListModel::reload);
    reload(); // queued: QML sets the filter properties before it runs
}

void LibraryListModel::reload()
{
    if (m_reloadQueued)
        return;
    m_reloadQueued = true;
    // Loading from the moment a reload is requested: the current rows are already stale.
    setLoading(true);
    QTimer::singleShot(0, this, [this] {
        m_reloadQueued = false;
        startLoad();
    });
}

QVariantMap LibraryListModel::get(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= rowCount())
        return map;
    const QModelIndex idx = index(row);
    const QHash<int, QByteArray> roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        map.insert(QString::fromUtf8(it.value()), data(idx, it.key()));
    return map;
}

void LibraryListModel::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    Q_EMIT loadingChanged();
}

namespace {
bool validRow(const QModelIndex &index, qsizetype size)
{
    return index.isValid() && index.row() >= 0 && index.row() < size;
}
} // namespace

// --- ArtistListModel --------------------------------------------------------

int ArtistListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant ArtistListModel::data(const QModelIndex &index, int role) const
{
    if (!validRow(index, m_rows.size()))
        return {};
    const mmm::ArtistRow &r = m_rows.at(index.row());
    switch (role) {
    case ArtistIdRole: return static_cast<int>(r.id);
    case Qt::DisplayRole:
    case NameRole: return r.name;
    case AlbumCountRole: return r.albumCount;
    default: return {};
    }
}

QHash<int, QByteArray> ArtistListModel::roleNames() const
{
    return {{ArtistIdRole, "artistId"}, {NameRole, "name"}, {AlbumCountRole, "albumCount"}};
}

void ArtistListModel::startLoad()
{
    fetchInto(&m_rows, [](const mmm::LibraryQuery &q) { return q.albumArtists(); });
}

// --- GenreListModel ---------------------------------------------------------

int GenreListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant GenreListModel::data(const QModelIndex &index, int role) const
{
    if (!validRow(index, m_rows.size()))
        return {};
    const mmm::GenreRow &r = m_rows.at(index.row());
    switch (role) {
    case GenreIdRole: return static_cast<int>(r.id);
    case Qt::DisplayRole:
    case NameRole: return r.name;
    case AlbumCountRole: return r.albumCount;
    default: return {};
    }
}

QHash<int, QByteArray> GenreListModel::roleNames() const
{
    return {{GenreIdRole, "genreId"}, {NameRole, "name"}, {AlbumCountRole, "albumCount"}};
}

void GenreListModel::startLoad()
{
    fetchInto(&m_rows, [](const mmm::LibraryQuery &q) { return q.genres(); });
}

// --- PlaylistListModel ------------------------------------------------------

int PlaylistListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant PlaylistListModel::data(const QModelIndex &index, int role) const
{
    if (!validRow(index, m_rows.size()))
        return {};
    const mmm::PlaylistRow &r = m_rows.at(index.row());
    switch (role) {
    case PlaylistIdRole: return static_cast<int>(r.id);
    case Qt::DisplayRole:
    case NameRole: return r.name;
    case ItemCountRole: return r.itemCount;
    case AvailableCountRole: return r.availableCount;
    default: return {};
    }
}

QHash<int, QByteArray> PlaylistListModel::roleNames() const
{
    return {{PlaylistIdRole, "playlistId"}, {NameRole, "name"}, {ItemCountRole, "itemCount"},
            {AvailableCountRole, "availableCount"}};
}

void PlaylistListModel::startLoad()
{
    fetchInto(&m_rows, [](const mmm::LibraryQuery &q) { return q.playlists(); });
}

// --- AlbumListModel ---------------------------------------------------------

void AlbumListModel::setArtistId(int id)
{
    if (m_artistId == id)
        return;
    m_artistId = id;
    Q_EMIT artistIdChanged();
    reload();
}

void AlbumListModel::setGenreId(int id)
{
    if (m_genreId == id)
        return;
    m_genreId = id;
    Q_EMIT genreIdChanged();
    reload();
}

QString AlbumListModel::coverUrl(const mmm::AlbumRow &row)
{
    return CoverLoader::url(row.id, row.coverPath, row.artTrackPath);
}

int AlbumListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant AlbumListModel::data(const QModelIndex &index, int role) const
{
    if (!validRow(index, m_rows.size()))
        return {};
    const mmm::AlbumRow &r = m_rows.at(index.row());
    switch (role) {
    case AlbumIdRole: return static_cast<int>(r.id);
    case Qt::DisplayRole:
    case TitleRole: return r.title;
    case ArtistRole: return r.artist;
    case YearRole: return r.year;
    case TrackCountRole: return r.trackCount;
    case DurationMsRole: return r.durationMs;
    case CoverUrlRole: return coverUrl(r);
    case CompilationRole: return r.compilation;
    default: return {};
    }
}

QHash<int, QByteArray> AlbumListModel::roleNames() const
{
    return {
        {AlbumIdRole, "albumId"},       {TitleRole, "title"},
        {ArtistRole, "artist"},         {YearRole, "year"},
        {TrackCountRole, "trackCount"}, {DurationMsRole, "durationMs"},
        {CoverUrlRole, "coverUrl"},     {CompilationRole, "compilation"},
    };
}

void AlbumListModel::startLoad()
{
    fetchInto(&m_rows, [artist = m_artistId, genre = m_genreId](const mmm::LibraryQuery &q) {
        return q.albums(artist, genre);
    });
}

// --- TrackListModel ---------------------------------------------------------

void TrackListModel::setAlbumId(int id)
{
    if (m_albumId == id)
        return;
    m_albumId = id;
    Q_EMIT albumIdChanged();
    reload();
}

void TrackListModel::setPlaylistId(int id)
{
    if (m_playlistId == id)
        return;
    m_playlistId = id;
    Q_EMIT playlistIdChanged();
    reload();
}

void TrackListModel::setSearchText(const QString &text)
{
    if (m_searchText == text)
        return;
    m_searchText = text;
    Q_EMIT searchTextChanged();
    reload();
}

void TrackListModel::setSearchLimit(int limit)
{
    if (m_searchLimit == limit)
        return;
    m_searchLimit = limit;
    Q_EMIT searchLimitChanged();
    if (!m_searchText.trimmed().isEmpty())
        reload();
}

qint64 TrackListModel::totalDurationMs() const
{
    qint64 total = 0;
    for (const mmm::TrackRow &r : m_rows)
        total += r.durationMs;
    return total;
}

int TrackListModel::discCount() const
{
    QSet<int> discs;
    for (const mmm::TrackRow &r : m_rows)
        discs.insert(r.discNumber);
    return static_cast<int>(discs.size());
}

int TrackListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant TrackListModel::data(const QModelIndex &index, int role) const
{
    if (!validRow(index, m_rows.size()))
        return {};
    const mmm::TrackRow &r = m_rows.at(index.row());
    switch (role) {
    case TrackIdRole: return static_cast<int>(r.id);
    case PathRole: return r.path;
    case Qt::DisplayRole:
    case TitleRole: return r.title;
    case ArtistRole: return r.artist;
    case AlbumRole: return r.album;
    case AlbumIdRole: return static_cast<int>(r.albumId);
    case AlbumArtistRole: return r.albumArtist;
    case TrackNumberRole: return r.trackNumber;
    case DiscNumberRole: return r.discNumber;
    case YearRole: return r.year;
    case DurationMsRole: return r.durationMs;
    case DurationRole: return mmm::formatDuration(r.durationMs);
    case CodecRole: return r.codec;
    default: return {};
    }
}

QHash<int, QByteArray> TrackListModel::roleNames() const
{
    return {
        {TrackIdRole, "trackId"},         {PathRole, "path"},
        {TitleRole, "title"},             {ArtistRole, "artist"},
        {AlbumRole, "album"},             {AlbumIdRole, "albumId"},
        {AlbumArtistRole, "albumArtist"}, {TrackNumberRole, "trackNumber"},
        {DiscNumberRole, "discNumber"},   {YearRole, "year"},
        {DurationMsRole, "durationMs"},   {DurationRole, "duration"},
        {CodecRole, "codec"},
    };
}

void TrackListModel::startLoad()
{
    const QString text = m_searchText.trimmed();
    if (!text.isEmpty()) {
        fetchInto(&m_rows, [text, limit = m_searchLimit](const mmm::LibraryQuery &q) { return q.search(text, limit); });
    } else if (m_playlistId > 0) {
        fetchInto(&m_rows, [pl = m_playlistId](const mmm::LibraryQuery &q) { return q.playlistTracks(pl); });
    } else if (m_albumId > 0) {
        fetchInto(&m_rows, [album = m_albumId](const mmm::LibraryQuery &q) { return q.albumTracks(album); });
    } else {
        cancelLoads();
        replaceRows(&m_rows, QList<mmm::TrackRow>{});
    }
}

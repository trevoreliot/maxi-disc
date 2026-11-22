// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <algorithm>

#include "library.h"
#include "libraryquery.h"

// Base for the library list models: loads rows on Library's query pool and applies them
// on the UI thread. Property changes and Library::changed() schedule one coalesced reload;
// results from superseded loads are dropped. If a reload returns the same rows (by id),
// they're updated in place so views keep their scroll position.
class LibraryListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged FINAL)

public:
    explicit LibraryListModel(QObject *parent = nullptr);

    int count() const { return rowCount(); }
    bool isLoading() const { return m_loading; }

    // Schedules a reload (coalesced; runs on the next event-loop pass).
    Q_INVOKABLE void reload();
    // Row `row` as {roleName: value}; empty if out of range.
    Q_INVOKABLE QVariantMap get(int row) const;

Q_SIGNALS:
    void countChanged();
    void loadingChanged();

protected:
    virtual void startLoad() = 0;

    // Runs `fetch` on the query pool and swaps the result into `rows`.
    template<typename Row, typename Fetch>
    void fetchInto(QList<Row> *rows, Fetch fetch)
    {
        const quint64 generation = ++m_generation;
        Library *lib = Library::instance();
        if (!lib) {
            replaceRows(rows, QList<Row>{});
            return;
        }
        setLoading(true);
        lib->runQuery(std::move(fetch)).then(this, [this, rows, generation](QList<Row> result) {
            if (generation != m_generation)
                return; // superseded
            replaceRows(rows, std::move(result));
            setLoading(false);
        });
    }

    template<typename Row>
    void replaceRows(QList<Row> *rows, QList<Row> next)
    {
        const bool sameIds = rows->size() == next.size()
            && std::equal(rows->cbegin(), rows->cend(), next.cbegin(),
                          [](const Row &a, const Row &b) { return a.id == b.id; });
        if (sameIds) {
            *rows = std::move(next);
            if (!rows->isEmpty())
                Q_EMIT dataChanged(index(0), index(static_cast<int>(rows->size()) - 1));
            Q_EMIT countChanged(); // count is the same, but derived properties may not be
            return;
        }
        beginResetModel();
        *rows = std::move(next);
        endResetModel();
        Q_EMIT countChanged();
    }

    // Invalidates in-flight loads (e.g. when a filter clears the list synchronously).
    void cancelLoads() { ++m_generation; setLoading(false); }

private:
    void setLoading(bool loading);

    quint64 m_generation = 0;
    bool m_loading = false;
    bool m_reloadQueued = false;
};

// Album artists. Roles: artistId, name, albumCount.
class ArtistListModel : public LibraryListModel
{
    Q_OBJECT
    QML_ELEMENT

public:
    enum Role { ArtistIdRole = Qt::UserRole + 1, NameRole, AlbumCountRole };
    using LibraryListModel::LibraryListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

protected:
    void startLoad() override;

private:
    QList<mmm::ArtistRow> m_rows;
};

// Genres. Roles: genreId, name, albumCount.
class GenreListModel : public LibraryListModel
{
    Q_OBJECT
    QML_ELEMENT

public:
    enum Role { GenreIdRole = Qt::UserRole + 1, NameRole, AlbumCountRole };
    using LibraryListModel::LibraryListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

protected:
    void startLoad() override;

private:
    QList<mmm::GenreRow> m_rows;
};

// Albums, optionally filtered by album artist and/or genre (0 = any).
// Roles: albumId, title, artist, year, trackCount, durationMs, coverUrl, compilation.
class AlbumListModel : public LibraryListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int artistId READ artistId WRITE setArtistId NOTIFY artistIdChanged FINAL)
    Q_PROPERTY(int genreId READ genreId WRITE setGenreId NOTIFY genreIdChanged FINAL)

public:
    enum Role {
        AlbumIdRole = Qt::UserRole + 1,
        TitleRole,
        ArtistRole,
        YearRole,
        TrackCountRole,
        DurationMsRole,
        CoverUrlRole,
        CompilationRole,
    };
    using LibraryListModel::LibraryListModel;

    // Ids are int in QML: SQLite row ids of a personal library stay far below 2^31.
    int artistId() const { return static_cast<int>(m_artistId); }
    void setArtistId(int id);
    int genreId() const { return static_cast<int>(m_genreId); }
    void setGenreId(int id);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // "image://cover/<albumId>/<base64url(folderCover \n artTrack)>" or "" when there's no art.
    static QString coverUrl(const mmm::AlbumRow &row);

Q_SIGNALS:
    void artistIdChanged();
    void genreIdChanged();

protected:
    void startLoad() override;

private:
    qint64 m_artistId = 0;
    qint64 m_genreId = 0;
    QList<mmm::AlbumRow> m_rows;
};

// Playlists. Roles: playlistId, name, itemCount, availableCount.
class PlaylistListModel : public LibraryListModel
{
    Q_OBJECT
    QML_ELEMENT

public:
    enum Role { PlaylistIdRole = Qt::UserRole + 1, NameRole, ItemCountRole, AvailableCountRole };
    using LibraryListModel::LibraryListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

protected:
    void startLoad() override;

private:
    QList<mmm::PlaylistRow> m_rows;
};

// Tracks: search results when searchText is set, otherwise a playlist's tracks
// (playlistId), otherwise an album's tracks (albumId), otherwise empty. Roles: trackId, path, title, artist, album, albumId, albumArtist,
// trackNumber, discNumber, year, durationMs, duration (formatted), codec, coverUrl (album
// folder art, else the track's embedded art).
class TrackListModel : public LibraryListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int albumId READ albumId WRITE setAlbumId NOTIFY albumIdChanged FINAL)
    Q_PROPERTY(int playlistId READ playlistId WRITE setPlaylistId NOTIFY playlistIdChanged FINAL)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged FINAL)
    Q_PROPERTY(int searchLimit READ searchLimit WRITE setSearchLimit NOTIFY searchLimitChanged FINAL)
    Q_PROPERTY(qint64 totalDurationMs READ totalDurationMs NOTIFY countChanged FINAL)
    Q_PROPERTY(int discCount READ discCount NOTIFY countChanged FINAL)

public:
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
        YearRole,
        DurationMsRole,
        DurationRole,
        CodecRole,
        CoverUrlRole,
    };
    using LibraryListModel::LibraryListModel;

    int albumId() const { return static_cast<int>(m_albumId); }
    void setAlbumId(int id);
    int playlistId() const { return static_cast<int>(m_playlistId); }
    void setPlaylistId(int id);
    QString searchText() const { return m_searchText; }
    void setSearchText(const QString &text);
    int searchLimit() const { return m_searchLimit; }
    void setSearchLimit(int limit);
    qint64 totalDurationMs() const;
    int discCount() const; // distinct disc numbers (an album shown with "Disc N" headers if > 1)
    const QList<mmm::TrackRow> &rows() const { return m_rows; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void albumIdChanged();
    void playlistIdChanged();
    void searchTextChanged();
    void searchLimitChanged();

protected:
    void startLoad() override;

private:
    qint64 m_albumId = 0;
    qint64 m_playlistId = 0;
    QString m_searchText;
    int m_searchLimit = 500;
    QList<mmm::TrackRow> m_rows;
};

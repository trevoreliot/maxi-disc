// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QSet>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include "artworkfinder.h"

// Backs the artwork finder dialog (QML type `ArtFinder`): the albums to review (this model)
// and the cover candidates of the current one. Searching runs in mmm::ArtworkFinder; saving
// in Library::saveAlbumArt().
//
// openMissing() lists albums without art (Library.missingArtCount), openAlbum() one album
// (to replace its art). The current album is searched first. accept() downloads a candidate
// and saves it; skip() marks the album so later runs leave it out (`includeSkipped` lists
// them again). close() stops searching; downloads and saves already started still finish.
//
// Roles: albumId, title, artist, year, trackCount, folder, status (ArtFinder.Status),
// candidateCount, error, coverUrl (the saved cover, once saved).
class ArtFinder : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged FINAL)
    Q_PROPERTY(bool includeSkipped READ includeSkipped WRITE setIncludeSkipped NOTIFY includeSkippedChanged FINAL)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentChanged FINAL)
    // The current album as {roleName: value} ({} if none), and its candidates as a list of
    // {title, artist, year, country, format, trackCount, score, fromReleaseGroup, previewUrl}.
    Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged FINAL)
    Q_PROPERTY(QVariantList candidates READ candidates NOTIFY currentChanged FINAL)
    Q_PROPERTY(int searchedCount READ searchedCount NOTIFY progressChanged FINAL)
    Q_PROPERTY(int savedCount READ savedCount NOTIFY progressChanged FINAL)
    Q_PROPERTY(int skippedCount READ skippedCount NOTIFY progressChanged FINAL)

public:
    enum Status { Pending, Searching, Found, NotFound, Failed, Saving, Saved, Skipped };
    Q_ENUM(Status)
    enum Role {
        AlbumIdRole = Qt::UserRole + 1,
        TitleRole,
        ArtistRole,
        YearRole,
        TrackCountRole,
        FolderRole,
        StatusRole,
        CandidateCountRole,
        ErrorRole,
        CoverUrlRole,
    };

    explicit ArtFinder(QObject *parent = nullptr);
    ~ArtFinder() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return rowCount(); }
    bool isLoading() const { return m_loading; }
    bool includeSkipped() const { return m_includeSkipped; }
    void setIncludeSkipped(bool include);
    int currentIndex() const { return m_current; }
    void setCurrentIndex(int row);
    QVariantMap current() const;
    QVariantList candidates() const;
    int searchedCount() const;
    int savedCount() const { return countStatus(Saved); }
    int skippedCount() const { return countStatus(Skipped); }

    Q_INVOKABLE void openMissing();
    Q_INVOKABLE void openAlbum(int albumId);
    Q_INVOKABLE void close();
    // Saves candidate `index` of the current album. False if there's no such candidate or
    // the album is being saved already.
    Q_INVOKABLE bool accept(int index);
    Q_INVOKABLE void skip();
    Q_INVOKABLE void retry();
    // The first row at or after `from` (wrapping) that still needs a decision: not saved,
    // saving or skipped. -1 if none.
    Q_INVOKABLE int nextOpenRow(int from) const;

    // Test hook: shows these albums without searching. Each is {albumId, title, artist,
    // status, candidates: [{title, previewUrl, saveUrl, ...}]}.
    Q_INVOKABLE void setTestData(const QVariantList &albums);

    mmm::ArtworkFinder &finder() { return m_finder; }

Q_SIGNALS:
    void countChanged();
    void loadingChanged();
    void includeSkippedChanged();
    void currentChanged();
    void progressChanged();
    // accept() started (download + save) for this album.
    void accepted(int albumId);
    // A save ended.
    void saved(int albumId, bool ok, const QString &message);

private:
    struct Row
    {
        mmm::ArtworkTarget album;
        Status status = Pending;
        QList<mmm::ArtworkCandidate> candidates;
        QString error;
        QString coverUrl;
    };

    void load(bool missing, qint64 albumId);
    void setRows(const QList<mmm::ArtworkTarget> &albums);
    void onAlbumUpdated(qint64 albumId, mmm::ArtworkFinder::Status status, const QString &error);
    void save(qint64 albumId, const QByteArray &image);
    void finishSave(qint64 albumId, bool ok, const QString &message, const QString &coverUrl);
    int rowOf(qint64 albumId) const;
    void rowChanged(int row);
    int countStatus(Status s) const;
    void setLoading(bool loading);

    mmm::ArtworkFinder m_finder;
    QList<Row> m_rows;
    int m_current = -1;
    bool m_loading = false;
    bool m_includeSkipped = false;
    quint64 m_generation = 0; // drops list loads that were superseded
    QSet<qint64> m_saving;     // albums being downloaded/saved; outlives close(), so a reopened
                               // list shows them as saving and can't start a second save
};

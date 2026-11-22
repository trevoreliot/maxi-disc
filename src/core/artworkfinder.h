// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QUrl>

#include <functional>

#include "coverartarchiveclient.h"
#include "libraryquery.h"
#include "musicbrainzclient.h"

namespace mmm {

// A cover the artwork finder offers for an album.
struct ArtworkCandidate
{
    QString releaseId;
    QString releaseGroupId;
    QString title;          // of the release
    QString artist;
    int year = 0;
    QString country;
    QString format;
    int trackCount = 0;
    int score = 0;          // ArtworkMatch::score; 100+ for the release the tags name
    bool fromReleaseGroup = false; // the album's art across editions, not this release's own
    QString imageId;
    QUrl previewUrl;        // ~500 px
    QUrl saveUrl;           // ≤ 1200 px
};

// Finds cover candidates for albums: MusicBrainz release lookup (by the MBID in the tags)
// or search (title + artist, then a looser retry), then the Cover Art Archive front covers
// of the best 6 releases; if fewer than 3 have one, their release groups' covers too.
//
// Albums are searched one at a time in the order given (MusicBrainz allows ~1 request/s),
// while cover listings for earlier albums load in parallel. prioritize() moves an album to
// the front, e.g. the one the user is looking at. Runs on the thread it lives on (async
// network, no blocking); results arrive through albumUpdated().
// Environment: MMM_MUSICBRAINZ_URL / MMM_COVERARTARCHIVE_URL replace the services' base URLs.
class ArtworkFinder : public QObject
{
    Q_OBJECT

public:
    enum class Status { Pending, Searching, Found, NotFound, Failed };
    Q_ENUM(Status)

    static constexpr int kMaxReleases = 6;
    static constexpr int kWantedCandidates = 3;

    explicit ArtworkFinder(QObject *parent = nullptr);
    ~ArtworkFinder() override;

    MusicBrainzClient &musicBrainz() { return m_mb; }
    CoverArtArchiveClient &coverArt() { return m_caa; }

    // Replaces the work list and starts searching.
    void start(const QList<ArtworkTarget> &albums);
    void prioritize(qint64 albumId);
    // Takes a not-yet-searched album out of the queue (the user skipped it); retry() brings it back.
    void drop(qint64 albumId);
    // Searches an album again (after a failure); it goes to the front.
    void retry(qint64 albumId);
    // Cancels everything; no more signals for the current albums.
    void stop();

    Status status(qint64 albumId) const;
    QList<ArtworkCandidate> candidates(qint64 albumId) const;
    int searchedCount() const;
    int totalCount() const { return static_cast<int>(m_jobs.size()); }

    // Downloads a candidate's image for saving (any URL, also file://). Not cancelled by
    // stop() / start(): a download for an accepted cover finishes even if the user moves on.
    using DataCallback = std::function<void(const QByteArray &data, const QString &error)>;
    void download(const QUrl &url, QObject *context, DataCallback done);
    HttpFetcher &downloads() { return m_downloads; }

Q_SIGNALS:
    void albumUpdated(qint64 albumId, mmm::ArtworkFinder::Status status, const QString &error);
    void progressChanged();

private:
    struct Job
    {
        ArtworkTarget album;
        Status status = Status::Pending;
        quint64 generation = 0;
        QList<MbRelease> releases;   // scored, best first
        QList<int> scores;
        QList<ArtworkCandidate> found;
        QSet<QString> imageIds;
        int probesPending = 0;
        bool groupRound = false;
        QString error;
    };

    void pumpSearch();
    void searchDone(qint64 albumId, quint64 generation);
    void search(qint64 albumId, quint64 generation, bool relaxed);
    void probeReleases(qint64 albumId);
    void probeGroups(qint64 albumId);
    void probeDone(qint64 albumId, quint64 generation, const ArtworkCandidate &base,
                   const std::optional<CaaImage> &front, const QString &error);
    void finish(qint64 albumId);
    void setStatus(qint64 albumId, Status status);
    Job *job(qint64 albumId, quint64 generation);

    QNetworkAccessManager m_nam;
    MusicBrainzClient m_mb;
    CoverArtArchiveClient m_caa;
    HttpFetcher m_downloads;
    QHash<qint64, Job> m_jobs;
    QList<qint64> m_queue;   // albums waiting for their MusicBrainz search
    qint64 m_searching = 0;  // album whose MusicBrainz search is running (0 = none)
    quint64 m_nextGeneration = 0;
};

} // namespace mmm

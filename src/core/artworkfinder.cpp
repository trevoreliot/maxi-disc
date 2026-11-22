// SPDX-License-Identifier: GPL-3.0-or-later
#include "artworkfinder.h"

#include "appinfo.h"
#include "artworkmatch.h"

#include <QLoggingCategory>

#include <algorithm>

Q_LOGGING_CATEGORY(lcArt, "mmm.artwork")

namespace mmm {

namespace {

ArtworkCandidate candidateFrom(const MbRelease &r, int score)
{
    ArtworkCandidate c;
    c.releaseId = r.id;
    c.releaseGroupId = r.releaseGroupId;
    c.title = r.title;
    c.artist = r.artist;
    c.year = r.year;
    c.country = r.country;
    c.format = r.format;
    c.trackCount = r.trackCount;
    c.score = score;
    return c;
}

} // namespace

ArtworkFinder::ArtworkFinder(QObject *parent)
    : QObject(parent)
    , m_mb(&m_nam, this)
    , m_caa(&m_nam, this)
    , m_downloads(&m_nam, HttpFetcher::Options{2, 0, 2, 60000, 2000}, this)
{
    m_downloads.setUserAgent(AppInfo::userAgent());
    // Overrides for tests and development (e.g. a local MusicBrainz mirror). ctest points
    // them at a closed local port so no test ever reaches the real services.
    const QString mb = qEnvironmentVariable("MMM_MUSICBRAINZ_URL");
    if (!mb.isEmpty())
        m_mb.setBaseUrl(QUrl(mb));
    const QString caa = qEnvironmentVariable("MMM_COVERARTARCHIVE_URL");
    if (!caa.isEmpty())
        m_caa.setBaseUrl(QUrl(caa));
}

ArtworkFinder::~ArtworkFinder()
{
    stop();
    m_downloads.cancelAll();
}

void ArtworkFinder::start(const QList<ArtworkTarget> &albums)
{
    stop();
    for (const ArtworkTarget &a : albums) {
        if (m_jobs.contains(a.albumId))
            continue;
        Job j;
        j.album = a;
        j.generation = ++m_nextGeneration;
        m_jobs.insert(a.albumId, std::move(j));
        m_queue << a.albumId;
    }
    Q_EMIT progressChanged();
    pumpSearch();
}

void ArtworkFinder::prioritize(qint64 albumId)
{
    if (m_queue.removeOne(albumId))
        m_queue.prepend(albumId);
}

void ArtworkFinder::drop(qint64 albumId)
{
    m_queue.removeAll(albumId);
}

void ArtworkFinder::retry(qint64 albumId)
{
    const auto it = m_jobs.find(albumId);
    if (it == m_jobs.end())
        return;
    Job fresh;
    fresh.album = it->album;
    fresh.generation = ++m_nextGeneration; // results of the old search are dropped
    *it = std::move(fresh);
    m_queue.removeAll(albumId);
    m_queue.prepend(albumId);
    setStatus(albumId, Status::Pending);
    Q_EMIT progressChanged();
    pumpSearch();
}

void ArtworkFinder::stop()
{
    m_mb.cancelAll();
    m_caa.cancelAll();
    m_jobs.clear();
    m_queue.clear();
    m_searching = 0;
}

ArtworkFinder::Status ArtworkFinder::status(qint64 albumId) const
{
    const auto it = m_jobs.constFind(albumId);
    return it == m_jobs.cend() ? Status::Pending : it->status;
}

QList<ArtworkCandidate> ArtworkFinder::candidates(qint64 albumId) const
{
    const auto it = m_jobs.constFind(albumId);
    return it == m_jobs.cend() || it->status != Status::Found ? QList<ArtworkCandidate>() : it->found;
}

int ArtworkFinder::searchedCount() const
{
    return static_cast<int>(std::count_if(m_jobs.cbegin(), m_jobs.cend(), [](const Job &j) {
        return j.status == Status::Found || j.status == Status::NotFound || j.status == Status::Failed;
    }));
}

void ArtworkFinder::download(const QUrl &url, QObject *context, DataCallback done)
{
    m_downloads.get(url, context, [done = std::move(done)](const HttpResult &r) {
        if (!r.ok()) {
            done({}, QStringLiteral("download failed: %1").arg(r.describe()));
            return;
        }
        done(r.body, QString());
    });
}

ArtworkFinder::Job *ArtworkFinder::job(qint64 albumId, quint64 generation)
{
    const auto it = m_jobs.find(albumId);
    return it == m_jobs.end() || it->generation != generation ? nullptr : &*it;
}

void ArtworkFinder::setStatus(qint64 albumId, Status status)
{
    const auto it = m_jobs.find(albumId);
    if (it == m_jobs.end())
        return;
    it->status = status;
    Q_EMIT albumUpdated(albumId, status, it->error);
}

void ArtworkFinder::pumpSearch()
{
    if (m_searching != 0 || m_queue.isEmpty())
        return;
    const qint64 id = m_queue.takeFirst();
    const auto it = m_jobs.find(id);
    if (it == m_jobs.end())
        return pumpSearch();
    m_searching = id;
    const quint64 generation = it->generation;
    const QString mbid = it->album.mbAlbumId;
    setStatus(id, Status::Searching);

    if (!MusicBrainzClient::isMbid(mbid)) {
        search(id, generation, false);
        return;
    }
    m_mb.lookupRelease(mbid, this, [this, id, generation](const QList<MbRelease> &releases, const QString &error) {
        Job *j = job(id, generation);
        if (!j) {
            searchDone(id, generation);
            return;
        }
        if (!error.isEmpty()) {
            // Offline or MusicBrainz down: a search would fail the same way.
            j->error = error;
            searchDone(id, generation);
            return;
        }
        if (releases.isEmpty()) {
            // Unknown MBID (merged or deleted release): search by name instead.
            search(id, generation, false);
            return;
        }
        // The release the tags name wins over anything a search finds.
        j->releases = {releases.front()};
        j->scores = {100 + std::max(0, ArtworkMatch::score(j->album, releases.front()))};
        searchDone(id, generation);
    });
}

void ArtworkFinder::search(qint64 albumId, quint64 generation, bool relaxed)
{
    const Job *current = job(albumId, generation);
    if (!current) {
        searchDone(albumId, generation);
        return;
    }
    m_mb.searchReleases(current->album.title, current->album.artist, relaxed, this,
                        [this, albumId, generation, relaxed](const QList<MbRelease> &releases, const QString &error) {
        Job *j = job(albumId, generation);
        if (!j) {
            searchDone(albumId, generation);
            return;
        }
        if (!error.isEmpty() && releases.isEmpty()) {
            j->error = error;
            searchDone(albumId, generation);
            return;
        }
        QList<std::pair<int, MbRelease>> scored;
        for (const MbRelease &r : releases) {
            const int s = ArtworkMatch::score(j->album, r);
            if (s >= 0)
                scored.emplace_back(s, r);
        }
        std::stable_sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        if (scored.isEmpty() && !relaxed
            && MusicBrainzClient::searchQuery(j->album.title, j->album.artist, true)
                   != MusicBrainzClient::searchQuery(j->album.title, j->album.artist, false)) {
            search(albumId, generation, true);
            return;
        }
        for (qsizetype i = 0; i < std::min<qsizetype>(scored.size(), kMaxReleases); ++i) {
            j->scores << scored[i].first;
            j->releases << scored[i].second;
        }
        searchDone(albumId, generation);
    });
}

void ArtworkFinder::searchDone(qint64 albumId, quint64 generation)
{
    if (m_searching == albumId)
        m_searching = 0;
    pumpSearch(); // the next album's search waits its turn in the rate limiter
    if (job(albumId, generation))
        probeReleases(albumId);
}

void ArtworkFinder::probeReleases(qint64 albumId)
{
    Job &j = m_jobs[albumId];
    j.probesPending = static_cast<int>(j.releases.size());
    if (j.probesPending == 0) {
        finish(albumId);
        return;
    }
    const quint64 generation = j.generation;
    for (qsizetype i = 0; i < j.releases.size(); ++i) {
        const ArtworkCandidate base = candidateFrom(j.releases[i], j.scores[i]);
        m_caa.frontCover(CoverArtArchiveClient::Kind::Release, base.releaseId, this,
                         [this, albumId, generation, base](const std::optional<CaaImage> &front, const QString &error) {
                             probeDone(albumId, generation, base, front, error);
                         });
    }
}

void ArtworkFinder::probeGroups(qint64 albumId)
{
    Job &j = m_jobs[albumId];
    j.groupRound = true;
    QList<ArtworkCandidate> bases;
    QSet<QString> groups;
    for (qsizetype i = 0; i < std::min<qsizetype>(j.releases.size(), kWantedCandidates); ++i) {
        const QString group = j.releases[i].releaseGroupId;
        if (group.isEmpty() || groups.contains(group))
            continue;
        groups.insert(group);
        ArtworkCandidate base = candidateFrom(j.releases[i], std::max(0, j.scores[i] - 1));
        base.fromReleaseGroup = true;
        bases << base;
    }
    j.probesPending = static_cast<int>(bases.size());
    if (bases.isEmpty()) {
        finish(albumId);
        return;
    }
    const quint64 generation = j.generation;
    for (const ArtworkCandidate &base : std::as_const(bases)) {
        m_caa.frontCover(CoverArtArchiveClient::Kind::ReleaseGroup, base.releaseGroupId, this,
                         [this, albumId, generation, base](const std::optional<CaaImage> &front, const QString &error) {
                             probeDone(albumId, generation, base, front, error);
                         });
    }
}

void ArtworkFinder::probeDone(qint64 albumId, quint64 generation, const ArtworkCandidate &base,
                              const std::optional<CaaImage> &front, const QString &error)
{
    Job *j = job(albumId, generation);
    if (!j)
        return;
    if (front && !j->imageIds.contains(front->id)) {
        ArtworkCandidate c = base;
        c.imageId = front->id;
        c.previewUrl = front->previewUrl();
        c.saveUrl = front->saveUrl();
        j->imageIds.insert(front->id);
        j->found << c;
    }
    if (!error.isEmpty())
        j->error = error;
    if (--j->probesPending > 0)
        return;
    if (!j->groupRound && j->found.size() < kWantedCandidates)
        probeGroups(albumId);
    else
        finish(albumId);
}

void ArtworkFinder::finish(qint64 albumId)
{
    Job &j = m_jobs[albumId];
    std::stable_sort(j.found.begin(), j.found.end(),
                     [](const ArtworkCandidate &a, const ArtworkCandidate &b) { return a.score > b.score; });
    if (!j.found.isEmpty())
        j.error.clear(); // a failed listing doesn't matter when others worked
    setStatus(albumId, !j.found.isEmpty() ? Status::Found : !j.error.isEmpty() ? Status::Failed : Status::NotFound);
    Q_EMIT progressChanged();
}

} // namespace mmm

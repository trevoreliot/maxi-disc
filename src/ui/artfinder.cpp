// SPDX-License-Identifier: GPL-3.0-or-later
#include "artfinder.h"

#include "coverimageprovider.h"
#include "library.h"

#include <algorithm>

namespace {

ArtFinder::Status fromFinder(mmm::ArtworkFinder::Status s)
{
    switch (s) {
    case mmm::ArtworkFinder::Status::Pending: return ArtFinder::Pending;
    case mmm::ArtworkFinder::Status::Searching: return ArtFinder::Searching;
    case mmm::ArtworkFinder::Status::Found: return ArtFinder::Found;
    case mmm::ArtworkFinder::Status::NotFound: return ArtFinder::NotFound;
    case mmm::ArtworkFinder::Status::Failed: return ArtFinder::Failed;
    }
    return ArtFinder::Pending;
}

QVariantMap candidateMap(const mmm::ArtworkCandidate &c)
{
    return {
        {QStringLiteral("title"), c.title},
        {QStringLiteral("artist"), c.artist},
        {QStringLiteral("year"), c.year},
        {QStringLiteral("country"), c.country},
        {QStringLiteral("format"), c.format},
        {QStringLiteral("trackCount"), c.trackCount},
        {QStringLiteral("score"), c.score},
        {QStringLiteral("fromReleaseGroup"), c.fromReleaseGroup},
        {QStringLiteral("previewUrl"), c.previewUrl},
        {QStringLiteral("releaseId"), c.releaseId},
    };
}

} // namespace

ArtFinder::ArtFinder(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(&m_finder, &mmm::ArtworkFinder::albumUpdated, this, &ArtFinder::onAlbumUpdated);
    connect(&m_finder, &mmm::ArtworkFinder::progressChanged, this, &ArtFinder::progressChanged);
}

ArtFinder::~ArtFinder() = default;

int ArtFinder::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant ArtFinder::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case AlbumIdRole: return static_cast<int>(r.album.albumId);
    case Qt::DisplayRole:
    case TitleRole: return r.album.title;
    case ArtistRole: return r.album.artist;
    case YearRole: return r.album.year;
    case TrackCountRole: return r.album.trackCount;
    case FolderRole: return r.album.folder;
    case StatusRole: return static_cast<int>(r.status);
    case CandidateCountRole: return static_cast<int>(r.candidates.size());
    case ErrorRole: return r.error;
    case CoverUrlRole: return r.coverUrl;
    default: return {};
    }
}

QHash<int, QByteArray> ArtFinder::roleNames() const
{
    return {
        {AlbumIdRole, "albumId"},   {TitleRole, "title"},   {ArtistRole, "artist"},
        {YearRole, "year"},         {TrackCountRole, "trackCount"}, {FolderRole, "folder"},
        {StatusRole, "status"},     {CandidateCountRole, "candidateCount"},
        {ErrorRole, "error"},       {CoverUrlRole, "coverUrl"},
    };
}

void ArtFinder::setIncludeSkipped(bool include)
{
    if (m_includeSkipped == include)
        return;
    m_includeSkipped = include;
    Q_EMIT includeSkippedChanged();
}

void ArtFinder::setCurrentIndex(int row)
{
    row = m_rows.isEmpty() ? -1 : std::clamp(row, 0, static_cast<int>(m_rows.size()) - 1);
    if (row == m_current)
        return;
    m_current = row;
    if (row >= 0)
        m_finder.prioritize(m_rows.at(row).album.albumId); // what the user looks at loads first
    Q_EMIT currentChanged();
}

QVariantMap ArtFinder::current() const
{
    QVariantMap map;
    if (m_current < 0 || m_current >= m_rows.size())
        return map;
    const QModelIndex idx = index(m_current);
    const QHash<int, QByteArray> roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        map.insert(QString::fromUtf8(it.value()), data(idx, it.key()));
    return map;
}

QVariantList ArtFinder::candidates() const
{
    QVariantList out;
    if (m_current < 0 || m_current >= m_rows.size())
        return out;
    for (const mmm::ArtworkCandidate &c : m_rows.at(m_current).candidates)
        out << candidateMap(c);
    return out;
}

int ArtFinder::searchedCount() const
{
    return static_cast<int>(std::count_if(m_rows.cbegin(), m_rows.cend(), [](const Row &r) {
        return r.status != Pending && r.status != Searching;
    }));
}

int ArtFinder::countStatus(Status s) const
{
    return static_cast<int>(std::count_if(m_rows.cbegin(), m_rows.cend(), [s](const Row &r) { return r.status == s; }));
}

void ArtFinder::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    Q_EMIT loadingChanged();
}

void ArtFinder::openMissing()
{
    load(true, 0);
}

void ArtFinder::openAlbum(int albumId)
{
    load(false, albumId);
}

void ArtFinder::load(bool missing, qint64 albumId)
{
    close();
    Library *lib = Library::instance();
    if (!lib)
        return;
    setLoading(true);
    const quint64 generation = m_generation;
    lib->runQuery([missing, albumId, skipped = m_includeSkipped](const mmm::LibraryQuery &q) {
           if (missing)
               return q.albumsMissingArt(skipped);
           const auto one = q.artworkTarget(albumId);
           return one ? QList<mmm::ArtworkTarget>{*one} : QList<mmm::ArtworkTarget>{};
       })
        .then(this, [this, generation](const QList<mmm::ArtworkTarget> &albums) {
            if (generation != m_generation)
                return; // closed or reopened meanwhile
            setLoading(false);
            setRows(albums);
            m_finder.start(albums);
        });
}

void ArtFinder::setRows(const QList<mmm::ArtworkTarget> &albums)
{
    beginResetModel();
    m_rows.clear();
    for (const mmm::ArtworkTarget &a : albums)
        m_rows << Row{a, m_saving.contains(a.albumId) ? Saving : Pending, {}, {}, {}};
    m_current = m_rows.isEmpty() ? -1 : 0;
    endResetModel();
    Q_EMIT countChanged();
    Q_EMIT currentChanged();
    Q_EMIT progressChanged();
}

void ArtFinder::close()
{
    ++m_generation;
    m_finder.stop(); // downloads (accepted covers) keep going
    setLoading(false);
    if (m_rows.isEmpty() && m_current == -1)
        return;
    beginResetModel();
    m_rows.clear();
    m_current = -1;
    endResetModel();
    Q_EMIT countChanged();
    Q_EMIT currentChanged();
    Q_EMIT progressChanged();
}

int ArtFinder::rowOf(qint64 albumId) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).album.albumId == albumId)
            return i;
    }
    return -1;
}

void ArtFinder::rowChanged(int row)
{
    Q_EMIT dataChanged(index(row), index(row));
    if (row == m_current)
        Q_EMIT currentChanged();
    Q_EMIT progressChanged();
}

void ArtFinder::onAlbumUpdated(qint64 albumId, mmm::ArtworkFinder::Status status, const QString &error)
{
    const int row = rowOf(albumId);
    if (row < 0)
        return;
    Row &r = m_rows[row];
    if (r.status == Saved)
        return; // a retry finishing late doesn't undo a save
    r.candidates = m_finder.candidates(albumId);
    if (r.status == Skipped || r.status == Saving) {
        rowChanged(row); // keeps its state; the covers are there if it goes back to choosing
        return;
    }
    r.status = fromFinder(status);
    r.error = error;
    rowChanged(row);
}

bool ArtFinder::accept(int index)
{
    if (m_current < 0 || m_current >= m_rows.size())
        return false;
    Row &r = m_rows[m_current];
    const qint64 albumId = r.album.albumId;
    if (index < 0 || index >= r.candidates.size() || r.status == Saving || m_saving.contains(albumId))
        return false;
    m_saving.insert(albumId);
    const QUrl url = r.candidates.at(index).saveUrl;
    r.status = Saving;
    r.error.clear();
    rowChanged(m_current);
    Q_EMIT accepted(static_cast<int>(albumId));

    // `this` as context: if the dialog's ArtFinder is destroyed, nothing runs. close()
    // doesn't cancel downloads, so an accepted cover is still saved after it.
    m_finder.download(url, this, [this, albumId](const QByteArray &data, const QString &error) {
        if (!error.isEmpty()) {
            finishSave(albumId, false, QStringLiteral("Couldn't download the cover: %1").arg(error), {});
            return;
        }
        save(albumId, data);
    });
    return true;
}

void ArtFinder::save(qint64 albumId, const QByteArray &image)
{
    Library *lib = Library::instance();
    if (!lib) {
        finishSave(albumId, false, QStringLiteral("no library"), {});
        return;
    }
    lib->saveAlbumArt(albumId, image).then(this, [this, albumId](const mmm::ArtworkSaveResult &result) {
        if (!result.ok) {
            finishSave(albumId, false, QStringLiteral("Couldn't save the cover: %1").arg(result.error), {});
            return;
        }
        QString note;
        if (!result.warnings.isEmpty())
            note = result.warnings.size() == 1 ? result.warnings.front()
                                               : tr("%1 problems. First: %2").arg(result.warnings.size()).arg(result.warnings.front());
        finishSave(albumId, true, note,
                   CoverLoader::url(albumId, result.coverFiles.value(0), result.edit.written.value(0)));
    });
}

void ArtFinder::finishSave(qint64 albumId, bool ok, const QString &message, const QString &coverUrl)
{
    m_saving.remove(albumId);
    const int row = rowOf(albumId);
    // Only the row that is waiting for this save (a reopened list may show the album afresh).
    if (row >= 0 && m_rows[row].status == Saving) {
        Row &r = m_rows[row];
        r.error = message;
        if (ok) {
            r.status = Saved;
            r.coverUrl = coverUrl;
        } else {
            // Back to choosing; a reopened list that hasn't searched yet goes back to waiting.
            r.status = r.candidates.isEmpty() ? fromFinder(m_finder.status(albumId)) : Found;
            if (r.status == Found && r.candidates.isEmpty())
                r.status = NotFound; // nothing to choose from (e.g. test data without candidates)
        }
        rowChanged(row);
    }
    Q_EMIT saved(static_cast<int>(albumId), ok, ok ? QString() : message);
}

void ArtFinder::skip()
{
    if (m_current < 0 || m_current >= m_rows.size())
        return;
    Row &r = m_rows[m_current];
    if (r.status == Saving || r.status == Saved)
        return;
    if (Library *lib = Library::instance())
        lib->setAlbumArtSkipped(r.album.albumId, true);
    m_finder.drop(r.album.albumId); // don't spend MusicBrainz requests on it
    r.status = Skipped;
    rowChanged(m_current);
}

void ArtFinder::retry()
{
    if (m_current < 0 || m_current >= m_rows.size())
        return;
    Row &r = m_rows[m_current];
    if (r.status == Saving)
        return;
    if (r.status == Skipped) {
        if (Library *lib = Library::instance())
            lib->setAlbumArtSkipped(r.album.albumId, false);
    }
    r.status = Pending;
    r.error.clear();
    r.candidates.clear();
    r.coverUrl.clear();
    rowChanged(m_current);
    m_finder.retry(r.album.albumId);
}

int ArtFinder::nextOpenRow(int from) const
{
    const int n = static_cast<int>(m_rows.size());
    for (int i = 0; i < n; ++i) {
        const int row = ((from + i) % n + n) % n;
        const Status s = m_rows.at(row).status;
        if (s != Saved && s != Saving && s != Skipped)
            return row;
    }
    return -1;
}

void ArtFinder::setTestData(const QVariantList &albums)
{
    close();
    beginResetModel();
    for (const QVariant &v : albums) {
        const QVariantMap m = v.toMap();
        Row r;
        r.album.albumId = m.value(QStringLiteral("albumId")).toLongLong();
        r.album.title = m.value(QStringLiteral("title")).toString();
        r.album.artist = m.value(QStringLiteral("artist")).toString();
        r.album.trackCount = m.value(QStringLiteral("trackCount")).toInt();
        r.status = m_saving.contains(r.album.albumId) ? Saving
                                                      : static_cast<Status>(m.value(QStringLiteral("status"), Found).toInt());
        r.error = m.value(QStringLiteral("error")).toString();
        for (const QVariant &cv : m.value(QStringLiteral("candidates")).toList()) {
            const QVariantMap c = cv.toMap();
            mmm::ArtworkCandidate cand;
            cand.title = c.value(QStringLiteral("title")).toString();
            cand.artist = c.value(QStringLiteral("artist")).toString();
            cand.year = c.value(QStringLiteral("year")).toInt();
            cand.format = c.value(QStringLiteral("format")).toString();
            cand.trackCount = c.value(QStringLiteral("trackCount")).toInt();
            cand.fromReleaseGroup = c.value(QStringLiteral("fromReleaseGroup")).toBool();
            cand.previewUrl = c.value(QStringLiteral("previewUrl")).toUrl();
            cand.saveUrl = c.value(QStringLiteral("saveUrl")).toUrl();
            r.candidates << cand;
        }
        m_rows << r;
    }
    m_current = m_rows.isEmpty() ? -1 : 0;
    endResetModel();
    Q_EMIT countChanged();
    Q_EMIT currentChanged();
    Q_EMIT progressChanged();
}

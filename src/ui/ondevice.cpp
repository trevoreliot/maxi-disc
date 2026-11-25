// SPDX-License-Identifier: GPL-3.0-or-later
#include "ondevice.h"

#include "desktop.h"

#include "database.h"
#include "devices.h"
#include "library.h"
#include "libraryquery.h"
#include "pathlayout.h"
#include "sync.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QJSEngine>

#include <algorithm>
#include <map>

namespace {
OnDevice *s_instance = nullptr;
const QChar kSep(0x1F);
} // namespace

// --- OnDeviceModel ---

OnDeviceModel::OnDeviceModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(200);
    connect(&m_rebuildTimer, &QTimer::timeout, this, &OnDeviceModel::rebuild);
}

int OnDeviceModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant OnDeviceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    const mmm::DeviceTrack &t = r.track;
    switch (role) {
    case KindRole:
        return int(r.kind);
    case KeyRole:
        return r.key;
    case LabelRole:
        return r.label;
    case DetailRole:
        return r.detail;
    case DepthRole:
        return int(r.kind);
    case ExpandedRole:
        return r.kind != TrackRow && !m_collapsed.contains(r.key);
    case TrackCountRole:
        return r.trackCount;
    case SizeTextRole:
        return Devices::formatBytes(r.bytes);
    case NotInLibraryCountRole:
        return r.notInLibrary;
    case TrackNumberRole:
        return t.trackNumber;
    case DiscNumberRole:
        return t.discNumber;
    case DurationRole:
        return t.durationMs;
    case CodecRole:
        return t.codec.isEmpty() ? QFileInfo(t.relPath).suffix().toUpper() : t.codec;
    case RelPathRole:
        return t.relPath;
    case InLibraryRole:
        return r.kind == TrackRow ? t.inLibrary : r.notInLibrary == 0;
    case SyncedRole:
        return t.synced;
    }
    return {};
}

QHash<int, QByteArray> OnDeviceModel::roleNames() const
{
    return {{KindRole, "kind"},
            {KeyRole, "key"},
            {LabelRole, "label"},
            {DetailRole, "detail"},
            {DepthRole, "depth"},
            {ExpandedRole, "expanded"},
            {TrackCountRole, "trackCount"},
            {SizeTextRole, "sizeText"},
            {NotInLibraryCountRole, "notInLibraryCount"},
            {TrackNumberRole, "trackNumber"},
            {DiscNumberRole, "discNumber"},
            {DurationRole, "durationMs"},
            {CodecRole, "codec"},
            {RelPathRole, "relPath"},
            {InLibraryRole, "inLibrary"},
            {SyncedRole, "synced"}};
}

QString OnDeviceModel::totalSizeText() const
{
    return Devices::formatBytes(m_bytes);
}

void OnDeviceModel::setCurrentIndex(int row)
{
    row = m_rows.isEmpty() ? -1 : std::clamp(row, 0, static_cast<int>(m_rows.size()) - 1);
    m_currentKey = row >= 0 ? m_rows.at(row).key : QString();
    if (row == m_current)
        return;
    m_current = row;
    Q_EMIT currentIndexChanged();
}

void OnDeviceModel::move(int delta)
{
    if (!m_rows.isEmpty())
        setCurrentIndex(std::max(0, m_current) + delta);
}

void OnDeviceModel::toggle(int row)
{
    if (row < 0 || row >= m_rows.size() || m_rows.at(row).kind == TrackRow)
        return;
    const QString key = m_rows.at(row).key;
    if (!m_collapsed.remove(key))
        m_collapsed.insert(key);
    m_currentKey = key; // the cursor stays on the header it folded
    rebuild();
}

QStringList OnDeviceModel::relPathsAt(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return {};
    const Row &r = m_rows.at(row);
    return r.kind == TrackRow ? QStringList{r.track.relPath} : r.relPaths;
}

int OnDeviceModel::indexOfKey(const QString &key) const
{
    for (qsizetype i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).key == key)
            return static_cast<int>(i);
    }
    return -1;
}

void OnDeviceModel::clear()
{
    m_rebuildTimer.stop();
    m_tracks.clear();
    m_seen.clear();
    m_collapsed.clear();
    m_currentKey.clear();
    rebuild();
}

void OnDeviceModel::beginBrowse()
{
    m_seen.clear();
}

void OnDeviceModel::addTracks(const QList<mmm::DeviceTrack> &tracks)
{
    for (const mmm::DeviceTrack &t : tracks) {
        const QString k = mmm::Database::pathKey(t.relPath);
        m_tracks.insert(k, t);
        m_seen.insert(k);
    }
    // The first results show at once; later ones a few times a second.
    if (m_rows.isEmpty())
        rebuild();
    else if (!m_rebuildTimer.isActive())
        m_rebuildTimer.start();
}

void OnDeviceModel::endBrowse()
{
    m_tracks.removeIf([this](const QHash<QString, mmm::DeviceTrack>::iterator it) { return !m_seen.contains(it.key()); });
    rebuild();
}

void OnDeviceModel::updateStats()
{
    int notInLibrary = 0;
    int synced = 0;
    qint64 bytes = 0;
    for (const mmm::DeviceTrack &t : std::as_const(m_tracks)) {
        notInLibrary += t.inLibrary ? 0 : 1;
        synced += t.synced ? 1 : 0;
        bytes += t.size;
    }
    m_notInLibrary = notInLibrary;
    m_synced = synced;
    m_bytes = bytes;
    Q_EMIT statsChanged();
}

void OnDeviceModel::rebuild()
{
    m_rebuildTimer.stop();
    // Artist → album → tracks. Names compare like the library's (leading "The " ignored,
    // locale-aware); "Unknown …" go last.
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    struct Album
    {
        QString name;
        int year = 0;
        QList<const mmm::DeviceTrack *> tracks;
    };
    struct Artist
    {
        QString name;
        std::map<QString, Album> albums; // by key
    };
    std::map<QString, Artist> artists;
    for (const mmm::DeviceTrack &t : std::as_const(m_tracks)) {
        const QString artistName = !t.albumArtist.trimmed().isEmpty() ? t.albumArtist
            : !t.artist.trimmed().isEmpty()                            ? t.artist
                                                                       : QString();
        const QString albumName = t.album.trimmed();
        Artist &artist = artists[artistName.simplified().toCaseFolded()];
        artist.name = artistName.isEmpty() ? QStringLiteral("Unknown Artist") : artistName;
        Album &album = artist.albums[albumName.simplified().toCaseFolded()];
        album.name = albumName.isEmpty() ? QStringLiteral("Unknown Album") : albumName;
        album.year = std::max(album.year, t.year);
        album.tracks << &t;
    }
    auto sortKey = [](const QString &groupKey, const QString &name) {
        return groupKey.isEmpty() ? QString() : mmm::LibraryQuery::sortName(name);
    };
    QList<std::pair<QString, Artist *>> artistOrder;
    for (auto &[k, a] : artists)
        artistOrder.emplace_back(k, &a);
    std::sort(artistOrder.begin(), artistOrder.end(), [&](const auto &x, const auto &y) {
        if (x.first.isEmpty() != y.first.isEmpty())
            return y.first.isEmpty(); // Unknown Artist last
        return collator.compare(sortKey(x.first, x.second->name), sortKey(y.first, y.second->name)) < 0;
    });

    QList<Row> rows;
    for (const auto &[artistKey, artist] : std::as_const(artistOrder)) {
        Row header;
        header.kind = ArtistRow;
        header.key = QStringLiteral("a") + kSep + artistKey;
        header.label = artist->name;
        const qsizetype headerAt = rows.size();
        rows << header;
        QList<std::pair<QString, Album *>> albumOrder;
        for (auto &[k, al] : artist->albums)
            albumOrder.emplace_back(k, &al);
        std::sort(albumOrder.begin(), albumOrder.end(), [&](const auto &x, const auto &y) {
            if (x.first.isEmpty() != y.first.isEmpty())
                return y.first.isEmpty(); // Unknown Album last
            if (x.second->year != y.second->year)
                return x.second->year < y.second->year;
            return collator.compare(x.second->name, y.second->name) < 0;
        });
        const bool artistOpen = !m_collapsed.contains(header.key);
        for (const auto &[albumKey, album] : std::as_const(albumOrder)) {
            std::sort(album->tracks.begin(), album->tracks.end(), [&](const mmm::DeviceTrack *a, const mmm::DeviceTrack *b) {
                if (a->discNumber != b->discNumber)
                    return a->discNumber < b->discNumber;
                if (a->trackNumber != b->trackNumber)
                    return a->trackNumber < b->trackNumber;
                return collator.compare(a->relPath, b->relPath) < 0;
            });
            Row albumRow;
            albumRow.kind = AlbumRow;
            albumRow.key = QStringLiteral("b") + kSep + artistKey + kSep + albumKey;
            albumRow.label = album->name;
            albumRow.detail = album->year > 0 ? QString::number(album->year) : QString();
            for (const mmm::DeviceTrack *t : std::as_const(album->tracks)) {
                albumRow.trackCount += 1;
                albumRow.bytes += t->size;
                albumRow.notInLibrary += t->inLibrary ? 0 : 1;
                albumRow.relPaths << t->relPath;
            }
            rows[headerAt].relPaths << albumRow.relPaths;
            rows[headerAt].trackCount += albumRow.trackCount;
            rows[headerAt].bytes += albumRow.bytes;
            rows[headerAt].notInLibrary += albumRow.notInLibrary;
            if (!artistOpen)
                continue;
            const bool albumOpen = !m_collapsed.contains(albumRow.key);
            rows << albumRow;
            if (!albumOpen)
                continue;
            for (const mmm::DeviceTrack *t : std::as_const(album->tracks)) {
                Row tr;
                tr.kind = TrackRow;
                tr.key = QStringLiteral("t") + kSep + mmm::Database::pathKey(t->relPath);
                tr.label = !t->title.trimmed().isEmpty() ? t->title : QFileInfo(t->relPath).completeBaseName();
                // A compilation's track artist, when it isn't the group's.
                const QString artistName = t->artist.trimmed();
                if (!artistName.isEmpty() && artistName.simplified().toCaseFolded() != artistKey)
                    tr.detail = artistName;
                tr.trackCount = 1;
                tr.bytes = t->size;
                tr.notInLibrary = t->inLibrary ? 0 : 1;
                tr.track = *t;
                rows << tr;
            }
        }
    }

    const int oldCount = static_cast<int>(m_rows.size());
    const int oldIndex = m_current;
    beginResetModel();
    m_rows = std::move(rows);
    // The cursor stays on its row (by key), else at the same position; settled (with the
    // totals) before anyone hears about the new rows.
    int row = m_currentKey.isEmpty() ? -1 : indexOfKey(m_currentKey);
    if (row < 0 && !m_rows.isEmpty())
        row = std::clamp(oldIndex, 0, static_cast<int>(m_rows.size()) - 1);
    m_current = row;
    m_currentKey = row >= 0 ? m_rows.at(row).key : QString();
    endResetModel();
    updateStats();
    if (oldCount != m_rows.size())
        Q_EMIT countChanged();
    Q_EMIT currentIndexChanged(); // the view's index was reset with the model: always
}

// --- OnDevice ---

OnDevice::OnDevice(const Options &options, QObject *parent)
    : QObject(parent)
    , m_model(std::make_unique<OnDeviceModel>())
    , m_browser(std::make_unique<mmm::DeviceBrowser>(options.databasePath))
{
    Q_ASSERT_X(!s_instance, "OnDevice", "only one OnDevice may exist");
    Q_ASSERT_X(Devices::instance(), "OnDevice", "construct Devices before OnDevice");
    s_instance = this;
    Devices *devices = Devices::instance();
    connect(devices, &Devices::currentChanged, this, [this] { update(); });
    connect(devices->manager(), &mmm::DeviceManager::devicesChanged, this, [this] { update(); });
    connect(devices->manager(), &mmm::DeviceManager::profileChanged, this, [this] { update(); });
    if (Library *library = Library::instance())
        connect(library, &Library::changed, this, [this] {
            m_browseKey.clear(); // "not in library" marks may change: read again when shown
            update();
        });
    if (Sync *sync = Sync::instance()) {
        m_paused = sync->state() == Sync::Syncing;
        connect(sync, &Sync::stateChanged, this, [this, sync] {
            const bool syncing = sync->state() == Sync::Syncing;
            if (syncing == m_paused)
                return;
            m_paused = syncing;
            m_browseKey.clear(); // the sync changes the device: read again when shown
            Q_EMIT stateChanged();
            update();
        });
    }

    connect(m_browser.get(), &mmm::DeviceBrowser::busyChanged, this, &OnDevice::stateChanged);
    connect(m_browser.get(), &mmm::DeviceBrowser::discovered, this, [this](int total, const QString &dir, bool fellBack) {
        m_total = total;
        m_scannedDir = dir;
        m_fellBack = fellBack;
        Q_EMIT stateChanged();
        Q_EMIT progressChanged();
    });
    connect(m_browser.get(), &mmm::DeviceBrowser::batchReady, this, [this](const QList<mmm::DeviceTrack> &batch, int done, int) {
        m_model->addTracks(batch);
        m_done = done;
        Q_EMIT progressChanged();
    });
    connect(m_browser.get(), &mmm::DeviceBrowser::finished, this, [this](const mmm::DeviceBrowseSummary &) {
        m_model->endBrowse();
        m_done = m_total;
        Q_EMIT progressChanged();
    });
    connect(m_browser.get(), &mmm::DeviceBrowser::failed, this, [this](const QString &error) {
        m_error = error;
        m_browseKey.clear(); // try again next time
        m_scannedDir.clear();
        m_fellBack = false;
        Q_EMIT stateChanged();
    });
    update();
}

OnDevice::~OnDevice()
{
    m_browser.reset();
    s_instance = nullptr;
}

OnDevice *OnDevice::instance()
{
    return s_instance;
}

OnDevice *OnDevice::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(s_instance, "OnDevice::create", "construct OnDevice before loading QML");
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void OnDevice::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    Q_EMIT activeChanged();
    update();
}

void OnDevice::setWholeDevice(bool whole)
{
    if (m_wholeDevice == whole)
        return;
    m_wholeDevice = whole;
    Q_EMIT stateChanged();
    update();
}

bool OnDevice::canScanWholeDevice() const
{
    // A music folder that exists: then "whole device" means more.
    return !m_profileMusicDir.isEmpty() && !m_fellBack;
}

QString OnDevice::pathAt(int row) const
{
    const QStringList rel = m_model->relPathsAt(row);
    if (m_mountPoint.isEmpty() || rel.isEmpty())
        return {};
    const QDir root(m_mountPoint);
    if (m_model->data(m_model->index(row), OnDeviceModel::KindRole).toInt() == OnDeviceModel::TrackRow)
        return root.filePath(rel.first());
    QStringList files;
    for (const QString &r : rel)
        files << root.filePath(r);
    return Desktop::commonFolder(files);
}

void OnDevice::refresh()
{
    update(true);
}

void OnDevice::update(bool force)
{
    Devices *devices = Devices::instance();
    const QVariantMap current = devices->current();
    const QString objectPath = current.value(QStringLiteral("objectPath")).toString();
    const auto entry = objectPath.isEmpty() ? std::nullopt : devices->manager()->device(objectPath);

    // Another device: start over.
    const QString deviceKey = entry ? entry->device.id + kSep + entry->device.objectPath : QString();
    if (deviceKey != m_deviceKey) {
        m_deviceKey = deviceKey;
        m_browser->cancel();
        m_model->clear();
        m_browseKey.clear();
        m_shownKey.clear();
        m_wholeDevice = false;
        m_error.clear();
        m_done = 0;
        m_total = -1;
        m_scannedDir.clear();
        m_fellBack = false;
        Q_EMIT stateChanged();
        Q_EMIT progressChanged();
    }
    m_profileMusicDir = entry && entry->profile ? mmm::PathLayout::cleanFolder(entry->profile->musicDir) : QString();

    const bool mounted = entry && entry->device.mounted();
    if (!mounted && (!m_browseKey.isEmpty() || !m_shownKey.isEmpty() || m_model->trackCount() > 0 || !m_error.isEmpty())) {
        // Unmounted: what it showed may change elsewhere. Start fresh when it's back.
        m_browser->cancel();
        m_model->clear();
        m_browseKey.clear();
        m_shownKey.clear();
        m_error.clear();
        m_done = 0;
        m_total = -1;
        m_scannedDir.clear();
        m_fellBack = false;
        Q_EMIT progressChanged();
    }
    if (!m_active || !mounted || m_paused) {
        if (m_browser->isBusy()) {
            m_browser->cancel();
            m_browseKey.clear(); // unfinished: read again when shown
        }
        Q_EMIT stateChanged();
        return;
    }
    const QString browseKey = deviceKey + kSep + entry->device.mountPoint + kSep + m_profileMusicDir + kSep
        + (m_wholeDevice ? QStringLiteral("1") : QStringLiteral("0"));
    if (!force && browseKey == m_browseKey)
        return;
    if (browseKey != m_shownKey)
        m_model->clear(); // another folder (or whole device): a fresh list
    m_browseKey = browseKey;
    m_shownKey = browseKey;
    m_error.clear();
    m_done = 0;
    m_total = -1;
    m_model->beginBrowse();
    m_mountPoint = entry->device.mountPoint;
    m_browser->start(mmm::DeviceBrowseRequest{entry->device.id, entry->device.mountPoint, m_profileMusicDir, m_wholeDevice});
    Q_EMIT stateChanged();
    Q_EMIT progressChanged();
}

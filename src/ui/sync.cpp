// SPDX-License-Identifier: GPL-3.0-or-later
#include "sync.h"

#include "devices.h"
#include "library.h"

#include <QJSEngine>

namespace {
Sync *s_instance = nullptr;

QString kindLabel(mmm::SyncItem::Kind k)
{
    switch (k) {
    case mmm::SyncItem::Kind::Playlist:
        return QStringLiteral("Playlist");
    case mmm::SyncItem::Kind::Album:
        return QStringLiteral("Album");
    case mmm::SyncItem::Kind::Artist:
        return QStringLiteral("Artist");
    case mmm::SyncItem::Kind::Genre:
        return QStringLiteral("Genre");
    }
    return {};
}

QString reasonText(mmm::SyncAction::Reason r)
{
    using R = mmm::SyncAction::Reason;
    switch (r) {
    case R::New:
        return QStringLiteral("new");
    case R::MissingOnDevice:
        return QStringLiteral("missing on the device");
    case R::SourceChanged:
        return QStringLiteral("changed in the library");
    case R::ProfileChanged:
        return QStringLiteral("profile settings changed");
    case R::ChangedOnDevice:
        return QStringLiteral("changed on the device");
    case R::LayoutChanged:
        return QStringLiteral("new folder layout");
    case R::Deselected:
        return QStringLiteral("no longer selected");
    case R::PlaylistChanged:
        return QStringLiteral("playlist changed");
    case R::GoneFromDevice:
        return QStringLiteral("already gone from the device");
    case R::KeptUserChanges:
        return QStringLiteral("changed on the device: left in place");
    }
    return {};
}

} // namespace

// --- SyncSelectionModel ---

int SyncSelectionModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}

QVariant SyncSelectionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const mmm::SyncItem &item = m_items.at(index.row());
    switch (role) {
    case ItemIdRole:
        return static_cast<int>(item.id);
    case KindRole:
        return kindLabel(item.kind);
    case NameRole:
        return item.name;
    case TrackCountRole:
        return m_isResolved ? m_resolved.trackCounts.value(item.id, 0) : -1;
    case MissingRole:
        return m_isResolved && m_resolved.missing.contains(item.name) && m_resolved.trackCounts.value(item.id, 0) == 0;
    }
    return {};
}

QHash<int, QByteArray> SyncSelectionModel::roleNames() const
{
    return {{ItemIdRole, "itemId"}, {KindRole, "kind"}, {NameRole, "name"}, {TrackCountRole, "trackCount"},
            {MissingRole, "missing"}};
}

void SyncSelectionModel::setItems(const QList<mmm::SyncItem> &items)
{
    const bool countChanges = items.size() != m_items.size();
    beginResetModel();
    m_items = items;
    m_isResolved = false;
    endResetModel();
    if (countChanges)
        Q_EMIT countChanged();
}

void SyncSelectionModel::setResolved(const mmm::ResolvedSelection &resolved)
{
    m_resolved = resolved;
    m_isResolved = true;
    if (!m_items.isEmpty())
        Q_EMIT dataChanged(index(0), index(rowCount() - 1), {TrackCountRole, MissingRole});
}

// --- SyncTrackModel ---

int SyncTrackModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_tracks.size());
}

QVariant SyncTrackModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_tracks.size())
        return {};
    const mmm::TrackRow &t = m_tracks.at(index.row());
    const bool transcode = m_profile && mmm::DeviceProfiles::decide(t.codec, *m_profile).transcode;
    switch (role) {
    case TitleRole:
        return t.title;
    case ArtistRole:
        return t.artist.isEmpty() ? t.albumArtist : t.artist;
    case AlbumRole:
        return t.album;
    case CodecRole:
        return t.codec;
    case TargetRole:
        return transcode ? QStringLiteral("MP3") : t.codec;
    case TranscodeRole:
        return transcode;
    case DurationRole:
        return t.durationMs;
    }
    return {};
}

QHash<int, QByteArray> SyncTrackModel::roleNames() const
{
    return {{TitleRole, "title"},   {ArtistRole, "artist"},       {AlbumRole, "album"},      {CodecRole, "codec"},
            {TargetRole, "target"}, {TranscodeRole, "transcode"}, {DurationRole, "durationMs"}};
}

void SyncTrackModel::setTracks(const QList<mmm::TrackRow> &tracks, const std::optional<mmm::DeviceProfile> &profile)
{
    const bool countChanges = tracks.size() != m_tracks.size();
    beginResetModel();
    m_tracks = tracks;
    m_profile = profile;
    endResetModel();
    if (countChanges)
        Q_EMIT countChanged();
}

qint64 SyncTrackModel::totalDurationMs() const
{
    qint64 total = 0;
    for (const mmm::TrackRow &t : m_tracks)
        total += t.durationMs;
    return total;
}

// --- SyncPlanModel ---

int SyncPlanModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_actions.size());
}

QVariant SyncPlanModel::data(const QModelIndex &index, int role) const
{
    using K = mmm::SyncAction::Kind;
    if (!index.isValid() || index.row() >= m_actions.size())
        return {};
    const mmm::SyncAction &a = m_actions.at(index.row());
    const bool playlist = a.itemKind == mmm::ManifestEntry::Kind::Playlist;
    switch (role) {
    case KindRole:
        switch (a.kind) {
        case K::Add:
            return QStringLiteral("add");
        case K::Update:
            return QStringLiteral("update");
        case K::Move:
            return QStringLiteral("move");
        case K::Remove:
            return QStringLiteral("remove");
        case K::Forget:
            return QStringLiteral("forget");
        }
        return {};
    case LabelRole:
        switch (a.kind) {
        case K::Add:
            return a.transcode ? QStringLiteral("Convert") : QStringLiteral("Copy");
        case K::Update:
            return QStringLiteral("Update");
        case K::Move:
            return QStringLiteral("Move");
        case K::Remove:
            return QStringLiteral("Remove");
        case K::Forget:
            return QStringLiteral("Skip");
        }
        return {};
    case TitleRole:
        if (playlist)
            return QStringLiteral("Playlist: %1").arg(a.playlistName.isEmpty() ? a.track.title : a.playlistName);
        return a.track.title.isEmpty() ? (a.destRelPath.isEmpty() ? a.oldRelPath : a.destRelPath) : a.track.title;
    case DetailRole: {
        QStringList parts;
        const QString artist = a.track.artist.isEmpty() ? a.track.albumArtist : a.track.artist;
        if (!playlist && !artist.isEmpty())
            parts << artist;
        if (!playlist && !a.track.album.isEmpty())
            parts << a.track.album;
        if (a.kind != K::Add || a.reason != mmm::SyncAction::Reason::New)
            parts << reasonText(a.reason);
        return parts.join(QStringLiteral(" · "));
    }
    case PathRole:
        return a.destRelPath.isEmpty() ? a.oldRelPath : a.destRelPath;
    case SizeTextRole:
        return a.bytes > 0 ? Devices::formatBytes(a.bytes) : a.oldBytes > 0 ? QStringLiteral("−") + Devices::formatBytes(a.oldBytes) : QString();
    case PlaylistRole:
        return playlist;
    }
    return {};
}

QHash<int, QByteArray> SyncPlanModel::roleNames() const
{
    return {{KindRole, "kind"},         {LabelRole, "label"},       {TitleRole, "title"},     {DetailRole, "detail"},
            {PathRole, "path"},         {SizeTextRole, "sizeText"}, {PlaylistRole, "playlist"}};
}

void SyncPlanModel::setActions(const QList<mmm::SyncAction> &actions)
{
    const bool countChanges = actions.size() != m_actions.size();
    beginResetModel();
    m_actions = actions;
    endResetModel();
    if (countChanges)
        Q_EMIT countChanged();
}

// --- Sync ---

Sync::Sync(const Options &options, QObject *parent)
    : QObject(parent)
    , m_selection(std::make_unique<SyncSelectionModel>())
    , m_tracks(std::make_unique<SyncTrackModel>())
    , m_planModel(std::make_unique<SyncPlanModel>())
    , m_job(std::make_unique<mmm::SyncJob>(mmm::SyncJob::Options{options.databasePath, options.cacheDir, 0, 0}))
{
    Q_ASSERT_X(!s_instance, "Sync", "only one Sync may exist");
    Q_ASSERT_X(Devices::instance(), "Sync", "construct Devices before Sync");
    s_instance = this;
    Devices *devices = Devices::instance();
    mmm::DeviceManager *manager = devices->manager();

    connect(devices, &Devices::currentChanged, this, &Sync::followCurrentDevice);
    connect(manager, &mmm::DeviceManager::devicesChanged, this, &Sync::followCurrentDevice);
    connect(manager, &mmm::DeviceManager::syncItemsChanged, this, [this](const QString &id) {
        if (id == m_deviceId) {
            invalidatePlan();
            reloadSelection();
        }
    });
    connect(manager, &mmm::DeviceManager::profileChanged, this, [this](const QString &id) {
        if (id == m_deviceId) {
            invalidatePlan();
            resolve();
            followCurrentDevice();
        }
    });
    if (Library *library = Library::instance()) {
        connect(library, &Library::changed, this, [this] {
            invalidatePlan();
            resolve();
        });
    }

    connect(m_job.get(), &mmm::SyncJob::busyChanged, this, &Sync::updateCan);
    connect(m_job.get(), &mmm::SyncJob::planReady, this, [this](const mmm::SyncPlan &plan) {
        if (m_state != Planning)
            return;
        const bool stale = m_planStale;
        m_planStale = false;
        m_plan = {};
        m_planModel->setActions({});
        // Planned for what's no longer selected, set up or mounted: drop it quietly.
        if (stale || plan.deviceId != m_deviceId) {
            setState(Idle);
            return;
        }
        if (!plan.isValid()) {
            if (!plan.canceled)
                setError(plan.error);
            setState(Idle);
            return;
        }
        if (!syncable(currentEntry(), plan.mountPoint)) {
            setState(Idle);
            return;
        }
        m_plan = plan;
        m_planModel->setActions(plan.actions);
        setState(Ready);
    });
    connect(m_job.get(), &mmm::SyncJob::progressChanged, this, [this](const mmm::SyncProgress &p) {
        m_progress = {
            {QStringLiteral("filesDone"), p.filesDone},
            {QStringLiteral("filesTotal"), p.filesTotal},
            {QStringLiteral("fraction"), p.bytesTotal > 0 ? std::min(1.0, double(p.bytesDone) / double(p.bytesTotal))
                                                          : (p.filesTotal > 0 ? double(p.filesDone) / p.filesTotal : 0.0)},
            {QStringLiteral("stage"), p.stage},
            {QStringLiteral("currentFile"), p.currentFile},
            {QStringLiteral("bytesText"), QStringLiteral("%1 of %2").arg(Devices::formatBytes(p.bytesDone),
                                                                       Devices::formatBytes(p.bytesTotal))},
        };
        Q_EMIT progressChanged();
    });
    connect(m_job.get(), &mmm::SyncJob::finished, this, [this](const mmm::SyncResult &r) {
        m_result = {
            {QStringLiteral("ok"), r.ok},
            {QStringLiteral("canceled"), r.canceled},
            {QStringLiteral("added"), r.added},
            {QStringLiteral("updated"), r.updated},
            {QStringLiteral("moved"), r.moved},
            {QStringLiteral("removed"), r.removed},
            {QStringLiteral("playlists"), r.playlistsWritten},
            {QStringLiteral("failed"), r.failed},
            {QStringLiteral("written"), Devices::formatBytes(r.bytesWritten)},
            {QStringLiteral("error"), r.error},
            {QStringLiteral("warnings"), r.warnings},
        };
        if (Devices *d = Devices::instance())
            d->manager()->refreshSpace();
        if (m_plan.deviceId != m_deviceId) {
            // The user moved on to another device: only an error is worth showing there.
            if (!r.error.isEmpty())
                setError(QStringLiteral("Sync to %1 stopped: %2").arg(m_plan.profile.name, r.error));
            m_plan = {};
            m_planModel->setActions({});
            setState(Idle);
            return;
        }
        if (!r.error.isEmpty())
            setError(QStringLiteral("Sync stopped: %1").arg(r.error));
        setState(Done);
    });
    followCurrentDevice();
}

Sync::~Sync()
{
    m_job.reset(); // cancels and waits for a running sync before the models go
    s_instance = nullptr;
}

Sync *Sync::instance()
{
    return s_instance;
}

Sync *Sync::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(s_instance, "Sync::create", "construct Sync before loading QML");
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

std::optional<mmm::DeviceEntry> Sync::currentEntry() const
{
    Devices *devices = Devices::instance();
    if (!devices || m_objectPath.isEmpty())
        return std::nullopt;
    return devices->manager()->device(m_objectPath);
}

void Sync::followCurrentDevice()
{
    const QVariantMap current = Devices::instance()->current();
    const QString id = current.value(QStringLiteral("deviceId")).toString();
    m_objectPath = current.value(QStringLiteral("objectPath")).toString();
    if (id != m_deviceId) {
        m_deviceId = id;
        if (m_state == Done)
            dismiss(); // another device's result
        else
            invalidatePlan(); // a running sync goes on (its device is in the title)
        Q_EMIT deviceChanged();
        reloadSelection();
    }
    // A shown or coming plan is for the device as it was: mounted there, writable, set up.
    if ((m_state == Ready || m_state == Planning) && !syncable(currentEntry(), m_plan.isValid() ? m_plan.mountPoint : QString()))
        invalidatePlan();
    updateCan();
}

void Sync::reloadSelection()
{
    m_selection->setItems(m_deviceId.isEmpty() ? QList<mmm::SyncItem>()
                                               : Devices::instance()->manager()->syncItems(m_deviceId));
    resolve();
}

void Sync::resolve()
{
    const quint64 generation = ++m_resolveGeneration;
    const QList<mmm::SyncItem> items = m_selection->items();
    const auto entry = currentEntry();
    const std::optional<mmm::DeviceProfile> profile = entry ? entry->profile : std::nullopt;
    Library *library = Library::instance();
    if (items.isEmpty() || !library) {
        m_selection->setResolved({});
        m_tracks->setTracks({}, profile);
        Q_EMIT tracksChanged();
        return;
    }
    library->runQuery([items](const mmm::LibraryQuery &q) { return mmm::SyncPlanner::resolve(q, items); })
        .then(this, [this, generation, profile](const mmm::ResolvedSelection &resolved) {
            if (generation != m_resolveGeneration)
                return;
            m_selection->setResolved(resolved);
            m_tracks->setTracks(resolved.tracks, profile);
            Q_EMIT tracksChanged();
        });
}

bool Sync::syncable(const std::optional<mmm::DeviceEntry> &entry, const QString &mountPoint)
{
    return entry && entry->profile && entry->device.mounted() && !entry->device.readOnly && !entry->device.busy
        && (mountPoint.isEmpty() || entry->device.mountPoint == mountPoint);
}

bool Sync::canPlan() const
{
    return m_state != Planning && m_state != Syncing && !m_job->isBusy() && syncable(currentEntry());
}

bool Sync::canSync() const
{
    return m_state == Ready && m_plan.isValid() && m_plan.hasWork() && !m_plan.insufficientSpace && !m_job->isBusy()
        && m_plan.deviceId == m_deviceId && syncable(currentEntry(), m_plan.mountPoint);
}

void Sync::updateCan()
{
    const bool plan = canPlan();
    if (plan != m_canPlan) {
        m_canPlan = plan;
        Q_EMIT canPlanChanged();
    }
    const bool sync = canSync();
    if (sync != m_canSync) {
        m_canSync = sync;
        Q_EMIT canSyncChanged();
    }
}

QVariantMap Sync::planSummary() const
{
    if (m_state != Ready && m_state != Syncing && m_state != Done)
        return {};
    const mmm::SyncPlan &p = m_plan;
    return {
        {QStringLiteral("deviceName"), p.profile.name},
        {QStringLiteral("adds"), p.adds},
        {QStringLiteral("updates"), p.updates},
        {QStringLiteral("moves"), p.moves},
        {QStringLiteral("removes"), p.removes},
        {QStringLiteral("unchanged"), p.unchanged},
        {QStringLiteral("playlists"), p.playlistWrites},
        {QStringLiteral("untracked"), p.untracked},
        {QStringLiteral("toWrite"), Devices::formatBytes(p.bytesToWrite)},
        {QStringLiteral("freed"), Devices::formatBytes(p.bytesFreed)},
        {QStringLiteral("freeAfter"), p.freeAfter >= 0 ? Devices::formatBytes(std::max<qint64>(0, p.freeAfter)) : QString()},
        {QStringLiteral("insufficientSpace"), p.insufficientSpace},
        {QStringLiteral("hasWork"), p.hasWork()},
        {QStringLiteral("warnings"), p.warnings},
        {QStringLiteral("missing"), p.missingItems},
        {QStringLiteral("error"), p.error},
    };
}

bool Sync::addItem(const QString &kind, int targetId, const QString &name)
{
    const auto k = mmm::SyncItem::kindFrom(kind);
    if (m_deviceId.isEmpty()) {
        setError(QStringLiteral("Select a device first."));
        return false;
    }
    if (!k || targetId <= 0 || m_state == Syncing)
        return false;
    QString error;
    if (!Devices::instance()->manager()->addSyncItem(mmm::SyncItem{0, m_deviceId, *k, targetId, name}, &error)) {
        setError(QStringLiteral("Couldn't add %1: %2").arg(name, error));
        return false;
    }
    return true;
}

void Sync::removeAt(int row)
{
    if (row < 0 || row >= m_selection->items().size() || m_state == Syncing)
        return;
    Devices::instance()->manager()->removeSyncItem(m_deviceId, m_selection->items().at(row).id);
}

void Sync::plan()
{
    if (!canPlan())
        return;
    const auto entry = currentEntry();
    if (!m_job->plan(entry->device, *entry->profile))
        return;
    clearError();
    m_result.clear();
    m_plan = {};
    m_planModel->setActions({});
    m_planStale = false;
    setState(Planning);
}

void Sync::run()
{
    if (!canSync())
        return;
    m_progress = {{QStringLiteral("filesDone"), 0}, {QStringLiteral("filesTotal"), 0}, {QStringLiteral("fraction"), 0.0},
                  {QStringLiteral("stage"), QStringLiteral("Starting")}, {QStringLiteral("currentFile"), QString()},
                  {QStringLiteral("bytesText"), QString()}};
    Q_EMIT progressChanged();
    clearError();
    if (m_job->run(m_plan))
        setState(Syncing);
}

void Sync::cancel()
{
    if (m_state == Planning || m_state == Syncing)
        m_job->cancel();
}

void Sync::dismiss()
{
    if (m_state != Ready && m_state != Done)
        return;
    m_plan = {};
    m_planModel->setActions({});
    setState(Idle);
}

void Sync::invalidatePlan()
{
    if (m_state == Planning) {
        m_planStale = true; // planReady drops it
        m_job->cancel();
    } else if (m_state == Ready) {
        m_plan = {};
        m_planModel->setActions({});
        setState(Idle);
    }
}

void Sync::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    Q_EMIT stateChanged();
    updateCan();
}

void Sync::setError(const QString &message)
{
    if (m_error == message)
        return;
    m_error = message;
    Q_EMIT errorStringChanged();
}

void Sync::clearError()
{
    setError(QString());
}

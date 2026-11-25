// SPDX-License-Identifier: GPL-3.0-or-later
#include "minidisc.h"

#include "atracencoder.h"

#include "devices.h"
#include "library.h"
#include "mdtitle.h"
#include "mduploadplanner.h"
#include "syncplanner.h"

#include <algorithm>
#include <QSet>

#include <QJSEngine>
#include <QSettings>

namespace {
MiniDisc *s_instance = nullptr;
}

// --- MdTrackModel ---

QString MdTrackModel::formatDuration(qint64 ms)
{
    const qint64 s = (ms + 500) / 1000;
    if (s >= 3600)
        return QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

int MdTrackModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_disc.tracks.size());
}

QVariant MdTrackModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_disc.tracks.size())
        return {};
    const mmm::MdTrack &t = m_disc.tracks.at(index.row());
    switch (role) {
    case NumberRole:
        return t.index + 1;
    case TitleRole:
        return t.title;
    case DurationTextRole:
        return formatDuration(t.durationMs);
    case ModeRole: {
        QString mode = t.encoding == mmm::MdEncoding::Sp ? QStringLiteral("SP")
            : t.encoding == mmm::MdEncoding::Lp2         ? QStringLiteral("LP2")
            : t.encoding == mmm::MdEncoding::Lp4         ? QStringLiteral("LP4")
                                                         : QString();
        if (t.mono)
            mode += QStringLiteral(" mono");
        return mode.trimmed();
    }
    case ProtectedRole:
        return t.protectedTrack;
    case GroupTitleRole:
        for (const mmm::MdGroup &g : m_disc.groups) {
            if (g.first == t.index)
                return g.title.isEmpty() ? QStringLiteral("Untitled group") : g.title;
        }
        return QString();
    case InGroupRole:
        return std::any_of(m_disc.groups.cbegin(), m_disc.groups.cend(), [&](const mmm::MdGroup &g) { return g.contains(t.index); });
    case MarkedRole:
        return m_marked.contains(index.row());
    }
    return {};
}

QHash<int, QByteArray> MdTrackModel::roleNames() const
{
    return {{NumberRole, "number"}, {TitleRole, "title"}, {DurationTextRole, "durationText"}, {ModeRole, "mode"},
            {ProtectedRole, "protectedTrack"}, {GroupTitleRole, "groupTitle"}, {InGroupRole, "inGroup"},
            {MarkedRole, "marked"}};
}

void MdTrackModel::setCurrentIndex(int row)
{
    row = m_disc.tracks.isEmpty() ? -1 : std::clamp(row, 0, static_cast<int>(m_disc.tracks.size()) - 1);
    if (row == m_current)
        return;
    m_current = row;
    Q_EMIT currentIndexChanged();
}

void MdTrackModel::move(int delta)
{
    if (!m_disc.tracks.isEmpty())
        setCurrentIndex(std::max(0, m_current) + delta);
}

QString MdTrackModel::titleAt(int row) const
{
    return row >= 0 && row < m_disc.tracks.size() ? m_disc.tracks.at(row).title : QString();
}

void MdTrackModel::toggleMark(int row)
{
    if (row < 0 || row >= m_disc.tracks.size())
        return;
    if (!m_marked.remove(row))
        m_marked.insert(row);
    Q_EMIT dataChanged(index(row), index(row), {MarkedRole});
    Q_EMIT marksChanged();
}

void MdTrackModel::clearMarks()
{
    if (m_marked.isEmpty())
        return;
    const QList<int> rows = markedRows();
    m_marked.clear();
    for (const int row : rows)
        Q_EMIT dataChanged(index(row), index(row), {MarkedRole});
    Q_EMIT marksChanged();
}

QList<int> MdTrackModel::markedRows() const
{
    QList<int> rows(m_marked.cbegin(), m_marked.cend());
    std::sort(rows.begin(), rows.end());
    return rows;
}

QList<int> MdTrackModel::deleteRows() const
{
    if (!m_marked.isEmpty())
        return markedRows();
    if (m_current >= 0 && m_current < m_disc.tracks.size())
        return {m_current};
    return {};
}

void MdTrackModel::setDisc(const mmm::MdDisc &disc)
{
    const bool sameTracks = disc.tracks.size() == m_disc.tracks.size();
    if (sameTracks && !m_disc.tracks.isEmpty()) {
        // A rename: same rows, new titles (the cursor stays).
        m_disc = disc;
        Q_EMIT dataChanged(index(0), index(rowCount() - 1));
        return;
    }
    beginResetModel();
    m_disc = disc;
    const bool hadMarks = !m_marked.isEmpty();
    m_marked.clear();
    endResetModel();
    Q_EMIT countChanged();
    if (hadMarks)
        Q_EMIT marksChanged();
    m_current = -2;
    setCurrentIndex(0);
}

// --- MdQueueModel ---

namespace {

QString stateName(mmm::MdItemState s)
{
    switch (s) {
    case mmm::MdItemState::Pending:
        return QStringLiteral("pending");
    case mmm::MdItemState::Decoding:
        return QStringLiteral("decoding");
    case mmm::MdItemState::Ready:
        return QStringLiteral("ready");
    case mmm::MdItemState::Uploading:
        return QStringLiteral("uploading");
    case mmm::MdItemState::Done:
        return QStringLiteral("done");
    case mmm::MdItemState::Failed:
        return QStringLiteral("failed");
    case mmm::MdItemState::Skipped:
        return QStringLiteral("skipped");
    }
    return {};
}

} // namespace

int MdQueueModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}

QVariant MdQueueModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const mmm::MdUploadItem &i = m_items.at(index.row());
    switch (role) {
    case NumberRole:
        return index.row() + 1;
    case TitleRole:
        return i.title;
    case ArtistRole:
        return i.track.artist.isEmpty() ? i.track.albumArtist : i.track.artist;
    case DurationTextRole:
        return MdTrackModel::formatDuration(i.track.durationMs);
    case StateRole:
        return stateName(i.state);
    case ErrorRole:
        return i.error;
    }
    return {};
}

QHash<int, QByteArray> MdQueueModel::roleNames() const
{
    return {{NumberRole, "number"}, {TitleRole, "title"}, {ArtistRole, "artist"}, {DurationTextRole, "durationText"},
            {StateRole, "itemState"}, {ErrorRole, "error"}};
}

void MdQueueModel::setCurrentIndex(int row)
{
    row = m_items.isEmpty() ? -1 : std::clamp(row, 0, static_cast<int>(m_items.size()) - 1);
    if (row == m_current)
        return;
    m_current = row;
    Q_EMIT currentIndexChanged();
}

void MdQueueModel::move(int delta)
{
    if (!m_items.isEmpty())
        setCurrentIndex(std::max(0, m_current) + delta);
}

void MdQueueModel::setItems(const QList<mmm::MdUploadItem> &items)
{
    const int old = m_current;
    beginResetModel();
    m_items = items;
    endResetModel();
    Q_EMIT countChanged();
    m_current = -2;
    setCurrentIndex(old < 0 ? 0 : old);
}

void MdQueueModel::updateStates(const QList<mmm::MdUploadItem> &items)
{
    if (items.size() != m_items.size()) {
        setItems(items);
        return;
    }
    m_items = items;
    if (!m_items.isEmpty())
        Q_EMIT dataChanged(index(0), index(rowCount() - 1), {StateRole, ErrorRole});
}

// --- MiniDisc ---

MiniDisc::MiniDisc(const Options &options, QObject *parent)
    : QObject(parent)
    , m_tracks(std::make_unique<MdTrackModel>())
    , m_manager(std::make_unique<mmm::NetMdManager>(mmm::NetMdManager::Options{options.backend, options.monitor, 2000, options.encoder, options.tempDir}))
    , m_queue(std::make_unique<MdQueueModel>())
{
    Q_ASSERT_X(!s_instance, "MiniDisc", "only one MiniDisc may exist");
    Q_ASSERT_X(Devices::instance(), "MiniDisc", "construct Devices before MiniDisc");
    s_instance = this;
    connect(m_manager.get(), &mmm::NetMdManager::devicesChanged, this, &MiniDisc::publishDevices);
    connect(m_manager.get(), &mmm::NetMdManager::stateChanged, this, [this] {
        Q_EMIT stateChanged();
        Q_EMIT statusChanged();
        Q_EMIT editableChanged();
    });
    connect(m_manager.get(), &mmm::NetMdManager::discChanged, this, [this] {
        m_tracks->setDisc(m_manager->disc());
        Q_EMIT discChanged();
        Q_EMIT statusChanged();
        Q_EMIT planChanged();
        Q_EMIT editableChanged();
    });
    connect(m_manager.get(), &mmm::NetMdManager::editFinished, this, [this](bool ok) {
        // The rows the marks were on aren't the same tracks any more.
        m_tracks->clearMarks();
        if (const int cursor = ok ? m_cursorAfterEdit : m_cursorIfFailed; cursor >= 0)
            m_tracks->setCurrentIndex(cursor);
        m_cursorAfterEdit = m_cursorIfFailed = -1;
    });
    connect(m_manager.get(), &mmm::NetMdManager::uploadChanged, this, [this] {
        if (m_manager->isUploading() || !m_manager->uploadItems().isEmpty())
            m_queue->updateStates(m_manager->uploadItems());
        Q_EMIT recordingChanged();
        Q_EMIT progressChanged();
        Q_EMIT statusChanged();
        Q_EMIT editableChanged();
    });
    connect(m_manager.get(), &mmm::NetMdManager::uploadProgressChanged, this, &MiniDisc::progressChanged);
    connect(m_manager.get(), &mmm::NetMdManager::uploadFinished, this, [this](bool ok) {
        const QList<mmm::MdUploadItem> items = m_manager->uploadItems();
        const auto count = [&](mmm::MdItemState s) {
            return std::count_if(items.cbegin(), items.cend(), [s](const mmm::MdUploadItem &i) { return i.state == s; });
        };
        const auto done = count(mmm::MdItemState::Done);
        if (ok)
            m_result = done == 1 ? QStringLiteral("Recorded 1 track.") : QStringLiteral("Recorded %1 tracks.").arg(done);
        else if (count(mmm::MdItemState::Failed) > 0)
            m_result = QStringLiteral("Stopped after %1 of %2 tracks: %3").arg(done).arg(items.size()).arg([&] {
                for (const mmm::MdUploadItem &i : items)
                    if (i.state == mmm::MdItemState::Failed)
                        return i.error;
                return QString();
            }());
        else
            m_result = QStringLiteral("Canceled: %1 of %2 tracks recorded.").arg(done).arg(items.size());
        Q_EMIT recordingChanged();
        Q_EMIT planChanged();
        followCurrentDevice(); // the user may have picked another device meanwhile
    });
    connect(m_manager.get(), &mmm::NetMdManager::errorChanged, this, [this] {
        Q_EMIT errorChanged();
        Q_EMIT statusChanged();
    });
    connect(Devices::instance(), &Devices::currentChanged, this, &MiniDisc::followCurrentDevice);

    // LP2 / LP4 (M12): the default encoder needs atracdenc; a given encoder (tests) does it all.
    m_lpAvailable = options.encoder ? true : mmm::AtracEncoder::isAvailable(&m_lpReason);
    m_persist = options.persistSettings;
    if (m_persist) {
        const QString saved = QSettings().value(QStringLiteral("minidisc/recordMode")).toString();
        if (m_lpAvailable && saved == QStringLiteral("lp2"))
            m_recordMode = mmm::MdRecordMode::Lp2;
        else if (m_lpAvailable && saved == QStringLiteral("lp4"))
            m_recordMode = mmm::MdRecordMode::Lp4;
    }
    m_manager->start();
    followCurrentDevice();
}

MiniDisc::~MiniDisc()
{
    if (Devices *d = Devices::instance())
        d->manager()->setNetMdDevices({});
    m_manager.reset();
    s_instance = nullptr;
}

MiniDisc *MiniDisc::instance()
{
    return s_instance;
}

MiniDisc *MiniDisc::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(s_instance, "MiniDisc::create", "construct MiniDisc before loading QML");
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void MiniDisc::publishDevices()
{
    QList<mmm::Device> list;
    for (const mmm::NetMdDevice &n : m_manager->devices()) {
        mmm::Device d;
        d.id = n.id;
        d.objectPath = n.id;
        d.kind = mmm::Device::Kind::NetMd;
        d.name = n.name;
        list << d;
    }
    Devices::instance()->manager()->setNetMdDevices(list);
}

void MiniDisc::followCurrentDevice()
{
    const QVariantMap current = Devices::instance()->current();
    const bool netMd = current.value(QStringLiteral("netMd")).toBool();
    const QString id = netMd ? current.value(QStringLiteral("objectPath")).toString() : QString();
    if (id != m_manager->selectedId()) {
        m_manager->select(id);
        Q_EMIT stateChanged();
    }
}

// --- Recording ---

QList<mmm::TrackRow> MiniDisc::queuedTracks() const
{
    QList<mmm::TrackRow> tracks;
    for (const mmm::MdUploadItem &i : m_queue->items()) {
        if (i.state != mmm::MdItemState::Done)
            tracks << i.track;
    }
    return tracks;
}

mmm::MdUploadPlan MiniDisc::currentPlan() const
{
    return mmm::MdUploadPlanner::plan(m_manager->disc(), m_manager->hasDisc(), queuedTracks(), m_recordMode);
}

QVariantMap MiniDisc::plan() const
{
    const mmm::MdUploadPlan p = currentPlan();
    const mmm::MdCapacity c = m_manager->disc().capacity;
    const double total = c.valid && c.totalMs > 0 ? double(c.totalMs) : 1.0;
    return {
        {QStringLiteral("canRecord"), p.canRecord},
        {QStringLiteral("error"), p.error},
        {QStringLiteral("warning"), p.warning},
        {QStringLiteral("durationText"), MdTrackModel::formatDuration(p.durationMs)},
        // In the mode's own time: an 80-minute disc has 160 minutes free in LP2.
        {QStringLiteral("neededText"), MdTrackModel::formatDuration(p.neededMs * mmm::mdModeFactor(p.mode))},
        {QStringLiteral("availableText"), MdTrackModel::formatDuration(p.availableMs * mmm::mdModeFactor(p.mode))},
        {QStringLiteral("modeName"), mmm::mdModeName(p.mode)},
        {QStringLiteral("queuedFraction"), std::clamp(double(p.neededMs) / total, 0.0, 1.0)},
        {QStringLiteral("overflow"), p.neededMs > p.availableMs},
        {QStringLiteral("suggestedDiscTitle"), p.suggestedDiscTitle},
        {QStringLiteral("pending"), static_cast<int>(queuedTracks().size())}, // not recorded yet
    };
}

void MiniDisc::setRecordView(bool on)
{
    if (m_recordView == on)
        return;
    m_recordView = on;
    Q_EMIT recordViewChanged();
}

void MiniDisc::setTitleDisc(bool on)
{
    if (m_titleDisc == on)
        return;
    m_titleDisc = on;
    Q_EMIT planChanged();
}

QVariantMap MiniDisc::progress() const
{
    const QList<mmm::MdUploadItem> items = m_manager->uploadItems();
    const int index = m_manager->uploadIndex();
    const mmm::MdTransferProgress p = m_manager->uploadProgress();
    const double track = p.bytesTotal > 0 ? std::clamp(double(p.bytesSent) / double(p.bytesTotal), 0.0, 1.0) : 0.0;
    QString stage;
    if (index >= 0 && index < items.size()) {
        switch (items.at(index).state) {
        case mmm::MdItemState::Pending: // its preparing hasn't started yet, or failed (shown in its turn)
        case mmm::MdItemState::Decoding:
        case mmm::MdItemState::Ready:
            stage = QStringLiteral("Preparing");
            break;
        case mmm::MdItemState::Uploading:
            stage = p.bytesTotal > 0 && p.bytesSent >= p.bytesTotal ? QStringLiteral("Finishing") : QStringLiteral("Recording");
            break;
        default:
            break;
        }
    }
    // Every track on the disc, the disc's title being written: all but done.
    const bool titling = m_manager->isUploading() && index < 0;
    if (titling)
        stage = QStringLiteral("Finishing");
    const double overall = titling ? 1.0 : items.isEmpty() ? 0.0 : (std::max(0, index) + track) / double(items.size());
    return {
        {QStringLiteral("trackText"), index >= 0 && index < items.size()
                                          ? QStringLiteral("Track %1 of %2: %3").arg(index + 1).arg(items.size()).arg(items.at(index).title)
                                          : QString()},
        {QStringLiteral("stage"), m_manager->isCancelling() ? QStringLiteral("Finishing this track, then stopping") : stage},
        {QStringLiteral("trackFraction"), track},
        {QStringLiteral("overallFraction"), std::clamp(overall, 0.0, 1.0)},
        {QStringLiteral("bytesText"), p.bytesTotal > 0 ? QStringLiteral("%1 of %2").arg(Devices::formatBytes(p.bytesSent),
                                                                                         Devices::formatBytes(p.bytesTotal))
                                                       : QString()},
    };
}

void MiniDisc::addItem(const QString &kind, int targetId, const QString &name)
{
    const auto k = mmm::SyncItem::kindFrom(kind);
    if (!k || targetId <= 0)
        return;
    addItems({mmm::SyncItem{0, QString(), *k, targetId, name}});
}

void MiniDisc::addItems(const QList<mmm::SyncItem> &items, const DeviceAddCallback &done)
{
    Library *library = Library::instance();
    const QString name = deviceName();
    auto report = [done, name](DeviceAddResult r) {
        r.deviceName = name;
        if (done)
            done(r);
    };
    if (!library || items.isEmpty()) {
        report(DeviceAddResult::failed(QStringLiteral("Nothing to add")));
        return;
    }
    if (isRecording()) {
        report(DeviceAddResult::failed(QStringLiteral("Wait for %1 to finish recording").arg(name)));
        return;
    }
    library->runQuery([items](const mmm::LibraryQuery &q) { return mmm::SyncPlanner::resolve(q, items); })
        .then(this, [this, report, name](const mmm::ResolvedSelection &resolved) {
            if (isRecording()) {
                report(DeviceAddResult::failed(QStringLiteral("Wait for %1 to finish recording").arg(name)));
                return;
            }
            DeviceAddResult r;
            r.missing = static_cast<int>(resolved.missing.size());
            // Tracks the last run recorded make way (like record() does): they can go on again.
            QList<mmm::MdUploadItem> queue;
            for (const mmm::MdUploadItem &i : m_queue->items()) {
                if (i.state != mmm::MdItemState::Done)
                    queue << i;
            }
            QSet<QString> queued;
            for (const mmm::MdUploadItem &i : std::as_const(queue))
                queued.insert(i.track.path);
            for (const mmm::TrackRow &t : resolved.tracks) {
                if (queued.contains(t.path)) {
                    ++r.skipped;
                    continue;
                }
                queued.insert(t.path);
                ++r.added;
                queue << mmm::MdUploadItem{t, mmm::MdUploadPlanner::titleFor(t, static_cast<int>(queue.size()) + 1),
                                           mmm::MdItemState::Pending, {}};
            }
            if (r.added > 0) {
                m_queue->setItems(queue);
                m_result.clear();
                setRecordView(true);
                Q_EMIT planChanged();
                Q_EMIT recordingChanged();
            }
            report(r);
        });
}

void MiniDisc::removeQueued(int row)
{
    if (isRecording() || row < 0 || row >= m_queue->items().size())
        return;
    QList<mmm::MdUploadItem> queue = m_queue->items();
    queue.removeAt(row);
    m_queue->setItems(queue);
    Q_EMIT planChanged();
}

void MiniDisc::clearQueue()
{
    if (isRecording())
        return;
    m_queue->setItems({});
    m_result.clear();
    Q_EMIT planChanged();
    Q_EMIT recordingChanged();
}

bool MiniDisc::record()
{
    if (isRecording() || !currentPlan().canRecord)
        return false;
    // Tracks recorded by an earlier run are on the disc already.
    QList<mmm::MdUploadItem> todo;
    for (const mmm::MdUploadItem &i : m_queue->items()) {
        if (i.state != mmm::MdItemState::Done)
            todo << mmm::MdUploadItem{i.track, i.title, mmm::MdItemState::Pending, {}};
    }
    const QString discTitle = m_titleDisc ? currentPlan().suggestedDiscTitle : QString();
    m_queue->setItems(todo);
    m_result.clear();
    if (!m_manager->startUpload(todo, discTitle, m_recordMode))
        return false;
    setRecordView(true);
    Q_EMIT recordingChanged();
    return true;
}

void MiniDisc::cancelRecord()
{
    m_manager->cancelUpload();
}

void MiniDisc::setRecordMode(int mode)
{
    if (mode < 0 || mode > 2 || isRecording())
        return;
    const auto m = static_cast<mmm::MdRecordMode>(mode);
    if (m != mmm::MdRecordMode::Sp && !m_lpAvailable)
        return;
    if (m == m_recordMode)
        return;
    m_recordMode = m;
    if (m_persist)
        QSettings().setValue(QStringLiteral("minidisc/recordMode"), recordModeName().toLower());
    Q_EMIT recordModeChanged();
    Q_EMIT planChanged();
}

void MiniDisc::cycleRecordMode()
{
    if (!m_lpAvailable)
        return;
    setRecordMode((recordMode() + 1) % 3);
}

QString MiniDisc::deviceName() const
{
    for (const mmm::NetMdDevice &n : m_manager->devices()) {
        if (n.id == m_manager->selectedId())
            return n.name;
    }
    return {};
}

QString MiniDisc::busyText() const
{
    switch (m_manager->state()) {
    case mmm::NetMdManager::State::Opening:
        return QStringLiteral("Reading the disc…");
    case mmm::NetMdManager::State::Writing:
        switch (m_manager->edit()) {
        case mmm::NetMdManager::Edit::Delete:
            return QStringLiteral("Deleting…");
        case mmm::NetMdManager::Edit::Move:
            return QStringLiteral("Moving the track…");
        case mmm::NetMdManager::Edit::Erase:
            return QStringLiteral("Erasing the disc…");
        case mmm::NetMdManager::Edit::None:
            break;
        }
        return QStringLiteral("Writing the title…");
    default:
        return {};
    }
}

QString MiniDisc::capacityText() const
{
    const mmm::MdCapacity c = m_manager->disc().capacity;
    if (!hasDisc() || !c.valid)
        return {};
    return QStringLiteral("%1 recorded · %2 free of %3")
        .arg(MdTrackModel::formatDuration(c.recordedMs), MdTrackModel::formatDuration(c.availableMs),
             MdTrackModel::formatDuration(c.totalMs));
}

double MiniDisc::usedFraction() const
{
    const mmm::MdCapacity c = m_manager->disc().capacity;
    return c.valid && c.totalMs > 0 ? std::clamp(double(c.totalMs - c.availableMs) / double(c.totalMs), 0.0, 1.0) : 0.0;
}

QString MiniDisc::statusText() const
{
    if (m_manager->isUploading())
        return QStringLiteral("Recording %1 of %2%3")
            .arg(std::max(0, m_manager->uploadIndex()) + 1)
            .arg(m_manager->uploadItems().size())
            .arg(m_manager->uploadMode() == mmm::MdRecordMode::Sp ? QString() : QStringLiteral(" in ") + mmm::mdModeName(m_manager->uploadMode()));
    if (m_manager->state() == mmm::NetMdManager::State::Opening)
        return QStringLiteral("Reading…");
    switch (m_manager->error()) {
    case mmm::NetMdError::None:
        break;
    case mmm::NetMdError::PermissionDenied:
        return QStringLiteral("Permission needed");
    case mmm::NetMdError::Busy:
        return QStringLiteral("In use by another program");
    case mmm::NetMdError::NoDisc:
        return QStringLiteral("No disc");
    default:
        if (!hasDisc())
            return QStringLiteral("Not readable");
        break;
    }
    if (!hasDisc())
        return {};
    const int n = trackCount();
    return n == 0 ? QStringLiteral("Blank disc") : n == 1 ? QStringLiteral("1 track") : QStringLiteral("%1 tracks").arg(n);
}

void MiniDisc::refresh()
{
    m_manager->refresh();
}

bool MiniDisc::deleteTracks()
{
    return deleteTrackRows(m_tracks->deleteRows());
}

bool MiniDisc::deleteTrackRows(const QList<int> &given)
{
    QList<int> rows = given;
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    if (rows.isEmpty() || !m_manager->deleteTracks(rows))
        return false;
    // Then: the track that took the first deleted one's place (or the new last one); if it
    // was refused, the cursor stays.
    m_cursorAfterEdit = rows.first();
    m_cursorIfFailed = m_tracks->currentIndex();
    return true;
}

bool MiniDisc::moveTrack(int delta)
{
    const int from = m_tracks->currentIndex();
    const int to = from + delta;
    if (from < 0 || !m_manager->moveTrack(from, to))
        return false;
    m_cursorAfterEdit = to; // the cursor goes with the track
    m_cursorIfFailed = from;
    return true;
}

bool MiniDisc::eraseDisc()
{
    if (!m_manager->eraseDisc())
        return false;
    m_cursorAfterEdit = m_cursorIfFailed = -1;
    return true;
}

void MiniDisc::renameDisc(const QString &title)
{
    m_manager->renameDisc(title);
}

void MiniDisc::renameTrack(int index, const QString &title)
{
    m_manager->renameTrack(index, title);
}

QVariantMap MiniDisc::preview(const QString &title, bool disc) const
{
    const auto c = mmm::MdTitle::clean(title, disc ? mmm::MdTitle::kMaxHeaderBytes : mmm::MdTitle::kMaxTrackTitleBytes);
    bool cut = c.truncated;
    QString text = c.text;
    if (disc) {
        // What withTitle() will keep, groups and all.
        bool headerCut = false;
        const QString header = mmm::MdTitle::withTitle(m_manager->disc().rawHeader, c.text, &headerCut);
        text = mmm::MdTitle::parseHeader(header).title;
        cut = cut || headerCut;
    }
    return {{QStringLiteral("text"), text}, {QStringLiteral("changed"), c.changed || text != c.text}, {QStringLiteral("truncated"), cut}};
}

void MiniDisc::setTestDevices(const QVariantList &devices)
{
    QList<mmm::NetMdDevice> list;
    for (const QVariant &v : devices) {
        const QVariantMap m = v.toMap();
        mmm::NetMdDevice d;
        d.id = m.value(QStringLiteral("id")).toString();
        d.name = m.value(QStringLiteral("name"), QStringLiteral("NetMD recorder")).toString();
        list << d;
    }
    m_manager->setDevicesForTesting(list);
}

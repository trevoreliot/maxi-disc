// SPDX-License-Identifier: GPL-3.0-or-later
#include "minidisc.h"

#include "devices.h"
#include "mdtitle.h"

#include <QJSEngine>

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
    }
    return {};
}

QHash<int, QByteArray> MdTrackModel::roleNames() const
{
    return {{NumberRole, "number"}, {TitleRole, "title"}, {DurationTextRole, "durationText"}, {ModeRole, "mode"},
            {ProtectedRole, "protectedTrack"}, {GroupTitleRole, "groupTitle"}, {InGroupRole, "inGroup"}};
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
    endResetModel();
    Q_EMIT countChanged();
    m_current = -2;
    setCurrentIndex(0);
}

// --- MiniDisc ---

MiniDisc::MiniDisc(const Options &options, QObject *parent)
    : QObject(parent)
    , m_tracks(std::make_unique<MdTrackModel>())
    , m_manager(std::make_unique<mmm::NetMdManager>(mmm::NetMdManager::Options{options.backend, options.monitor, 2000}))
{
    Q_ASSERT_X(!s_instance, "MiniDisc", "only one MiniDisc may exist");
    Q_ASSERT_X(Devices::instance(), "MiniDisc", "construct Devices before MiniDisc");
    s_instance = this;
    connect(m_manager.get(), &mmm::NetMdManager::devicesChanged, this, &MiniDisc::publishDevices);
    connect(m_manager.get(), &mmm::NetMdManager::stateChanged, this, [this] {
        Q_EMIT stateChanged();
        Q_EMIT statusChanged();
    });
    connect(m_manager.get(), &mmm::NetMdManager::discChanged, this, [this] {
        m_tracks->setDisc(m_manager->disc());
        Q_EMIT discChanged();
        Q_EMIT statusChanged();
    });
    connect(m_manager.get(), &mmm::NetMdManager::errorChanged, this, [this] {
        Q_EMIT errorChanged();
        Q_EMIT statusChanged();
    });
    connect(Devices::instance(), &Devices::currentChanged, this, &MiniDisc::followCurrentDevice);
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

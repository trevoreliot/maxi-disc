// SPDX-License-Identifier: GPL-3.0-or-later
#include "devices.h"

#include "libraryquery.h"
#include "pathlayout.h"

#include <QJSEngine>
#include <QLocale>

namespace {
Devices *s_instance = nullptr;
}

Devices::Devices(const Options &options, QObject *parent)
    : QAbstractListModel(parent)
    , m_manager(std::make_unique<mmm::DeviceManager>(
          mmm::DeviceManager::Options{options.databasePath, options.monitor, {}}))
{
    Q_ASSERT_X(!s_instance, "Devices", "only one Devices may exist");
    s_instance = this;
    connect(m_manager.get(), &mmm::DeviceManager::devicesChanged, this, &Devices::onDevicesChanged);
    connect(m_manager.get(), &mmm::DeviceManager::mountFinished, this,
            [this](const QString &id, bool ok, const QString &result) {
                if (!ok) {
                    const auto e = m_manager->device(id);
                    setError(QStringLiteral("Couldn't mount %1: %2").arg(e ? e->device.name : id, result));
                }
            });
    connect(m_manager.get(), &mmm::DeviceManager::unmountFinished, this,
            [this](const QString &id, bool ok, const QString &result) {
                if (!ok) {
                    const auto e = m_manager->device(id);
                    setError(QStringLiteral("Couldn't unmount %1: %2").arg(e ? e->device.name : id, result));
                }
            });
    m_manager->start();
}

Devices::~Devices()
{
    s_instance = nullptr;
}

Devices *Devices::instance()
{
    return s_instance;
}

Devices *Devices::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(s_instance, "Devices::create", "construct Devices before loading QML");
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

QString Devices::formatBytes(qint64 bytes)
{
    if (bytes < 0)
        return {};
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeSIFormat);
}

QVariantMap Devices::summarize(const mmm::DeviceProfile &p)
{
    namespace DP = mmm::DeviceProfiles;
    QString formats;
    if (p.playableCodecs == DP::allCodecs())
        formats = QStringLiteral("Every format, copied as it is");
    else if (p.playableCodecs.isEmpty())
        formats = QStringLiteral("None: everything is converted");
    else
        formats = p.playableCodecs.join(QStringLiteral(", "));
    const QString folder = mmm::PathLayout::cleanFolder(p.musicDir);
    return {
        {QStringLiteral("name"), p.name},
        {QStringLiteral("musicDir"), folder.isEmpty() ? QStringLiteral("(device root)") : folder},
        {QStringLiteral("layout"), p.pathLayout},
        {QStringLiteral("formats"), formats},
        {QStringLiteral("transcode"), DP::describe(p.transcodeTarget)},
        {QStringLiteral("artwork"), DP::describe(p.artworkPolicy)},
        {QStringLiteral("example"), mmm::PathLayout::render(mmm::PathLayout::sampleTrack(), p)},
    };
}

int Devices::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant Devices::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const mmm::DeviceEntry &e = m_rows.at(index.row());
    const mmm::Device &d = e.device;
    switch (role) {
    case DeviceIdRole: return d.id;
    case ObjectPathRole: return d.objectPath;
    case Qt::DisplayRole:
    case NameRole: return d.name;
    case NetMdRole: return d.kind == mmm::Device::Kind::NetMd;
    case DetailsRole: {
        if (d.kind == mmm::Device::Kind::NetMd)
            return QStringLiteral("MiniDisc recorder (NetMD)");
        QStringList parts;
        const QString vm = (d.vendor + QLatin1Char(' ') + d.model).trimmed();
        if (!vm.isEmpty() && vm != d.name)
            parts << vm;
        parts << d.fsType;
        if (d.readOnly)
            parts << QStringLiteral("read-only");
        return parts.join(QStringLiteral(" · "));
    }
    case MountedRole: return d.mounted();
    case MountPointRole: return d.mountPoint;
    case BusyRole: return d.busy;
    case ReadOnlyRole: return d.readOnly;
    case SpaceTextRole:
        if (d.kind == mmm::Device::Kind::NetMd)
            return QStringLiteral("NetMD");
        if (d.mounted() && d.freeBytes >= 0)
            return QStringLiteral("%1 free of %2").arg(formatBytes(d.freeBytes), formatBytes(d.totalBytes));
        if (d.mounted())
            return formatBytes(d.totalBytes);
        return QStringLiteral("%1 · not mounted").arg(formatBytes(d.totalBytes));
    case HasProfileRole: return e.profile.has_value();
    case ProfileNameRole: return e.profile ? e.profile->name : QString();
    default: return {};
    }
}

QHash<int, QByteArray> Devices::roleNames() const
{
    return {
        {DeviceIdRole, "deviceId"},   {ObjectPathRole, "objectPath"}, {NameRole, "name"},         {DetailsRole, "details"},
        {MountedRole, "mounted"},     {MountPointRole, "mountPoint"}, {BusyRole, "busy"},
        {ReadOnlyRole, "readOnly"},   {SpaceTextRole, "spaceText"},   {HasProfileRole, "hasProfile"},
        {ProfileNameRole, "profileName"}, {NetMdRole, "netMd"},
    };
}

void Devices::onDevicesChanged()
{
    // Selection by object path: two cloned sticks share a device id (filesystem UUID).
    const QString selected =
        m_current >= 0 && m_current < m_rows.size() ? m_rows.at(m_current).device.objectPath : QString();
    QList<mmm::DeviceEntry> next = m_manager->devices();
    auto rowOf = [&next](const QString &path) {
        for (int i = 0; i < next.size(); ++i) {
            if (next.at(i).device.objectPath == path)
                return i;
        }
        return -1;
    };
    // Keep the same device selected; else the first (a newly plugged drive is ready to set up).
    const int row = rowOf(selected);
    const int nextCurrent = row >= 0 ? row : (next.isEmpty() ? -1 : 0);
    const bool sameRows = next.size() == m_rows.size()
        && std::equal(next.cbegin(), next.cend(), m_rows.cbegin(),
                      [](const auto &a, const auto &b) { return a.device.objectPath == b.device.objectPath; });
    if (sameRows) {
        m_rows = std::move(next);
        m_current = nextCurrent;
        if (!m_rows.isEmpty())
            Q_EMIT dataChanged(index(0), index(static_cast<int>(m_rows.size()) - 1));
    } else {
        beginResetModel();
        m_rows = std::move(next);
        m_current = nextCurrent; // valid before views see the new rows
        endResetModel();
        Q_EMIT countChanged();
    }
    Q_EMIT currentChanged();
}

void Devices::setCurrentIndex(int row)
{
    row = m_rows.isEmpty() ? -1 : std::clamp(row, 0, static_cast<int>(m_rows.size()) - 1);
    if (row == m_current)
        return;
    m_current = row;
    Q_EMIT currentChanged();
}

QVariantMap Devices::current() const
{
    QVariantMap map;
    if (m_current < 0 || m_current >= m_rows.size())
        return map;
    const QHash<int, QByteArray> roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        map.insert(QString::fromUtf8(it.value()), data(index(m_current), it.key()));
    return map;
}

QVariantMap Devices::currentProfile() const
{
    if (m_current < 0 || m_current >= m_rows.size() || !m_rows.at(m_current).profile)
        return {};
    return summarize(*m_rows.at(m_current).profile);
}

void Devices::mount(const QString &device)
{
    m_manager->mount(device);
}

void Devices::unmount(const QString &device)
{
    m_manager->unmount(device);
}

void Devices::toggleMountCurrent()
{
    if (m_current < 0 || m_current >= m_rows.size())
        return;
    const mmm::Device &d = m_rows.at(m_current).device;
    if (d.busy || d.kind == mmm::Device::Kind::NetMd) // a recorder isn't mounted
        return;
    // By object path: unique even when two sticks share a filesystem UUID.
    if (d.mounted())
        unmount(d.objectPath);
    else
        mount(d.objectPath);
}

void Devices::setError(const QString &message)
{
    m_error = message;
    Q_EMIT errorStringChanged();
}

void Devices::clearError()
{
    if (m_error.isEmpty())
        return;
    m_error.clear();
    Q_EMIT errorStringChanged();
}

void Devices::setTestDevices(const QVariantList &devices)
{
    QList<mmm::Device> list;
    for (const QVariant &v : devices) {
        const QVariantMap m = v.toMap();
        mmm::Device d;
        d.id = m.value(QStringLiteral("id")).toString();
        d.objectPath = m.value(QStringLiteral("objectPath"), QStringLiteral("/test/") + d.id).toString();
        d.name = m.value(QStringLiteral("name")).toString();
        d.vendor = m.value(QStringLiteral("vendor")).toString();
        d.model = m.value(QStringLiteral("model")).toString();
        d.fsType = m.value(QStringLiteral("fsType"), QStringLiteral("vfat")).toString();
        d.mountPoint = m.value(QStringLiteral("mountPoint")).toString();
        d.totalBytes = m.value(QStringLiteral("totalBytes"), -1).toLongLong();
        d.freeBytes = m.value(QStringLiteral("freeBytes"), -1).toLongLong();
        d.readOnly = m.value(QStringLiteral("readOnly")).toBool();
        list << d;
    }
    m_manager->setDevicesForTesting(list);
}

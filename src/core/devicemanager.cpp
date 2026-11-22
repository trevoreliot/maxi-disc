// SPDX-License-Identifier: GPL-3.0-or-later
#include "devicemanager.h"

#include "database.h"
#include "pathlayout.h"

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcDevices)

namespace mmm {

DeviceManager::DeviceManager(Options options, QObject *parent)
    : QObject(parent), m_db(std::make_unique<Database>())
{
    QString error;
    if (!m_db->open(options.databasePath, &error))
        qCWarning(lcDevices).noquote() << "device profiles unavailable:" << error;
    if (options.monitor) {
        m_monitor = std::make_unique<UdisksMonitor>(std::move(options.udisks));
        connect(m_monitor.get(), &UdisksMonitor::devicesChanged, this, &DeviceManager::rebuild);
        connect(m_monitor.get(), &UdisksMonitor::mountFinished, this,
                [this](const QString &path, bool ok, const QString &result) {
                    Q_EMIT mountFinished(idFor(path), ok, result);
                });
        connect(m_monitor.get(), &UdisksMonitor::unmountFinished, this,
                [this](const QString &path, bool ok, const QString &result) {
                    Q_EMIT unmountFinished(idFor(path), ok, result);
                });
    }
}

DeviceManager::~DeviceManager() = default;

void DeviceManager::start()
{
    if (m_monitor)
        m_monitor->start();
}

void DeviceManager::setDevicesForTesting(const QList<Device> &devices)
{
    m_testMode = true;
    m_testDevices = devices;
    rebuild();
}

void DeviceManager::rebuild()
{
    const QList<Device> devices = m_testMode ? m_testDevices : m_monitor ? m_monitor->devices() : QList<Device>();
    QList<DeviceEntry> next;
    for (const Device &d : devices) {
        DeviceEntry e{d, m_db->isOpen() ? m_db->deviceProfile(d.id) : std::nullopt};
        if (e.profile && e.profile->lastSeenName != d.name) {
            m_db->setDeviceLastSeenName(d.id, d.name);
            e.profile->lastSeenName = d.name;
        }
        next << e;
    }
    if (next == m_entries)
        return;
    m_entries = std::move(next);
    Q_EMIT devicesChanged();
}

std::optional<DeviceEntry> DeviceManager::device(const QString &idOrObjectPath) const
{
    for (const DeviceEntry &e : m_entries) {
        if (e.device.objectPath == idOrObjectPath)
            return e;
    }
    for (const DeviceEntry &e : m_entries) {
        if (e.device.id == idOrObjectPath)
            return e;
    }
    return std::nullopt;
}

std::optional<DeviceProfile> DeviceManager::profile(const QString &deviceId) const
{
    return m_db->isOpen() ? m_db->deviceProfile(deviceId) : std::nullopt;
}

bool DeviceManager::saveProfile(DeviceProfile profile, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (profile.deviceId.isEmpty())
        return fail(QStringLiteral("No device chosen."));
    if (profile.name.trimmed().isEmpty())
        return fail(QStringLiteral("The profile needs a name."));
    if (const QString bad = PathLayout::validate(profile.pathLayout); !bad.isEmpty())
        return fail(bad);
    profile.musicDir = PathLayout::cleanFolder(profile.musicDir);
    // Canonical order, known codecs only.
    QStringList codecs;
    for (const QString &c : DeviceProfiles::allCodecs()) {
        if (profile.canPlay(c))
            codecs << c;
    }
    profile.playableCodecs = codecs;
    if (const auto d = device(profile.deviceId))
        profile.lastSeenName = d->device.name;
    QString dbError;
    if (!m_db->saveDeviceProfile(profile, &dbError))
        return fail(QStringLiteral("Could not save the profile: %1").arg(dbError));
    rebuild();
    Q_EMIT profileChanged(profile.deviceId);
    return true;
}

bool DeviceManager::deleteProfile(const QString &deviceId)
{
    const bool ok = m_db->deleteDeviceProfile(deviceId);
    if (ok) {
        rebuild();
        Q_EMIT profileChanged(deviceId);
    }
    return ok;
}

QList<SyncItem> DeviceManager::syncItems(const QString &deviceId) const
{
    return m_db->isOpen() ? m_db->syncItems(deviceId) : QList<SyncItem>();
}

bool DeviceManager::addSyncItem(const SyncItem &item, QString *error)
{
    if (!m_db->isOpen() || !m_db->addSyncItem(item, error))
        return false;
    Q_EMIT syncItemsChanged(item.deviceId);
    return true;
}

bool DeviceManager::removeSyncItem(const QString &deviceId, qint64 itemId)
{
    if (!m_db->isOpen() || !m_db->removeSyncItem(itemId))
        return false;
    Q_EMIT syncItemsChanged(deviceId);
    return true;
}

QString DeviceManager::objectPathFor(const QString &idOrObjectPath) const
{
    const auto e = device(idOrObjectPath);
    return e ? e->device.objectPath : QString();
}

QString DeviceManager::idFor(const QString &objectPath) const
{
    const auto e = device(objectPath);
    return e ? e->device.id : objectPath;
}

void DeviceManager::mount(const QString &idOrObjectPath)
{
    if (m_monitor)
        m_monitor->mount(objectPathFor(idOrObjectPath));
}

void DeviceManager::unmount(const QString &idOrObjectPath)
{
    if (m_monitor)
        m_monitor->unmount(objectPathFor(idOrObjectPath));
}

void DeviceManager::refreshSpace()
{
    if (m_monitor)
        m_monitor->refreshSpace();
}

} // namespace mmm

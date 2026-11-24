// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <memory>
#include <optional>

#include "device.h"
#include "deviceprofile.h"
#include "synctypes.h"
#include "udisksmonitor.h"

namespace mmm {

class Database;

// A connected device and its saved profile (if it has been set up).
struct DeviceEntry
{
    Device device;
    std::optional<DeviceProfile> profile;
    friend bool operator==(const DeviceEntry &, const DeviceEntry &) = default;
};

// The sync targets the app can see and their profiles: the core of the Export screen, and
// what the D-Bus interface (M16) will expose. Joins UdisksMonitor's devices with the
// profiles stored in the library database. Lives on one thread (normally the UI thread),
// with its own Database connection for these small reads/writes.
class DeviceManager : public QObject
{
    Q_OBJECT

public:
    struct Options
    {
        QString databasePath;
        bool monitor = true;                  // false: no device detection (tests, --no-devices)
        UdisksMonitor::Options udisks = {};
    };

    explicit DeviceManager(Options options, QObject *parent = nullptr);
    ~DeviceManager() override;

    // Starts device detection (devicesChanged() follows).
    void start();

    QList<DeviceEntry> devices() const { return m_entries; }
    // By object path (unique) or device id (the first device with that id: cloned sticks share one).
    std::optional<DeviceEntry> device(const QString &idOrObjectPath) const;

    std::optional<DeviceProfile> profile(const QString &deviceId) const;
    // Saves (inserts or replaces) a device's profile. Fails on an empty name or an invalid
    // layout (PathLayout::validate), with the reason in `error`.
    bool saveProfile(DeviceProfile profile, QString *error = nullptr);
    bool deleteProfile(const QString &deviceId);

    // The device's sync selection (M9): playlists / albums / artists / genres to put on it.
    QList<SyncItem> syncItems(const QString &deviceId) const;
    // Adds an entry (adding one that's there already is fine); returns false on failure.
    bool addSyncItem(const SyncItem &item, QString *error = nullptr);
    // Adds several in one transaction; returns how many were new (nullopt on failure).
    std::optional<int> addSyncItems(const QString &deviceId, const QList<SyncItem> &items, QString *error = nullptr);
    bool removeSyncItem(const QString &deviceId, qint64 itemId);

    void mount(const QString &idOrObjectPath);
    void unmount(const QString &idOrObjectPath);
    void refreshSpace();

    UdisksMonitor *monitor() const { return m_monitor.get(); }
    // Test hook: shows these devices instead of detected ones (no D-Bus).
    void setDevicesForTesting(const QList<Device> &devices);
    // NetMD recorders (M10; NetMdMonitor finds them, the UI passes them on): listed after the
    // USB devices, kind NetMd, no profile.
    void setNetMdDevices(const QList<Device> &devices);

Q_SIGNALS:
    void devicesChanged();
    void mountFinished(const QString &deviceId, bool ok, const QString &mountPointOrError);
    void unmountFinished(const QString &deviceId, bool ok, const QString &error);
    void syncItemsChanged(const QString &deviceId);
    void profileChanged(const QString &deviceId);

private:
    void rebuild();
    QString objectPathFor(const QString &idOrObjectPath) const;
    QString idFor(const QString &objectPath) const;

    std::unique_ptr<Database> m_db;
    std::unique_ptr<UdisksMonitor> m_monitor;
    QList<Device> m_testDevices;
    QList<Device> m_netMdDevices;
    bool m_testMode = false;
    QList<DeviceEntry> m_entries;
};

} // namespace mmm

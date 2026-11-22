// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QList>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantMap>

#include "device.h"

class QDBusMessage;

namespace mmm {

// org.freedesktop.UDisks2 object tree: object path → interface → properties
// (ObjectManager.GetManagedObjects, a{oa{sa{sv}}}).
using UdisksInterfaces = QMap<QString, QVariantMap>;
using UdisksObjects = QMap<QDBusObjectPath, UdisksInterfaces>;

// Removable USB storage, as UDisks2 (the system's disk service) reports it: the volumes
// that are on a USB drive (or a card in an SD reader), have a filesystem and aren't system
// disks. Follows plugging, unplugging and mounting (ObjectManager signals + PropertiesChanged),
// and mounts/unmounts on request (UDisks2 asks the user via polkit when needed).
// All D-Bus calls are asynchronous; lives on the thread it was created on.
// See docs/decisions/0004 for the rules.
class UdisksMonitor : public QObject
{
    Q_OBJECT

public:
    struct Options
    {
        QDBusConnection bus = QDBusConnection::systemBus();
        QString service = QStringLiteral("org.freedesktop.UDisks2");
        StorageSpaceFn space = storageSpace;
    };

    explicit UdisksMonitor(Options options, QObject *parent = nullptr);
    explicit UdisksMonitor(QObject *parent = nullptr) : UdisksMonitor(Options{}, parent) {}
    ~UdisksMonitor() override;

    // Subscribes and lists the current devices (devicesChanged() follows).
    void start();
    QList<Device> devices() const { return m_devices; }
    bool isAvailable() const { return m_available; }

    // `objectPath` = Device::objectPath. Results via mountFinished / unmountFinished.
    void mount(const QString &objectPath);
    void unmount(const QString &objectPath);
    // Re-reads free space of mounted devices (e.g. after a sync).
    void refreshSpace();

    // The removable-storage rules applied to a GetManagedObjects snapshot. Sorted by name.
    static QList<Device> parse(const UdisksObjects &objects, const StorageSpaceFn &space = storageSpace);
    // "ay" / "aay" values (NUL-terminated byte strings) as UDisks2 sends them.
    static QString decodeBytes(const QVariant &value);
    static QStringList decodeByteList(const QVariant &value);
    static void registerTypes();

Q_SIGNALS:
    void devicesChanged();
    void mountFinished(const QString &objectPath, bool ok, const QString &mountPointOrError);
    void unmountFinished(const QString &objectPath, bool ok, const QString &error);

private Q_SLOTS:
    void onInterfacesAdded(const QDBusObjectPath &path, const mmm::UdisksInterfaces &interfaces);
    void onInterfacesRemoved(const QDBusObjectPath &path, const QStringList &interfaces);
    void onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated,
                             const QDBusMessage &message);

private:
    void fetchAll();
    void rebuild();
    void call(const QString &objectPath, const QString &method, bool isMount);

    Options m_options;
    UdisksObjects m_objects;
    QList<Device> m_devices;
    QSet<QString> m_busy;
    QTimer m_refetch; // debounced full re-read (invalidated properties, service restart)
    bool m_available = false;
    bool m_started = false;
    quint64 m_fetchGeneration = 0; // drops GetManagedObjects replies that were superseded
};

} // namespace mmm

Q_DECLARE_METATYPE(mmm::UdisksInterfaces)
Q_DECLARE_METATYPE(mmm::UdisksObjects)

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVirtualObject>
#include <QFile>
#include <QHash>

#include "udisksmonitor.h"

namespace mmm::test {

// A fake org.freedesktop.UDisks2 for tests, on a private session bus (run under
// dbus-run-session). Serves ObjectManager.GetManagedObjects and Filesystem.Mount/Unmount,
// and emits InterfacesAdded/Removed and PropertiesChanged like udisksd.
class FakeUdisks : public QDBusVirtualObject
{
public:
    static constexpr const char *kService = "org.freedesktop.UDisks2";

    explicit FakeUdisks(QDBusConnection bus) : m_bus(std::move(bus)) { UdisksMonitor::registerTypes(); }
    ~FakeUdisks() override
    {
        m_bus.unregisterObject(root());
        m_bus.unregisterService(QString::fromLatin1(kService));
    }

    bool start()
    {
        return m_bus.registerVirtualObject(root(), this, QDBusConnection::SubPath)
            && m_bus.registerService(QString::fromLatin1(kService));
    }

    static QString root() { return QStringLiteral("/org/freedesktop/UDisks2"); }

    // A USB drive with one filesystem; returns the block object path.
    struct Volume
    {
        QString name;          // block_devices/<name>, drives/<name>_drive
        QString label;
        QString uuid;
        QString fsType = QStringLiteral("vfat");
        quint64 size = 8'000'000'000ULL;
        QString bus = QStringLiteral("usb");
        bool hintSystem = false;
        bool removable = true;
        QString mountPoint;    // "" = not mounted
        QString vendor = QStringLiteral("Generic");
        QString model = QStringLiteral("Flash Disk");
        QString serial;
    };
    // Volume{name, label, uuid} with the other fields at their defaults (GCC rejects
    // designated initializers that leave out fields).
    static Volume volume(const QString &name, const QString &label = {}, const QString &uuid = {})
    {
        Volume v;
        v.name = name;
        v.label = label;
        v.uuid = uuid;
        return v;
    }
    static UdisksObjects objectsFor(const Volume &v)
    {
        const QString drivePath = root() + QStringLiteral("/drives/") + v.name + QStringLiteral("_drive");
        UdisksObjects out;
        out[QDBusObjectPath(drivePath)][QStringLiteral("org.freedesktop.UDisks2.Drive")] = {
            {QStringLiteral("ConnectionBus"), v.bus},
            {QStringLiteral("Removable"), v.removable},
            {QStringLiteral("MediaRemovable"), false},
            {QStringLiteral("Optical"), false},
            {QStringLiteral("Vendor"), v.vendor},
            {QStringLiteral("Model"), v.model},
            {QStringLiteral("Serial"), v.serial},
            {QStringLiteral("Id"), v.name},
        };
        UdisksInterfaces &block = out[QDBusObjectPath(blockPath(v.name))];
        block[QStringLiteral("org.freedesktop.UDisks2.Block")] = {
            {QStringLiteral("Device"), QByteArray("/dev/") + v.name.toUtf8() + '\0'},
            {QStringLiteral("Drive"), QVariant::fromValue(QDBusObjectPath(drivePath))},
            {QStringLiteral("IdType"), v.fsType},
            {QStringLiteral("IdUUID"), v.uuid},
            {QStringLiteral("IdLabel"), v.label},
            {QStringLiteral("Size"), QVariant::fromValue<qulonglong>(v.size)},
            {QStringLiteral("HintSystem"), v.hintSystem},
            {QStringLiteral("HintIgnore"), false},
            {QStringLiteral("ReadOnly"), false},
        };
        block[QStringLiteral("org.freedesktop.UDisks2.Partition")] = {{QStringLiteral("Number"), 1u}};
        block[QStringLiteral("org.freedesktop.UDisks2.Filesystem")] = {
            {QStringLiteral("MountPoints"), mountPoints(v.mountPoint)}};
        return out;
    }
    static QString blockPath(const QString &name) { return root() + QStringLiteral("/block_devices/") + name; }
    static QVariant mountPoints(const QString &mountPoint)
    {
        QList<QByteArray> points;
        if (!mountPoint.isEmpty())
            points << QFile::encodeName(mountPoint) + '\0';
        return QVariant::fromValue(points);
    }

    // Plugs a volume in (InterfacesAdded for the drive, then the block device).
    void plug(const Volume &v, bool announce = true)
    {
        const UdisksObjects objs = objectsFor(v);
        for (auto it = objs.cbegin(); it != objs.cend(); ++it) {
            m_objects[it.key()] = it.value();
            if (announce) {
                QDBusMessage sig = QDBusMessage::createSignal(root(), QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                                                              QStringLiteral("InterfacesAdded"));
                sig << QVariant::fromValue(it.key()) << QVariant::fromValue(it.value());
                m_bus.send(sig);
            }
        }
    }
    void unplug(const QString &name)
    {
        for (const QString &path : {blockPath(name), root() + QStringLiteral("/drives/") + name + QStringLiteral("_drive")}) {
            const QDBusObjectPath p(path);
            const QStringList ifaces = m_objects.value(p).keys();
            m_objects.remove(p);
            QDBusMessage sig = QDBusMessage::createSignal(root(), QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                                                          QStringLiteral("InterfacesRemoved"));
            sig << QVariant::fromValue(p) << ifaces;
            m_bus.send(sig);
        }
    }
    // Mounted/unmounted from outside the app (e.g. the file manager).
    void setMountPoint(const QString &name, const QString &mountPoint)
    {
        const QVariant mp = mountPoints(mountPoint);
        m_objects[QDBusObjectPath(blockPath(name))][QStringLiteral("org.freedesktop.UDisks2.Filesystem")]
                 [QStringLiteral("MountPoints")] = mp;
        QDBusMessage sig = QDBusMessage::createSignal(blockPath(name), QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("PropertiesChanged"));
        sig << QStringLiteral("org.freedesktop.UDisks2.Filesystem") << QVariantMap{{QStringLiteral("MountPoints"), mp}}
            << QStringList();
        m_bus.send(sig);
    }

    QString failNextWith;      // D-Bus error name for the next Mount/Unmount (e.g. NotAuthorized)
    QStringList calls;         // "Mount /org/.../sdb1", ...

    QString introspect(const QString &) const override { return {}; }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        if (message.member() == QLatin1String("GetManagedObjects")) {
            connection.send(message.createReply(QVariant::fromValue(m_objects)));
            return true;
        }
        if (message.interface() == QLatin1String("org.freedesktop.UDisks2.Filesystem")
            && (message.member() == QLatin1String("Mount") || message.member() == QLatin1String("Unmount"))) {
            calls << message.member() + QLatin1Char(' ') + message.path();
            if (!failNextWith.isEmpty()) {
                connection.send(message.createErrorReply(failNextWith, QStringLiteral("not allowed")));
                failNextWith.clear();
                return true;
            }
            const QString name = message.path().section(QLatin1Char('/'), -1);
            const bool mount = message.member() == QLatin1String("Mount");
            const QString mp = mount ? QStringLiteral("/run/media/test/") + name : QString();
            connection.send(mount ? message.createReply(mp) : message.createReply());
            setMountPoint(name, mp);
            return true;
        }
        return false;
    }

private:
    QDBusConnection m_bus;
    UdisksObjects m_objects;
};

} // namespace mmm::test

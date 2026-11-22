// SPDX-License-Identifier: GPL-3.0-or-later
#include "udisksmonitor.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QFile>
#include <QLoggingCategory>
#include <QStorageInfo>

#include <algorithm>
#include <mutex>

Q_LOGGING_CATEGORY(lcDevices, "mmm.devices")

namespace mmm {

namespace {

const QString kRoot = QStringLiteral("/org/freedesktop/UDisks2");
const QString kObjectManager = QStringLiteral("org.freedesktop.DBus.ObjectManager");
const QString kProperties = QStringLiteral("org.freedesktop.DBus.Properties");
const QString kBlock = QStringLiteral("org.freedesktop.UDisks2.Block");
const QString kFilesystem = QStringLiteral("org.freedesktop.UDisks2.Filesystem");
const QString kPartition = QStringLiteral("org.freedesktop.UDisks2.Partition");
const QString kDrive = QStringLiteral("org.freedesktop.UDisks2.Drive");

// Filesystems a music player or a computer can read. Not optical/ISO images, swap, LUKS.
const QStringList kFilesystems = {QStringLiteral("vfat"),  QStringLiteral("exfat"), QStringLiteral("ntfs"),
                                  QStringLiteral("ntfs3"), QStringLiteral("ext2"),  QStringLiteral("ext3"),
                                  QStringLiteral("ext4"),  QStringLiteral("btrfs"), QStringLiteral("f2fs"),
                                  QStringLiteral("hfsplus")};

// QtDBus hands "aay" values over as a QDBusArgument, which can be read only once (copies
// share the read position): decode mount points when they arrive, so parse() can run any
// number of times over the stored objects.
void normalize(QVariantMap &props)
{
    const auto it = props.find(QStringLiteral("MountPoints"));
    if (it != props.end() && it->canConvert<QDBusArgument>()) {
        QList<QByteArray> list;
        it->value<QDBusArgument>() >> list;
        *it = QVariant::fromValue(list);
    }
}

void normalize(UdisksInterfaces &ifaces)
{
    for (auto it = ifaces.begin(); it != ifaces.end(); ++it)
        normalize(*it);
}

QString objectPathOf(const QVariant &v)
{
    if (v.canConvert<QDBusObjectPath>())
        return v.value<QDBusObjectPath>().path();
    return v.toString();
}

} // namespace

StorageSpace storageSpace(const QString &mountPoint)
{
    const QStorageInfo info(mountPoint);
    if (!info.isValid() || !info.isReady())
        return {};
    return {info.bytesTotal(), info.bytesAvailable()};
}

void UdisksMonitor::registerTypes()
{
    static std::once_flag once;
    std::call_once(once, [] {
        qDBusRegisterMetaType<UdisksInterfaces>();
        qDBusRegisterMetaType<UdisksObjects>();
        qDBusRegisterMetaType<QList<QByteArray>>();
    });
}

QString UdisksMonitor::decodeBytes(const QVariant &value)
{
    QByteArray bytes = value.toByteArray();
    while (bytes.endsWith('\0'))
        bytes.chop(1);
    return QFile::decodeName(bytes);
}

QStringList UdisksMonitor::decodeByteList(const QVariant &value)
{
    QList<QByteArray> list;
    if (value.canConvert<QDBusArgument>()) {
        const auto arg = value.value<QDBusArgument>();
        arg >> list;
    } else {
        list = value.value<QList<QByteArray>>();
    }
    QStringList out;
    for (QByteArray b : std::as_const(list)) {
        while (b.endsWith('\0'))
            b.chop(1);
        if (!b.isEmpty())
            out << QFile::decodeName(b);
    }
    return out;
}

QList<Device> UdisksMonitor::parse(const UdisksObjects &objects, const StorageSpaceFn &space)
{
    QList<Device> out;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const UdisksInterfaces &ifaces = it.value();
        if (!ifaces.contains(kBlock) || !ifaces.contains(kFilesystem))
            continue; // whole disks with partitions, swap, empty readers
        const QVariantMap &block = ifaces[kBlock];
        if (block.value(QStringLiteral("HintIgnore")).toBool() || block.value(QStringLiteral("HintSystem")).toBool())
            continue; // internal disks, EFI and recovery partitions
        const qint64 size = static_cast<qint64>(block.value(QStringLiteral("Size")).toULongLong());
        const QString fsType = block.value(QStringLiteral("IdType")).toString().toLower();
        if (size <= 0 || !kFilesystems.contains(fsType))
            continue;

        const QString drivePath = objectPathOf(block.value(QStringLiteral("Drive")));
        const auto driveIt = objects.constFind(QDBusObjectPath(drivePath.isEmpty() ? QStringLiteral("/") : drivePath));
        if (driveIt == objects.cend() || !driveIt->contains(kDrive))
            continue;
        const QVariantMap &drive = (*driveIt)[kDrive];
        if (drive.value(QStringLiteral("Optical")).toBool())
            continue;
        const bool mediaRemovable = drive.value(QStringLiteral("MediaRemovable")).toBool();
        if (mediaRemovable && drive.contains(QStringLiteral("MediaAvailable"))
            && !drive.value(QStringLiteral("MediaAvailable")).toBool())
            continue;
        // USB (sticks, DAPs, USB disks; Removable may be false for DAPs/enclosures) or a card
        // in a built-in SD reader.
        const QString bus = drive.value(QStringLiteral("ConnectionBus")).toString();
        if (bus != QLatin1String("usb") && bus != QLatin1String("sdio"))
            continue;

        Device d;
        d.objectPath = it.key().path();
        d.label = block.value(QStringLiteral("IdLabel")).toString().trimmed();
        d.vendor = drive.value(QStringLiteral("Vendor")).toString().trimmed();
        d.model = drive.value(QStringLiteral("Model")).toString().trimmed();
        d.deviceNode = decodeBytes(block.value(QStringLiteral("Device")));
        d.fsType = fsType;
        d.readOnly = block.value(QStringLiteral("ReadOnly")).toBool();
        const QString vendorModel = (d.vendor + QLatin1Char(' ') + d.model).trimmed();
        d.name = !d.label.isEmpty() ? d.label : !vendorModel.isEmpty() ? vendorModel : d.deviceNode;

        const QString uuid = block.value(QStringLiteral("IdUUID")).toString().trimmed();
        const QString part = ifaces.contains(kPartition)
            ? QStringLiteral("p%1").arg(ifaces[kPartition].value(QStringLiteral("Number")).toUInt())
            : QStringLiteral("whole");
        const QString serial = drive.value(QStringLiteral("Serial")).toString().trimmed();
        if (!uuid.isEmpty())
            d.id = QStringLiteral("uuid:") + uuid;
        else if (!serial.isEmpty())
            d.id = QStringLiteral("serial:%1:%2").arg(serial, part);
        else
            d.id = QStringLiteral("drive:%1:%2").arg(drive.value(QStringLiteral("Id")).toString(), part);

        const QStringList mounts = decodeByteList(ifaces[kFilesystem].value(QStringLiteral("MountPoints")));
        d.mountPoint = mounts.value(0);
        d.totalBytes = size;
        if (d.mounted() && space) {
            const StorageSpace s = space(d.mountPoint);
            if (s.totalBytes > 0) { // not readable yet right after mounting: keep the block size
                d.totalBytes = s.totalBytes;
                d.freeBytes = s.freeBytes;
            }
        }
        out << d;
    }
    std::sort(out.begin(), out.end(), [](const Device &a, const Device &b) {
        const int c = a.name.compare(b.name, Qt::CaseInsensitive);
        if (c != 0)
            return c < 0;
        return a.id != b.id ? a.id < b.id : a.objectPath < b.objectPath;
    });
    return out;
}

UdisksMonitor::UdisksMonitor(Options options, QObject *parent)
    : QObject(parent), m_options(std::move(options))
{
    registerTypes();
    m_refetch.setSingleShot(true);
    m_refetch.setInterval(50);
    connect(&m_refetch, &QTimer::timeout, this, &UdisksMonitor::fetchAll);
}

UdisksMonitor::~UdisksMonitor() = default;

void UdisksMonitor::start()
{
    if (m_started)
        return;
    m_started = true;
    QDBusConnection &bus = m_options.bus;
    if (!bus.isConnected()) {
        qCInfo(lcDevices) << "no D-Bus connection: device detection is off";
        return;
    }
    bus.connect(m_options.service, kRoot, kObjectManager, QStringLiteral("InterfacesAdded"), this,
                SLOT(onInterfacesAdded(QDBusObjectPath, mmm::UdisksInterfaces)));
    bus.connect(m_options.service, kRoot, kObjectManager, QStringLiteral("InterfacesRemoved"), this,
                SLOT(onInterfacesRemoved(QDBusObjectPath, QStringList)));
    // Any object's property changes (mount points come this way).
    bus.connect(m_options.service, QString(), kProperties, QStringLiteral("PropertiesChanged"), this,
                SLOT(onPropertiesChanged(QString, QVariantMap, QStringList, QDBusMessage)));
    auto *watcher = new QDBusServiceWatcher(m_options.service, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &newOwner) {
                if (newOwner.isEmpty()) { // udisksd stopped: nothing is known any more
                    ++m_fetchGeneration;
                    m_objects.clear();
                    m_available = false;
                    rebuild();
                } else {
                    fetchAll();
                }
            });
    fetchAll();
}

void UdisksMonitor::fetchAll()
{
    const quint64 generation = ++m_fetchGeneration;
    const QDBusMessage msg =
        QDBusMessage::createMethodCall(m_options.service, kRoot, kObjectManager, QStringLiteral("GetManagedObjects"));
    auto *watcher = new QDBusPendingCallWatcher(m_options.bus.asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (generation != m_fetchGeneration)
            return; // a newer read (or udisksd going away) superseded this one
        const QDBusPendingReply<UdisksObjects> reply = *w;
        if (reply.isError()) {
            qCInfo(lcDevices).noquote() << "UDisks2 not available:" << reply.error().message();
            m_available = false;
            m_objects.clear();
        } else {
            m_available = true;
            m_objects = reply.value();
            for (auto it = m_objects.begin(); it != m_objects.end(); ++it)
                normalize(*it);
        }
        rebuild();
    });
}

void UdisksMonitor::onInterfacesAdded(const QDBusObjectPath &path, const UdisksInterfaces &interfaces)
{
    UdisksInterfaces added = interfaces;
    normalize(added);
    UdisksInterfaces &obj = m_objects[path];
    for (auto it = added.cbegin(); it != added.cend(); ++it)
        obj[it.key()] = it.value();
    rebuild();
}

void UdisksMonitor::onInterfacesRemoved(const QDBusObjectPath &path, const QStringList &interfaces)
{
    const auto it = m_objects.find(path);
    if (it == m_objects.end())
        return;
    for (const QString &i : interfaces)
        it->remove(i);
    if (it->isEmpty())
        m_objects.erase(it);
    rebuild();
}

void UdisksMonitor::onPropertiesChanged(const QString &interface, const QVariantMap &changed,
                                        const QStringList &invalidated, const QDBusMessage &message)
{
    const QDBusObjectPath path(message.path());
    if (!message.path().startsWith(kRoot + QLatin1Char('/')))
        return;
    const auto it = m_objects.find(path);
    if (it == m_objects.end() || !it->contains(interface))
        return; // an object we haven't seen yet: InterfacesAdded brings it
    QVariantMap update = changed;
    normalize(update);
    QVariantMap &props = (*it)[interface];
    for (auto c = update.cbegin(); c != update.cend(); ++c)
        props[c.key()] = c.value();
    if (!invalidated.isEmpty())
        m_refetch.start(); // values not sent: read everything again
    rebuild();
}

void UdisksMonitor::rebuild()
{
    QList<Device> next = parse(m_objects, m_options.space);
    for (Device &d : next)
        d.busy = m_busy.contains(d.objectPath);
    if (next == m_devices)
        return;
    m_devices = std::move(next);
    Q_EMIT devicesChanged();
}

void UdisksMonitor::refreshSpace()
{
    rebuild();
}

void UdisksMonitor::mount(const QString &objectPath)
{
    call(objectPath, QStringLiteral("Mount"), true);
}

void UdisksMonitor::unmount(const QString &objectPath)
{
    call(objectPath, QStringLiteral("Unmount"), false);
}

void UdisksMonitor::call(const QString &objectPath, const QString &method, bool isMount)
{
    const bool known = std::any_of(m_devices.cbegin(), m_devices.cend(),
                                   [&](const Device &d) { return d.objectPath == objectPath; });
    if (!known || m_busy.contains(objectPath))
        return;
    m_busy.insert(objectPath);
    rebuild();

    QDBusMessage msg = QDBusMessage::createMethodCall(m_options.service, objectPath, kFilesystem, method);
    msg << QVariantMap{}; // options; UDisks2 asks for authorisation itself when needed
    msg.setInteractiveAuthorizationAllowed(true);
    auto *watcher = new QDBusPendingCallWatcher(m_options.bus.asyncCall(msg, 120000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, objectPath, method, isMount](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        m_busy.remove(objectPath);
        const QDBusMessage reply = w->reply();
        const bool ok = reply.type() == QDBusMessage::ReplyMessage;
        QString result = ok ? (isMount ? reply.arguments().value(0).toString() : QString()) : reply.errorMessage();
        const QDBusObjectPath path(objectPath);
        // Don't wait for PropertiesChanged to show the new state.
        if (ok && m_objects.contains(path) && m_objects[path].contains(kFilesystem)) {
            QList<QByteArray> points;
            if (isMount && !result.isEmpty())
                points << QFile::encodeName(result) + '\0';
            m_objects[path][kFilesystem][QStringLiteral("MountPoints")] = QVariant::fromValue(points);
        }
        if (!ok)
            qCInfo(lcDevices).noquote() << method << objectPath << "failed:" << result;
        rebuild();
        if (isMount)
            Q_EMIT mountFinished(objectPath, ok, result);
        else
            Q_EMIT unmountFinished(objectPath, ok, result);
    });
}

} // namespace mmm

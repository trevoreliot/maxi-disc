// SPDX-License-Identifier: GPL-3.0-or-later
// Run under `dbus-run-session` (ctest does): the fake UDisks2 lives on a private session
// bus, so the test never sees (or mounts) the machine's real devices.
#include <QDBusConnection>
#include <QSignalSpy>
#include <QTest>

#include "fakeudisks.h"
#include "udisksmonitor.h"

using mmm::Device;
using mmm::UdisksMonitor;
using mmm::UdisksObjects;
using mmm::test::FakeUdisks;

namespace {

mmm::StorageSpace fakeSpace(const QString &mountPoint)
{
    return mountPoint.isEmpty() ? mmm::StorageSpace{} : mmm::StorageSpace{4000, 1500};
}

UdisksObjects merge(std::initializer_list<UdisksObjects> parts)
{
    UdisksObjects out;
    for (const auto &p : parts) {
        for (auto it = p.cbegin(); it != p.cend(); ++it)
            out[it.key()] = it.value();
    }
    return out;
}

QStringList names(const QList<Device> &devices)
{
    QStringList out;
    for (const auto &d : devices)
        out << d.name;
    return out;
}

} // namespace

class TestUdisksMonitor : public QObject
{
    Q_OBJECT

private:
    QDBusConnection client() { return QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("monitor")); }

private Q_SLOTS:
    // --- The removable-storage rules (pure) ---

    void parseKeepsRemovableUsbOnly()
    {
        auto stick = FakeUdisks::volume(QStringLiteral("sdb1"), QStringLiteral("STICK"), QStringLiteral("AAAA-0001"));
        stick.mountPoint = QStringLiteral("/run/media/me/STICK");
        // The user's real drive: USB, Removable, exFAT, not mounted (UDisks2 values seen on M8).
        auto passport = FakeUdisks::volume(QStringLiteral("sdc1"), QStringLiteral("Passport5TB"), QStringLiteral("6478-1AB0"));
        passport.fsType = QStringLiteral("exfat");
        passport.size = 5000945205248ULL;
        passport.vendor = QString();
        passport.model = QStringLiteral("WDC WD50NDZW-11BCSS1");
        passport.serial = QStringLiteral("WD-WX82D4505701");
        // A USB DAP: not "Removable", still USB; no label or UUID.
        auto dap = FakeUdisks::volume(QStringLiteral("sdd"));
        dap.removable = false;
        dap.vendor = QStringLiteral("FiiO");
        dap.model = QStringLiteral("M3K");
        dap.serial = QStringLiteral("F11O");
        auto internal = FakeUdisks::volume(QStringLiteral("sda1"), QStringLiteral("backup"), QStringLiteral("x"));
        internal.fsType = QStringLiteral("ext4");
        internal.bus = QStringLiteral("ata");
        internal.hintSystem = true;
        auto nvme = FakeUdisks::volume(QStringLiteral("nvme0n1p3"), QStringLiteral("Home"), QStringLiteral("y"));
        nvme.fsType = QStringLiteral("ext4");
        nvme.bus = QString();
        auto iso = FakeUdisks::volume(QStringLiteral("sde1"), QStringLiteral("INSTALLER"), QStringLiteral("z"));
        iso.fsType = QStringLiteral("iso9660");
        auto empty = FakeUdisks::volume(QStringLiteral("sdf1"), QStringLiteral("EMPTY"), QStringLiteral("w"));
        empty.size = 0;
        auto sdcard = FakeUdisks::volume(QStringLiteral("mmcblk0p1"), QStringLiteral("SDCARD"), QStringLiteral("SD-01"));
        sdcard.bus = QStringLiteral("sdio");

        UdisksObjects all = merge({FakeUdisks::objectsFor(stick), FakeUdisks::objectsFor(passport), FakeUdisks::objectsFor(dap),
                                   FakeUdisks::objectsFor(internal), FakeUdisks::objectsFor(nvme), FakeUdisks::objectsFor(iso),
                                   FakeUdisks::objectsFor(empty), FakeUdisks::objectsFor(sdcard)});
        // A whole disk with a partition table has a Block but no Filesystem: not listed.
        all[QDBusObjectPath(FakeUdisks::blockPath(QStringLiteral("sdb")))][QStringLiteral("org.freedesktop.UDisks2.Block")] =
            {{QStringLiteral("Size"), QVariant::fromValue<qulonglong>(8)}};

        const QList<Device> devices = UdisksMonitor::parse(all, fakeSpace);
        QCOMPARE(names(devices), (QStringList{QStringLiteral("FiiO M3K"), QStringLiteral("Passport5TB"),
                                              QStringLiteral("SDCARD"), QStringLiteral("STICK")}));

        const Device &p = devices[1];
        QCOMPARE(p.id, QStringLiteral("uuid:6478-1AB0"));
        QCOMPARE(p.objectPath, FakeUdisks::blockPath(QStringLiteral("sdc1")));
        QCOMPARE(p.fsType, QStringLiteral("exfat"));
        QCOMPARE(p.deviceNode, QStringLiteral("/dev/sdc1"));
        QVERIFY(!p.mounted());
        QCOMPARE(p.totalBytes, qint64(5000945205248));
        QCOMPARE(p.freeBytes, -1);

        const Device &s = devices[3];
        QCOMPARE(s.mountPoint, QStringLiteral("/run/media/me/STICK"));
        QCOMPARE(s.totalBytes, 4000); // from the filesystem once mounted
        QCOMPARE(s.freeBytes, 1500);

        // No UUID: drive serial + partition, else drive id.
        QCOMPARE(devices[0].id, QStringLiteral("serial:F11O:p1"));
        QCOMPARE(UdisksMonitor::parse(FakeUdisks::objectsFor(FakeUdisks::volume(QStringLiteral("sdg1"), QStringLiteral("NONAME"))))
                     .front().id,
                 QStringLiteral("drive:sdg1:p1"));
    }

    void decodesByteStrings()
    {
        QCOMPARE(UdisksMonitor::decodeBytes(QByteArray("/dev/sdb1\0", 10)), QStringLiteral("/dev/sdb1"));
        const QList<QByteArray> points{QByteArray("/run/media/me/Stick ü\0", 23), QByteArray("\0", 1)};
        QCOMPARE(UdisksMonitor::decodeByteList(QVariant::fromValue(points)), QStringList{QStringLiteral("/run/media/me/Stick ü")});
        QVERIFY(UdisksMonitor::decodeByteList(QVariant()).isEmpty());
    }

    // --- Live, against the fake service ---

    void followsPlugMountAndUnplug()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        QVERIFY2(bus.isConnected(), "no session bus: run under dbus-run-session");
        FakeUdisks fake(bus);
        fake.plug(FakeUdisks::volume(QStringLiteral("sdb1"), QStringLiteral("STICK"), QStringLiteral("AAAA-0001")), false);
        auto preMounted = FakeUdisks::volume(QStringLiteral("sdz1"), QStringLiteral("ZMOUNTED"), QStringLiteral("ZZZZ-0009"));
        preMounted.mountPoint = QStringLiteral("/run/media/me/Z");
        fake.plug(preMounted, false);
        QVERIFY(fake.start());

        UdisksMonitor monitor(UdisksMonitor::Options{client(), QString::fromLatin1(FakeUdisks::kService), fakeSpace});
        QSignalSpy changed(&monitor, &UdisksMonitor::devicesChanged);
        monitor.start();
        QTRY_COMPARE(monitor.devices().size(), 2);
        QVERIFY(monitor.isAvailable());
        QCOMPARE(monitor.devices().front().name, QStringLiteral("STICK"));
        QVERIFY(!monitor.devices().front().mounted());
        // Already mounted at startup (GetManagedObjects): still mounted after re-reads.
        monitor.refreshSpace();
        QCOMPARE(monitor.devices().back().mountPoint, QStringLiteral("/run/media/me/Z"));
        fake.unplug(QStringLiteral("sdz1"));
        QTRY_COMPARE(monitor.devices().size(), 1);

        // Plugged in while running.
        fake.plug(FakeUdisks::volume(QStringLiteral("sdc1"), QStringLiteral("DAP"), QStringLiteral("BBBB-0002")));
        QTRY_COMPARE(monitor.devices().size(), 2);

        // Mounted elsewhere (file manager): PropertiesChanged.
        fake.setMountPoint(QStringLiteral("sdc1"), QStringLiteral("/run/media/me/DAP"));
        QTRY_COMPARE(monitor.devices().front().mountPoint, QStringLiteral("/run/media/me/DAP")); // "DAP" sorts first
        QCOMPARE(monitor.devices().front().freeBytes, 1500);

        // Mount from the app.
        QSignalSpy mounted(&monitor, &UdisksMonitor::mountFinished);
        const QString stick = FakeUdisks::blockPath(QStringLiteral("sdb1"));
        monitor.mount(stick);
        QVERIFY(monitor.devices().back().busy);
        monitor.mount(stick); // ignored while busy
        QTRY_COMPARE(mounted.size(), 1);
        QCOMPARE(fake.calls, QStringList{QStringLiteral("Mount ") + stick});
        QVERIFY(mounted.front().at(1).toBool());
        QCOMPARE(mounted.front().at(2).toString(), QStringLiteral("/run/media/test/sdb1"));
        QCOMPARE(monitor.devices().back().mountPoint, QStringLiteral("/run/media/test/sdb1"));
        QVERIFY(!monitor.devices().back().busy);
        // Mount points that came over D-Bus survive any number of re-reads (QtDBus delivers
        // them as a QDBusArgument, which can only be read once).
        monitor.refreshSpace();
        monitor.refreshSpace();
        QCOMPARE(monitor.devices().front().mountPoint, QStringLiteral("/run/media/me/DAP"));

        // A refused unmount (polkit / busy) is reported, state unchanged.
        QSignalSpy unmounted(&monitor, &UdisksMonitor::unmountFinished);
        fake.failNextWith = QStringLiteral("org.freedesktop.UDisks2.Error.DeviceBusy");
        monitor.unmount(stick);
        QTRY_COMPARE(unmounted.size(), 1);
        QVERIFY(!unmounted.front().at(1).toBool());
        QCOMPARE(unmounted.front().at(2).toString(), QStringLiteral("not allowed"));
        QVERIFY(monitor.devices().back().mounted());
        monitor.unmount(stick);
        QTRY_COMPARE(unmounted.size(), 2);
        QVERIFY(unmounted.back().at(1).toBool());
        QTRY_VERIFY(!monitor.devices().back().mounted());

        // Unknown devices are ignored.
        monitor.mount(QStringLiteral("/org/freedesktop/UDisks2/block_devices/nope"));
        QCOMPARE(fake.calls.size(), 3);

        fake.unplug(QStringLiteral("sdc1"));
        QTRY_COMPARE(monitor.devices().size(), 1);
        QCOMPARE(monitor.devices().front().name, QStringLiteral("STICK"));
    }

    void noServiceIsQuiet()
    {
        UdisksMonitor monitor(UdisksMonitor::Options{client(), QStringLiteral("org.example.NoSuchUDisks"), fakeSpace});
        monitor.start();
        QTest::qWait(200);
        QVERIFY(!monitor.isAvailable());
        QVERIFY(monitor.devices().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestUdisksMonitor)
#include "tst_udisksmonitor.moc"

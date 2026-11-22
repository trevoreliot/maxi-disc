// SPDX-License-Identifier: GPL-3.0-or-later
// Run under `dbus-run-session` (ctest does): devices come from a fake UDisks2.
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "devicemanager.h"
#include "fakeudisks.h"

using namespace mmm;
using mmm::test::FakeUdisks;

class TestDeviceManager : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    DeviceManager::Options options(bool monitor = true)
    {
        DeviceManager::Options o;
        o.databasePath = m_dir.filePath(QStringLiteral("library.db"));
        o.monitor = monitor;
        o.udisks.bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("manager"));
        o.udisks.service = QString::fromLatin1(FakeUdisks::kService);
        o.udisks.space = [](const QString &) { return StorageSpace{}; };
        return o;
    }

    static DeviceProfile profileFor(const QString &deviceId, const QString &name)
    {
        DeviceProfile p = *DeviceProfiles::builtIn(QStringLiteral("generic-dap"));
        p.deviceId = deviceId;
        p.name = name;
        return p;
    }

private Q_SLOTS:
    void joinsDevicesWithProfiles()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        QVERIFY2(bus.isConnected(), "no session bus: run under dbus-run-session");
        FakeUdisks fake(bus);
        fake.plug(FakeUdisks::volume(QStringLiteral("sdb1"), QStringLiteral("STICK"), QStringLiteral("AAAA-0001")), false);
        QVERIFY(fake.start());

        DeviceManager manager(options());
        QSignalSpy changed(&manager, &DeviceManager::devicesChanged);
        manager.start();
        QTRY_COMPARE(manager.devices().size(), 1);
        const DeviceEntry stick = manager.devices().front();
        QCOMPARE(stick.device.id, QStringLiteral("uuid:AAAA-0001"));
        QVERIFY(!stick.profile);

        // Set up: saved, joined, remembers the device's name; layout/name are checked.
        QString error;
        DeviceProfile p = profileFor(stick.device.id, QStringLiteral("Car"));
        p.musicDir = QStringLiteral("/Music//Car/");
        p.playableCodecs = {QStringLiteral("mp3"), QStringLiteral("Bogus"), QStringLiteral("FLAC")};
        QVERIFY2(manager.saveProfile(p, &error), qPrintable(error));
        QVERIFY(manager.devices().front().profile);
        const DeviceProfile saved = *manager.devices().front().profile;
        QCOMPARE(saved.name, QStringLiteral("Car"));
        QCOMPARE(saved.musicDir, QStringLiteral("Music/Car"));
        QCOMPARE(saved.playableCodecs, (QStringList{QStringLiteral("FLAC"), QStringLiteral("MP3")})); // canonical
        QCOMPARE(saved.lastSeenName, QStringLiteral("STICK"));
        QCOMPARE(manager.profile(stick.device.id), saved);

        p.pathLayout = QStringLiteral("{nope}");
        QVERIFY(!manager.saveProfile(p, &error));
        QVERIFY(error.contains(QStringLiteral("Unknown field")));
        p = profileFor(stick.device.id, QStringLiteral("  "));
        QVERIFY(!manager.saveProfile(p, &error));
        QVERIFY(error.contains(QStringLiteral("name")));

        // Mount by device id (what the UI and later D-Bus use).
        QSignalSpy mounted(&manager, &DeviceManager::mountFinished);
        manager.mount(stick.device.id);
        QTRY_COMPARE(mounted.size(), 1);
        QCOMPARE(mounted.front().at(0).toString(), stick.device.id);
        QVERIFY(mounted.front().at(1).toBool());
        QTRY_VERIFY(manager.devices().front().device.mounted());

        // Unplug + replug: the profile comes back with it (same filesystem UUID).
        fake.unplug(QStringLiteral("sdb1"));
        QTRY_VERIFY(manager.devices().isEmpty());
        fake.plug(FakeUdisks::volume(QStringLiteral("sdc1"), QStringLiteral("STICK2"), QStringLiteral("AAAA-0001")));
        QTRY_COMPARE(manager.devices().size(), 1);
        QCOMPARE(manager.devices().front().profile->name, QStringLiteral("Car"));
        QCOMPARE(manager.devices().front().profile->lastSeenName, QStringLiteral("STICK2"));

        QVERIFY(manager.deleteProfile(stick.device.id));
        QVERIFY(!manager.devices().front().profile);
    }

    void testDevicesWithoutDetection()
    {
        DeviceManager manager(options(false));
        manager.start();
        QVERIFY(manager.monitor() == nullptr);
        Device d;
        d.id = QStringLiteral("uuid:T1");
        d.objectPath = QStringLiteral("/test/1");
        d.name = QStringLiteral("Test");
        QSignalSpy changed(&manager, &DeviceManager::devicesChanged);
        manager.setDevicesForTesting({d});
        QCOMPARE(changed.size(), 1);
        QCOMPARE(manager.devices().size(), 1);
        QVERIFY(manager.device(QStringLiteral("/test/1")));
        QVERIFY(manager.device(QStringLiteral("uuid:T1")));
        QVERIFY(!manager.device(QStringLiteral("nope")));
        manager.mount(QStringLiteral("uuid:T1")); // no monitor: nothing happens
        QVERIFY(manager.saveProfile(profileFor(d.id, QStringLiteral("T"))));
        QCOMPARE(manager.devices().front().profile->name, QStringLiteral("T"));
    }
};

QTEST_GUILESS_MAIN(TestDeviceManager)
#include "tst_devicemanager.moc"

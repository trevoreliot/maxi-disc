// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include "database.h"
#include "deviceprofile.h"

using namespace mmm;

class TestDeviceProfile : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void builtIns()
    {
        const QList<DeviceProfile> all = DeviceProfiles::builtIns();
        QCOMPARE(all.size(), 2);
        const auto flash = DeviceProfiles::builtIn(QStringLiteral("usb-flash"));
        const auto dap = DeviceProfiles::builtIn(QStringLiteral("generic-dap"));
        QVERIFY(flash && dap);
        QVERIFY(!DeviceProfiles::builtIn(QStringLiteral("nope")));
        // Flash drive: everything as it is. DAP: MP3 only, small art (PLAN decisions).
        QCOMPARE(flash->playableCodecs, DeviceProfiles::allCodecs());
        QCOMPARE(flash->artworkPolicy, ArtworkPolicy::Keep);
        QCOMPARE(dap->playableCodecs, QStringList{QStringLiteral("MP3")});
        QCOMPARE(dap->transcodeTarget, TranscodeTarget::Mp3V0);
        QCOMPARE(dap->artworkPolicy, ArtworkPolicy::Shrink500);
        QCOMPARE(flash->musicDir, QStringLiteral("Music"));
    }

    void decide()
    {
        const DeviceProfile flash = *DeviceProfiles::builtIn(QStringLiteral("usb-flash"));
        DeviceProfile dap = *DeviceProfiles::builtIn(QStringLiteral("generic-dap"));
        for (const QString &codec : DeviceProfiles::allCodecs()) {
            const auto d = DeviceProfiles::decide(codec, flash);
            QVERIFY2(!d.transcode, qPrintable(codec));
            QCOMPARE(d.extension, DeviceProfiles::extensionForCodec(codec));
        }
        QCOMPARE(DeviceProfiles::decide(QStringLiteral("FLAC"), dap).transcode, true);
        QCOMPARE(DeviceProfiles::decide(QStringLiteral("FLAC"), dap).extension, QStringLiteral("mp3"));
        QCOMPARE(DeviceProfiles::decide(QStringLiteral("mp3"), dap).transcode, false); // case-insensitive
        dap.transcodeTarget = TranscodeTarget::Mp3Cbr320;
        QCOMPARE(DeviceProfiles::decide(QStringLiteral("Opus"), dap).extension, QStringLiteral("mp3"));
        // Unknown codecs can't be copied blindly.
        QCOMPARE(DeviceProfiles::decide(QStringLiteral("Unknown"), flash).transcode, true);
        QCOMPARE(DeviceProfiles::extensionForCodec(QStringLiteral("ALAC")), QStringLiteral("m4a"));
        QCOMPARE(DeviceProfiles::extensionForCodec(QStringLiteral("Vorbis")), QStringLiteral("ogg"));
        QCOMPARE(DeviceProfiles::extensionForCodec(QStringLiteral("nope")), QString());
    }

    void stringConversions()
    {
        for (const auto t : {TranscodeTarget::Mp3V0, TranscodeTarget::Mp3Cbr320})
            QCOMPARE(DeviceProfiles::transcodeTargetFrom(DeviceProfiles::toString(t)), t);
        for (const auto a : {ArtworkPolicy::Keep, ArtworkPolicy::Shrink500, ArtworkPolicy::Strip})
            QCOMPARE(DeviceProfiles::artworkPolicyFrom(DeviceProfiles::toString(a)), a);
        QVERIFY(!DeviceProfiles::transcodeTargetFrom(QStringLiteral("aac")));
        QVERIFY(!DeviceProfiles::artworkPolicyFrom(QString()));
        QVERIFY(!DeviceProfiles::describe(TranscodeTarget::Mp3Cbr320).isEmpty());
    }

    // Stored in the library database (schema v4), keyed by device id.
    void storage()
    {
        Database db;
        QVERIFY(db.open(QStringLiteral(":memory:")));
        QVERIFY(db.deviceProfiles().isEmpty());
        QVERIFY(!db.deviceProfile(QStringLiteral("uuid:X")));

        DeviceProfile p = *DeviceProfiles::builtIn(QStringLiteral("generic-dap"));
        p.deviceId = QStringLiteral("uuid:6478-1AB0");
        p.name = QStringLiteral("Car stick");
        p.musicDir = QStringLiteral("Music/Car");
        p.transcodeTarget = TranscodeTarget::Mp3Cbr320;
        p.lastSeenName = QStringLiteral("Passport5TB");
        QString error;
        QVERIFY2(db.saveDeviceProfile(p, &error), qPrintable(error));
        QCOMPARE(db.deviceProfile(p.deviceId), p);

        // Replacing keeps one row.
        p.name = QStringLiteral("Car");
        p.playableCodecs = {QStringLiteral("MP3"), QStringLiteral("FLAC")};
        QVERIFY(db.saveDeviceProfile(p));
        QCOMPARE(db.deviceProfiles().size(), 1);
        QCOMPARE(db.deviceProfile(p.deviceId)->playableCodecs, (QStringList{QStringLiteral("MP3"), QStringLiteral("FLAC")}));

        QVERIFY(db.setDeviceLastSeenName(p.deviceId, QStringLiteral("NEWLABEL")));
        QCOMPARE(db.deviceProfile(p.deviceId)->lastSeenName, QStringLiteral("NEWLABEL"));

        DeviceProfile other = *DeviceProfiles::builtIn(QStringLiteral("usb-flash"));
        other.deviceId = QStringLiteral("serial:ABC:p1");
        other.name = QStringLiteral("Aardvark");
        QVERIFY(db.saveDeviceProfile(other));
        QCOMPARE(db.deviceProfiles().front().name, QStringLiteral("Aardvark")); // by name

        DeviceProfile bad;
        QVERIFY(!db.saveDeviceProfile(bad, &error));
        QVERIFY(!error.isEmpty());

        QVERIFY(db.deleteDeviceProfile(p.deviceId));
        QVERIFY(!db.deleteDeviceProfile(p.deviceId));
        QCOMPARE(db.deviceProfiles().size(), 1);
    }

    void migratesV3ToV4()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("v3.db"));
        {
            Database db;
            QVERIFY(db.open(path));
            QSqlQuery q(db.connection());
            QVERIFY(q.exec(QStringLiteral("DROP TABLE device_profiles")));
            QVERIFY(q.exec(QStringLiteral("DROP TABLE device_sync_items")));
            QVERIFY(q.exec(QStringLiteral("DROP TABLE device_manifest")));
            QVERIFY(q.exec(QStringLiteral("PRAGMA user_version = 3")));
        }
        Database db;
        QString error;
        QVERIFY2(db.open(path, &error), qPrintable(error));
        QCOMPARE(db.schemaVersion(), Database::latestSchemaVersion());
        DeviceProfile p = *DeviceProfiles::builtIn(QStringLiteral("usb-flash"));
        p.deviceId = QStringLiteral("uuid:1");
        p.name = QStringLiteral("x");
        QVERIFY(db.saveDeviceProfile(p));
    }
};

QTEST_GUILESS_MAIN(TestDeviceProfile)
#include "tst_deviceprofile.moc"

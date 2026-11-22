// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include "appinfo.h"
#include "mmm/version.h"

class TestAppInfo : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void identityIsSet()
    {
        QCOMPARE(mmm::AppInfo::name(), QStringLiteral("music-md-manager"));
        QVERIFY(!mmm::AppInfo::displayName().isEmpty());
    }

    void formatDuration_data()
    {
        QTest::addColumn<qint64>("ms");
        QTest::addColumn<QString>("text");
        QTest::newRow("zero") << qint64(0) << "0:00";
        QTest::newRow("sub-second") << qint64(999) << "0:00";
        QTest::newRow("song") << qint64(187000) << "3:07";
        QTest::newRow("ten min") << qint64(600000) << "10:00";
        QTest::newRow("hour") << qint64(3723000) << "1:02:03";
        QTest::newRow("long") << qint64(36000000) << "10:00:00";
        QTest::newRow("unknown") << qint64(-1) << "--:--";
    }
    void formatDuration()
    {
        QFETCH(qint64, ms);
        QFETCH(QString, text);
        QCOMPARE(mmm::formatDuration(ms), text);
    }

    // MusicBrainz's format: "app/version ( contact )", the contact being the project page.
    void userAgentIdentifiesTheApp()
    {
        const QString ua = mmm::AppInfo::userAgent();
        QVERIFY2(ua.startsWith(mmm::AppInfo::name() + QLatin1Char('/') + mmm::AppInfo::version()), qPrintable(ua));
        const QString contact = QStringLiteral(MMM_CONTACT_URL); // the CMake setting (forks set their own)
        if (contact.isEmpty())
            QCOMPARE(ua, mmm::AppInfo::name() + QLatin1Char('/') + mmm::AppInfo::version());
        else
            QVERIFY2(ua.endsWith(QStringLiteral(" ( ") + contact + QStringLiteral(" )")), qPrintable(ua));
    }

    void versionIsSemver()
    {
        const auto parts = mmm::AppInfo::version().split(QLatin1Char('.'));
        QCOMPARE(parts.size(), 3);
        for (const auto &p : parts) {
            bool ok = false;
            p.toInt(&ok);
            QVERIFY2(ok, qPrintable(p));
        }
    }
};

QTEST_GUILESS_MAIN(TestAppInfo)
#include "tst_appinfo.moc"

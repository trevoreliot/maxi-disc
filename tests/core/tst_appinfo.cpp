// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include "appinfo.h"

class TestAppInfo : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void identityIsSet()
    {
        QCOMPARE(mmm::AppInfo::name(), QStringLiteral("music-md-manager"));
        QVERIFY(!mmm::AppInfo::displayName().isEmpty());
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

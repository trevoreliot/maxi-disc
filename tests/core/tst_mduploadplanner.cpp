// SPDX-License-Identifier: GPL-3.0-or-later
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "mduploadplanner.h"

using namespace mmm;

namespace {

MdDisc disc(qint64 availableMs, int tracks = 0, const QString &title = {})
{
    MdDisc d;
    d.title = title;
    d.capacity = MdCapacity{4'800'000 - availableMs, 4'800'000, availableMs, true};
    for (int i = 0; i < tracks; ++i)
        d.tracks << MdTrack{i, QStringLiteral("t"), 1000, MdEncoding::Sp, false, false};
    return d;
}

} // namespace

class TestMdUploadPlanner : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_file;

    TrackRow track(qint64 durationMs, const QString &album = QStringLiteral("Kid A"), const QString &title = QStringLiteral("Idioteque"))
    {
        TrackRow t;
        t.path = m_file;
        t.title = title;
        t.album = album;
        t.durationMs = durationMs;
        return t;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_file = m_dir.filePath(QStringLiteral("a.flac"));
        QFile f(m_file);
        QVERIFY(f.open(QIODevice::WriteOnly));
    }

    void fits()
    {
        const MdUploadPlan p = MdUploadPlanner::plan(disc(600'000), true, {track(200'000), track(180'000)});
        QVERIFY2(p.canRecord, qPrintable(p.error));
        QCOMPARE(p.durationMs, qint64(380'000));
        QCOMPARE(p.neededMs, qint64(380'000));
        QCOMPARE(p.availableMs, qint64(600'000));
        QVERIFY(!p.tight);
        QVERIFY(p.warning.isEmpty());
        QCOMPARE(p.suggestedDiscTitle, QStringLiteral("Kid A")); // blank, untitled, one album
    }

    void refusals()
    {
        QVERIFY(MdUploadPlanner::plan(disc(600'000), true, {}).error.contains(QStringLiteral("Nothing")));
        QVERIFY(MdUploadPlanner::plan(disc(600'000), false, {track(1000)}).error.contains(QStringLiteral("No disc")));
        const MdUploadPlan tooLong = MdUploadPlanner::plan(disc(300'000), true, {track(200'000), track(180'000)});
        QVERIFY(!tooLong.canRecord);
        QVERIFY(tooLong.error.contains(QStringLiteral("6:20 needed, 5:00 free")));
        QVERIFY(MdUploadPlanner::plan(disc(4'000'000, 254), true, {track(1000), track(1000)}).error.contains(QStringLiteral("255")));
        QVERIFY(MdUploadPlanner::plan(disc(4'000'000, 250), true, QList<TrackRow>(5, track(1000))).canRecord);
        TrackRow gone = track(1000);
        gone.path = m_dir.filePath(QStringLiteral("gone.flac"));
        QVERIFY(MdUploadPlanner::plan(disc(600'000), true, {gone}).error.contains(QStringLiteral("not found")));
    }

    void warnings()
    {
        const MdUploadPlan tight = MdUploadPlanner::plan(disc(381'000), true, {track(200'000), track(180'000)});
        QVERIFY(tight.canRecord);
        QVERIFY(tight.tight);
        QVERIFY(!tight.warning.isEmpty());
        const MdUploadPlan unknown = MdUploadPlanner::plan(disc(600'000), true, {track(0)});
        QVERIFY(unknown.canRecord);
        QVERIFY(unknown.warning.contains(QStringLiteral("length")));
    }

    void discTitleSuggestion()
    {
        // Only for a blank untitled disc filled from one album.
        QVERIFY(MdUploadPlanner::plan(disc(600'000, 1), true, {track(1000)}).suggestedDiscTitle.isEmpty());
        QVERIFY(MdUploadPlanner::plan(disc(600'000, 0, QStringLiteral("Mix")), true, {track(1000)}).suggestedDiscTitle.isEmpty());
        QVERIFY(MdUploadPlanner::plan(disc(600'000), true, {track(1000), track(1000, QStringLiteral("Amnesiac"))})
                    .suggestedDiscTitle.isEmpty());
        QCOMPARE(MdUploadPlanner::plan(disc(600'000), true, {track(1000, QStringLiteral("Ágætis byrjun"))}).suggestedDiscTitle,
                 QStringLiteral("Agaetis byrjun"));
    }

    // M12: a queue too long for SP fits in LP2 / LP4; the message is in the mode's time.
    void longPlayModes()
    {
        const QList<TrackRow> hundredMinutes(10, track(600'000));
        const MdUploadPlan sp = MdUploadPlanner::plan(disc(4'800'000), true, hundredMinutes);
        QVERIFY(!sp.canRecord);
        QVERIFY2(sp.error.contains(QStringLiteral("Doesn't fit in SP: 100:00 needed, 80:00 free")), qPrintable(sp.error));
        QVERIFY(MdUploadPlanner::plan(disc(4'800'000), true, hundredMinutes, MdRecordMode::Lp2).canRecord);
        const QList<TrackRow> fourHours(24, track(600'000));
        const MdUploadPlan lp2 = MdUploadPlanner::plan(disc(4'800'000), true, fourHours, MdRecordMode::Lp2);
        QVERIFY(!lp2.canRecord);
        QVERIFY2(lp2.error.contains(QStringLiteral("in LP2: 240:00 needed, 160:00 free")), qPrintable(lp2.error));
        QVERIFY(MdUploadPlanner::plan(disc(4'800'000), true, fourHours, MdRecordMode::Lp4).canRecord);
        // The track limit doesn't change with the mode.
        QVERIFY(!MdUploadPlanner::plan(disc(4'800'000, 250), true, QList<TrackRow>(6, track(1000)), MdRecordMode::Lp4).canRecord);
    }

    void wireSpecs()
    {
        QCOMPARE(mdWireSpec(MdRecordMode::Sp).wireFormat, 0x00);
        QCOMPARE(mdWireSpec(MdRecordMode::Sp).discFormat, 6);
        QCOMPARE(mdWireSpec(MdRecordMode::Lp2).wireFormat, 0x94);
        QCOMPARE(mdWireSpec(MdRecordMode::Lp2).discFormat, 2);
        QCOMPARE(mdWireSpec(MdRecordMode::Lp2).frameBytes, 384);
        QCOMPARE(mdWireSpec(MdRecordMode::Lp4).wireFormat, 0xa8);
        QCOMPARE(mdWireSpec(MdRecordMode::Lp4).discFormat, 0);
        QCOMPARE(mdWireSpec(MdRecordMode::Lp4).frameBytes, 192);
        QCOMPARE(mdModeFactor(MdRecordMode::Lp4), qint64(4));
        QCOMPARE(mdModeName(MdRecordMode::Lp2), QStringLiteral("LP2"));
    }

    void modesAndTitles()
    {
        QCOMPARE(MdUploadPlanner::neededMs(100'000, MdRecordMode::Sp), qint64(100'000));
        QCOMPARE(MdUploadPlanner::neededMs(100'001, MdRecordMode::Lp2), qint64(50'001));
        QCOMPARE(MdUploadPlanner::neededMs(100'001, MdRecordMode::Lp4), qint64(25'001));
        QCOMPARE(MdUploadPlanner::titleFor(track(1, {}, QStringLiteral("Jóga")), 1), QStringLiteral("Joga"));
        TrackRow untitled = track(1, {}, {});
        untitled.path = QStringLiteral("/m/03 Song Name.flac");
        QCOMPARE(MdUploadPlanner::titleFor(untitled, 3), QStringLiteral("03 Song Name"));
        untitled.path = QStringLiteral("/m/歌.flac");
        QCOMPARE(MdUploadPlanner::titleFor(untitled, 3), QStringLiteral("Track 3"));
    }
};

QTEST_GUILESS_MAIN(TestMdUploadPlanner)
#include "tst_mduploadplanner.moc"

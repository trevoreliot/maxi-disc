// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include "scrobbletracker.h"

using namespace mmm;

// When a play counts, on a fake clock (M14).
class TestScrobbleTracker : public QObject
{
    Q_OBJECT

    qint64 m_mono = 0;            // ms
    qint64 m_utc = 1'700'000'000; // s
    ScrobbleTracker tracker()
    {
        return ScrobbleTracker([this] { return m_mono; }, [this] { return m_utc; });
    }
    // Both clocks move on together.
    void wait(qint64 ms)
    {
        m_mono += ms;
        m_utc += ms / 1000;
    }
    static ScrobbleTrack song(qint64 durationMs, const QString &title = QStringLiteral("Roygbiv"))
    {
        return ScrobbleTrack{QStringLiteral("Boards of Canada"), title, QStringLiteral("Music Has the Right to Children"),
                             QString(), 7, durationMs};
    }

private Q_SLOTS:
    void init()
    {
        m_mono = 0;
        m_utc = 1'700'000'000;
    }

    void thresholds_data()
    {
        QTest::addColumn<qint64>("durationMs");
        QTest::addColumn<qint64>("thresholdMs"); // -1: never
        QTest::newRow("29.999 s") << qint64(29'999) << qint64(-1);
        QTest::newRow("30 s exactly") << qint64(30'000) << qint64(-1);
        QTest::newRow("30.001 s") << qint64(30'001) << qint64(15'000);
        QTest::newRow("3 min: half") << qint64(180'000) << qint64(90'000);
        QTest::newRow("8 min: half") << qint64(480'000) << qint64(240'000);
        QTest::newRow("10 min: 4 min") << qint64(600'000) << qint64(240'000);
        QTest::newRow("unknown length") << qint64(0) << qint64(-1);
    }
    void thresholds()
    {
        QFETCH(qint64, durationMs);
        QFETCH(qint64, thresholdMs);
        ScrobbleTracker t = tracker();
        QVERIFY(!t.start(song(durationMs), true));
        if (thresholdMs < 0) {
            QVERIFY(!t.msUntilDue());
            wait(700'000);
            QVERIFY(!t.check());
            QVERIFY(!t.stop());
            return;
        }
        QCOMPARE(t.msUntilDue(), std::optional<qint64>(thresholdMs));
        wait(thresholdMs - 1);
        QVERIFY(!t.check());
        QCOMPARE(t.msUntilDue(), std::optional<qint64>(1));
        wait(1);
        const std::optional<PendingScrobble> s = t.check();
        QVERIFY(s);
        QCOMPARE(s->track, song(durationMs));
        QCOMPARE(s->startedAt, 1'700'000'000);
        QVERIFY(!t.check()); // once
        QVERIFY(!t.stop());
        QVERIFY(!t.msUntilDue());
    }

    void needsAnArtistAndATitle()
    {
        ScrobbleTracker t = tracker();
        ScrobbleTrack noArtist = song(200'000);
        noArtist.artist = QStringLiteral("  ");
        t.start(noArtist, true);
        wait(200'000);
        QVERIFY(!t.stop());
        t.start(song(200'000, QString()), true);
        wait(200'000);
        QVERIFY(!t.stop());
    }

    void pausesDontCount()
    {
        ScrobbleTracker t = tracker();
        t.start(song(200'000), true); // due after 100 s of listening
        wait(60'000);
        QVERIFY(!t.setPlaying(false));
        QVERIFY(!t.msUntilDue()); // nothing to wait for while paused
        wait(3'600'000);
        QVERIFY(!t.check());
        QCOMPARE(t.listenedMs(), 60'000);
        t.setPlaying(true);
        QCOMPARE(t.msUntilDue(), std::optional<qint64>(40'000));
        wait(40'000);
        const auto s = t.check();
        QVERIFY(s);
        QCOMPARE(s->startedAt, 1'700'000'000); // when it started, not when it became due
    }

    void reachedWhilePausingCounts()
    {
        ScrobbleTracker t = tracker();
        t.start(song(200'000), true);
        wait(100'000);
        QVERIFY(t.setPlaying(false)); // the timer hadn't fired yet: the pause completes it
    }

    void seekingNeitherAddsNorTakesAway()
    {
        // The tracker never sees positions: a seek past the middle doesn't make it due, and
        // seeking back doesn't undo the time listened.
        ScrobbleTracker t = tracker();
        t.start(song(200'000), true);
        wait(10'000); // (the user seeks to 3:00 here)
        QVERIFY(!t.check());
        QCOMPARE(t.msUntilDue(), std::optional<qint64>(90'000));
        wait(90'000);
        QVERIFY(t.check());
    }

    void startsWhenItFirstPlays()
    {
        ScrobbleTracker t = tracker();
        t.start(song(200'000), false); // loaded paused
        QVERIFY(!t.msUntilDue());
        wait(600'000);
        t.setPlaying(true);
        const qint64 playedAt = m_utc;
        wait(100'000);
        const auto s = t.check();
        QVERIFY(s);
        QCOMPARE(s->startedAt, playedAt);
    }

    void aReplayCountsAgain()
    {
        ScrobbleTracker t = tracker();
        t.start(song(100'000), true);
        wait(100'000);
        QVERIFY(t.check());
        // Repeat one: the same track, a new play.
        QVERIFY(!t.start(song(100'000), true)); // the first one was already reported
        const qint64 secondAt = m_utc;
        wait(50'000);
        const auto s = t.check();
        QVERIFY(s);
        QCOMPARE(s->startedAt, secondAt);
    }

    void theNextTrackEndsThePlayBefore()
    {
        ScrobbleTracker t = tracker();
        t.start(song(100'000, QStringLiteral("One")), true);
        wait(50'000); // due exactly as it ends: the timer didn't fire yet
        const auto s = t.start(song(100'000, QStringLiteral("Two")), true);
        QVERIFY(s);
        QCOMPARE(s->track.title, QStringLiteral("One"));
        QCOMPARE(t.track().title, QStringLiteral("Two"));
        QCOMPARE(t.listenedMs(), 0);
        wait(10'000);
        QVERIFY(!t.stop()); // too short
        QVERIFY(!t.hasPlay());
    }

    void aTagEditIsNotANewPlay()
    {
        ScrobbleTracker t = tracker();
        t.start(song(200'000, QString()), true); // untitled: not eligible yet
        QVERIFY(!t.msUntilDue());
        wait(30'000);
        t.retag(song(0, QStringLiteral("Roygbiv"))); // titled now (an edit doesn't know the length)
        QCOMPARE(t.track().durationMs, 200'000);
        QCOMPARE(t.msUntilDue(), std::optional<qint64>(70'000)); // the 30 s listened count
        wait(70'000);
        const auto s = t.check();
        QVERIFY(s);
        QCOMPARE(s->track.title, QStringLiteral("Roygbiv"));
        QCOMPARE(s->startedAt, 1'700'000'000);
        t.retag(song(200'000, QStringLiteral("Renamed again")));
        QVERIFY(!t.check()); // already counted
    }

    void theLengthCanArriveLater()
    {
        ScrobbleTracker t = tracker();
        t.start(song(0), true);
        QVERIFY(!t.msUntilDue());
        wait(20'000);
        t.setDuration(120'000);
        QCOMPARE(t.msUntilDue(), std::optional<qint64>(40'000));
        t.setDuration(10'000); // known now: a later length (the next track's, perhaps) doesn't count
        QCOMPARE(t.track().durationMs, 120'000);
        wait(40'000);
        QVERIFY(t.check());
    }

    void nothingBeforeAPlay()
    {
        ScrobbleTracker t = tracker();
        QVERIFY(!t.hasPlay());
        QVERIFY(!t.setPlaying(true));
        QVERIFY(!t.check());
        QVERIFY(!t.stop());
        QVERIFY(!t.msUntilDue());
        t.retag(song(100'000));
        QVERIFY(!t.hasPlay());
    }
};

QTEST_GUILESS_MAIN(TestScrobbleTracker)
#include "tst_scrobbletracker.moc"

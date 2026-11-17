// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "playbackcontroller.h"
#include "testmedia.h"

using mmm::PlaybackController;
using mmm::Player;
using mmm::TrackRow;
using State = mmm::Player::PlaybackState;
using Repeat = mmm::PlayQueue::RepeatMode;

namespace {

constexpr int kTimeoutMs = 15000;

mmm::PlayerOptions silent()
{
    return {.audioSink = QByteArrayLiteral("fakesink sync=true")};
}

} // namespace

class TestPlaybackController : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    TrackRow m_a, m_b, m_c, m_long, m_missing;

    TrackRow make(qint64 id, const QString &file, const QString &encoder, int ms)
    {
        TrackRow t;
        t.id = id;
        t.path = m_dir.filePath(file);
        t.title = file;
        t.durationMs = ms;
        if (!encoder.isEmpty()) {
            const QString err = mmm::test::generateTone(t.path, encoder, ms);
            if (!err.isEmpty())
                qWarning("%s", qPrintable(err));
        }
        return t;
    }

    static bool waitStopped(PlaybackController &pc)
    {
        return QTest::qWaitFor([&] { return pc.player()->playbackState() == State::Stopped; }, kTimeoutMs);
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_a = make(1, QStringLiteral("a.flac"), QStringLiteral("flacenc"), 1000);
        m_b = make(2, QStringLiteral("b.mp3"), QStringLiteral("lamemp3enc ! xingmux"), 1000);
        m_c = make(3, QStringLiteral("c.wav"), QStringLiteral("wavenc"), 1000);
        m_long = make(4, QStringLiteral("long.flac"), QStringLiteral("flacenc"), 5000);
        m_missing = make(5, QStringLiteral("missing.flac"), QString(), 1000);
        QVERIFY(QFileInfo::exists(m_a.path) && QFileInfo::exists(m_b.path) && QFileInfo::exists(m_c.path));
    }

    void playsQueueGaplessly()
    {
        PlaybackController pc(silent());
        QSignalSpy changed(&pc, &PlaybackController::currentTrackChanged);
        QSignalSpy states(pc.player(), &Player::playbackStateChanged);
        pc.playTracks({m_a, m_b, m_c}, 0);
        QTRY_COMPARE_WITH_TIMEOUT(pc.player()->playbackState(), State::Playing, kTimeoutMs);
        states.clear();
        QCOMPARE(pc.currentTrack()->id, 1);

        QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 3, kTimeoutMs); // start + 2 gapless advances
        QCOMPARE(pc.queue()->currentIndex(), 2);
        QVERIFY(waitStopped(pc));
        QCOMPARE(states.count(), 1); // only the final Stopped: no restarts between tracks
        QCOMPARE(pc.queue()->currentIndex(), 2);
    }

    void playMidQueueAndNextPrevious()
    {
        PlaybackController pc(silent());
        pc.playTracks({m_a, m_long, m_c}, 1);
        QCOMPARE(pc.currentTrack()->id, 4);
        QTRY_COMPARE_WITH_TIMEOUT(pc.player()->playbackState(), State::Playing, kTimeoutMs);
        QVERIFY(pc.canGoNext());
        QVERIFY(pc.canGoPrevious());

        // Past 3 s, "previous" restarts the track instead of going back.
        QTRY_VERIFY_WITH_TIMEOUT(pc.player()->position() > 3200, kTimeoutMs);
        pc.previous();
        QCOMPARE(pc.queue()->currentIndex(), 1);
        QVERIFY(pc.player()->position() < 1000);

        pc.previous(); // near the start: goes back
        QCOMPARE(pc.currentTrack()->id, 1);
        pc.next();
        pc.next();
        QCOMPARE(pc.currentTrack()->id, 3);
        QVERIFY(!pc.canGoNext());
        pc.next(); // at the end with repeat off: no-op
        QCOMPARE(pc.currentTrack()->id, 3);
        pc.stop();
    }

    void repeatAllWraps()
    {
        PlaybackController pc(silent());
        pc.queue()->setRepeatMode(Repeat::All);
        QSignalSpy changed(&pc, &PlaybackController::currentTrackChanged);
        pc.playTracks({m_a, m_b}, 1);
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 2, kTimeoutMs);
        QCOMPARE(pc.queue()->currentIndex(), 0); // wrapped from the last track to the first
        QCOMPARE(pc.player()->playbackState(), State::Playing);
        QVERIFY(pc.canGoNext());
        pc.stop();
    }

    void repeatOneLoops()
    {
        PlaybackController pc(silent());
        pc.queue()->setRepeatMode(Repeat::One);
        QSignalSpy changed(&pc, &PlaybackController::currentTrackChanged);
        pc.playTracks({m_a, m_b}, 0);
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 3, kTimeoutMs); // start + 2 loops of the same track
        QCOMPARE(pc.queue()->currentIndex(), 0);
        QVERIFY(pc.player()->nextSource().isEmpty()); // loops restart; no self-gapless
        QTRY_COMPARE_WITH_TIMEOUT(pc.player()->playbackState(), State::Playing, kTimeoutMs);
        pc.next(); // explicit next still skips
        QCOMPARE(pc.queue()->currentIndex(), 1);
        pc.stop();
    }

    void removingCurrentSkips()
    {
        PlaybackController pc(silent());
        pc.playTracks({m_long, m_b, m_c}, 0);
        QTRY_COMPARE_WITH_TIMEOUT(pc.player()->playbackState(), State::Playing, kTimeoutMs);
        pc.queue()->removeAt(0);
        QCOMPARE(pc.currentTrack()->id, 2);
        QCOMPARE(pc.player()->source(), QUrl::fromLocalFile(m_b.path));
        QTRY_COMPARE_WITH_TIMEOUT(pc.player()->playbackState(), State::Playing, kTimeoutMs);
        pc.stop();

        // While stopped, removing the current track doesn't start anything.
        pc.queue()->removeAt(0);
        QCOMPARE(pc.player()->playbackState(), State::Stopped);
    }

    void queueChangedAfterHandOffIsHonoured()
    {
        // Short local files are handed to GStreamer almost immediately, so this inserts a
        // track *after* the next one (m_b) was committed. The queue must still win.
        PlaybackController pc(silent());
        QSignalSpy changed(&pc, &PlaybackController::currentTrackChanged);
        pc.playTracks({m_a, m_b}, 0);
        QTRY_COMPARE_WITH_TIMEOUT(pc.player()->playbackState(), State::Playing, kTimeoutMs);
        QTest::qWait(300);
        pc.queue()->insertNext({m_c});
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 2, kTimeoutMs);
        QCOMPARE(pc.currentTrack()->id, 3); // the inserted track, not the committed one
        QCOMPARE(pc.player()->source(), QUrl::fromLocalFile(m_c.path));
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 3, kTimeoutMs);
        QCOMPARE(pc.currentTrack()->id, 2); // ...followed by the rest of the queue
        QVERIFY(waitStopped(pc));
    }

    void removedNextAfterHandOffStops()
    {
        PlaybackController pc(silent());
        pc.playTracks({m_a, m_b}, 0);
        QTRY_COMPARE_WITH_TIMEOUT(pc.player()->playbackState(), State::Playing, kTimeoutMs);
        QTest::qWait(300);
        pc.queue()->removeAt(1); // m_b was already committed
        QVERIFY(waitStopped(pc));
        QCOMPARE(pc.currentTrack()->id, 1);
        QTest::qWait(300);
        QCOMPARE(pc.player()->playbackState(), State::Stopped); // m_b did not keep playing
    }

    void brokenTracksAreSkipped()
    {
        PlaybackController pc(silent());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral(".*")));
        pc.playTracks({m_missing, m_a}, 0);
        QTRY_COMPARE_WITH_TIMEOUT(pc.queue()->currentIndex(), 1, kTimeoutMs);
        QTRY_COMPARE_WITH_TIMEOUT(pc.player()->playbackState(), State::Playing, kTimeoutMs);
        pc.stop();

        // Nothing playable: gives up instead of looping (repeat-all would otherwise spin).
        pc.queue()->setRepeatMode(Repeat::All);
        pc.playTracks({m_missing, m_missing}, 0);
        QTest::qWait(1500);
        QCOMPARE(pc.player()->playbackState(), State::Stopped);
    }

    void playPauseAndClear()
    {
        PlaybackController pc(silent());
        pc.play(); // empty queue: nothing happens
        QCOMPARE(pc.player()->playbackState(), State::Stopped);

        pc.queue()->setItems({m_long, m_a}, -1);
        pc.queue()->setCurrentIndex(-1);
        pc.play(); // starts the first track
        QTRY_COMPARE_WITH_TIMEOUT(pc.player()->playbackState(), State::Playing, kTimeoutMs);
        QCOMPARE(pc.currentTrack()->id, 4);
        pc.togglePlayPause();
        QTRY_COMPARE_WITH_TIMEOUT(pc.player()->playbackState(), State::Paused, kTimeoutMs);
        pc.togglePlayPause();
        QTRY_COMPARE_WITH_TIMEOUT(pc.player()->playbackState(), State::Playing, kTimeoutMs);

        QSignalSpy changed(&pc, &PlaybackController::currentTrackChanged);
        pc.clearQueue();
        QCOMPARE(pc.player()->playbackState(), State::Stopped);
        QVERIFY(!pc.currentTrack());
        QCOMPARE(pc.queue()->count(), 0);
        QCOMPARE(changed.count(), 1);
    }
};

QTEST_GUILESS_MAIN(TestPlaybackController)
#include "tst_playbackcontroller.moc"

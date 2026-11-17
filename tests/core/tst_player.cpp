// SPDX-License-Identifier: GPL-3.0-or-later
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <taglib/fileref.h>
#include <taglib/tpropertymap.h>

#include "player.h"
#include "testmedia.h"

using mmm::Player;
using State = mmm::Player::PlaybackState;

namespace {

constexpr int kToneMs = 2000;
constexpr int kTimeoutMs = 10000;

// Real-time sink so position advances like on real hardware, but silent.
mmm::PlayerOptions testOptions()
{
    return {.audioSink = QByteArrayLiteral("fakesink sync=true")};
}

} // namespace

class TestPlayer : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_flac, m_mp3, m_wav;

    void waitForState(Player &p, State s)
    {
        QTRY_COMPARE_WITH_TIMEOUT(p.playbackState(), s, kTimeoutMs);
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_flac = m_dir.filePath(QStringLiteral("tone.flac"));
        m_mp3 = m_dir.filePath(QStringLiteral("tone 320.mp3")); // space: URI escaping
        m_wav = m_dir.filePath(QStringLiteral("tone.wav"));

        QCOMPARE(mmm::test::generateTone(m_flac, QStringLiteral("flacenc"), kToneMs), QString());
        QCOMPARE(mmm::test::generateTone(
                     m_mp3, QStringLiteral("lamemp3enc target=bitrate bitrate=320 cbr=true ! xingmux"),
                     kToneMs),
                 QString());
        QCOMPARE(mmm::test::generateTone(m_wav, QStringLiteral("wavenc"), kToneMs), QString());
    }

    void initialState()
    {
        Player p(testOptions());
        QCOMPARE(p.playbackState(), State::Stopped);
        QCOMPARE(p.position(), 0);
        QCOMPARE(p.duration(), -1);
        QVERIFY(p.source().isEmpty());
        p.play(); // no source: no-op
        QCOMPARE(p.playbackState(), State::Stopped);
    }

    void playsToEnd_data()
    {
        QTest::addColumn<QString>("file");
        QTest::newRow("flac") << m_flac;
        QTest::newRow("mp3-320") << m_mp3;
        QTest::newRow("wav") << m_wav;
    }

    void playsToEnd()
    {
        QFETCH(QString, file);
        Player p(testOptions());
        QSignalSpy eos(&p, &Player::endOfStream);
        QSignalSpy errors(&p, &Player::errorOccurred);
        QSignalSpy positions(&p, &Player::positionChanged);

        p.setSource(QUrl::fromLocalFile(file));
        p.play();
        waitForState(p, State::Playing);
        QTRY_VERIFY_WITH_TIMEOUT(p.duration() > 0, kTimeoutMs);
        QVERIFY2(qAbs(p.duration() - kToneMs) < 100, qPrintable(QString::number(p.duration())));
        QVERIFY(p.isSeekable());

        QTRY_COMPARE_WITH_TIMEOUT(eos.count(), 1, kTimeoutMs);
        QCOMPARE(p.playbackState(), State::Stopped);
        QCOMPARE(p.position(), 0);
        QVERIFY(positions.count() > 3); // ticked while playing
        QCOMPARE(errors.count(), 0);
    }

    void pauseAndResume()
    {
        Player p(testOptions());
        p.setSource(QUrl::fromLocalFile(m_flac));
        p.play();
        waitForState(p, State::Playing);
        QTest::qWait(300);

        p.pause();
        waitForState(p, State::Paused);
        const qint64 pausedAt = p.position();
        QVERIFY(pausedAt > 0);
        QTest::qWait(300);
        QCOMPARE(p.position(), pausedAt); // no ticks while paused

        p.play();
        waitForState(p, State::Playing);
        QTRY_VERIFY_WITH_TIMEOUT(p.position() > pausedAt, kTimeoutMs);
    }

    void pauseFromStoppedPrerolls()
    {
        Player p(testOptions());
        p.setSource(QUrl::fromLocalFile(m_wav));
        p.pause();
        waitForState(p, State::Paused);
        QTRY_VERIFY_WITH_TIMEOUT(p.duration() > 0, kTimeoutMs);
    }

    void seekWhilePaused()
    {
        Player p(testOptions());
        p.setSource(QUrl::fromLocalFile(m_flac));
        p.pause();
        waitForState(p, State::Paused);
        QTRY_VERIFY_WITH_TIMEOUT(p.isSeekable(), kTimeoutMs);

        p.seek(1500);
        QCOMPARE(p.position(), 1500);
        p.play();
        waitForState(p, State::Playing);
        QTest::qWait(150);
        QVERIFY2(p.position() >= 1500, qPrintable(QString::number(p.position())));
    }

    void seekBeforePrerollIsDeferred()
    {
        Player p(testOptions());
        p.setSource(QUrl::fromLocalFile(m_mp3));
        p.seek(1200);
        p.play();
        waitForState(p, State::Playing);
        QTRY_VERIFY_WITH_TIMEOUT(p.position() >= 1200, kTimeoutMs);
    }

    void seekIsClamped()
    {
        Player p(testOptions());
        p.setSource(QUrl::fromLocalFile(m_wav));
        p.pause();
        waitForState(p, State::Paused);
        QTRY_VERIFY_WITH_TIMEOUT(p.duration() > 0, kTimeoutMs);
        p.seek(-500);
        QCOMPARE(p.position(), 0);
        p.seek(kToneMs * 10);
        QCOMPARE(p.position(), p.duration());
    }

    void stopResetsPosition()
    {
        Player p(testOptions());
        p.setSource(QUrl::fromLocalFile(m_flac));
        p.play();
        waitForState(p, State::Playing);
        QTRY_VERIFY_WITH_TIMEOUT(p.position() > 200, kTimeoutMs);

        p.stop();
        QCOMPARE(p.playbackState(), State::Stopped);
        QCOMPARE(p.position(), 0);

        p.play(); // replay from start after stop
        waitForState(p, State::Playing);
    }

    void changingSourceStops()
    {
        Player p(testOptions());
        QSignalSpy sourceSpy(&p, &Player::sourceChanged);
        p.setSource(QUrl::fromLocalFile(m_flac));
        p.play();
        waitForState(p, State::Playing);

        p.setSource(QUrl::fromLocalFile(m_wav));
        QCOMPARE(p.playbackState(), State::Stopped);
        QCOMPARE(p.duration(), -1);
        QCOMPARE(sourceSpy.count(), 2);
        p.play();
        waitForState(p, State::Playing);
    }

    void volume()
    {
        Player p(testOptions());
        QSignalSpy spy(&p, &Player::volumeChanged);
        QCOMPARE(p.volume(), 1.0);
        p.setVolume(0.25);
        QCOMPARE(p.volume(), 0.25);
        p.setVolume(7.0);
        QCOMPARE(p.volume(), 1.0);
        p.setVolume(-1.0);
        QCOMPARE(p.volume(), 0.0);
        p.setVolume(0.0); // unchanged: no signal
        QCOMPARE(spy.count(), 3);
    }

    void missingFileReportsError()
    {
        Player p(testOptions());
        QSignalSpy errors(&p, &Player::errorOccurred);
        p.setSource(QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("does-not-exist.flac"))));
        p.play();
        QTRY_VERIFY_WITH_TIMEOUT(errors.count() >= 1, kTimeoutMs);
        QTest::qWait(200);
        QCOMPARE(errors.count(), 1); // GStreamer posts several ERRORs; report one
        QCOMPARE(p.playbackState(), State::Stopped);
    }

    void notAudioReportsError()
    {
        const QString path = m_dir.filePath(QStringLiteral("garbage.flac"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray(4096, 'x'));
        }
        Player p(testOptions());
        QSignalSpy errors(&p, &Player::errorOccurred);
        p.setSource(QUrl::fromLocalFile(path));
        p.play();
        QTRY_VERIFY_WITH_TIMEOUT(errors.count() >= 1, kTimeoutMs);
        QTest::qWait(200);
        QCOMPARE(errors.count(), 1); // GStreamer posts several ERRORs; report one
        QCOMPARE(p.playbackState(), State::Stopped);
    }

    void staleMessagesIgnoredAfterSourceChange()
    {
        // A failing track must not report errors after we've moved on to a good one.
        Player p(testOptions());
        QSignalSpy errors(&p, &Player::errorOccurred);
        QSignalSpy eos(&p, &Player::endOfStream);
        p.setSource(QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("missing.wav"))));
        p.play(); // errors are now queued...
        p.setSource(QUrl::fromLocalFile(m_wav)); // ...but we switch before handling them
        p.play();
        waitForState(p, State::Playing);
        QTRY_COMPARE_WITH_TIMEOUT(eos.count(), 1, kTimeoutMs);
        QCOMPARE(errors.count(), 0);
    }

    // --- Gapless (M5) ---

    void gaplessChainAcrossFormats()
    {
        Player p(testOptions());
        QSignalSpy advanced(&p, &Player::advancedToNext);
        QSignalSpy eos(&p, &Player::endOfStream);
        QSignalSpy states(&p, &Player::playbackStateChanged);
        QSignalSpy errors(&p, &Player::errorOccurred);
        const QUrl a = QUrl::fromLocalFile(m_flac), b = QUrl::fromLocalFile(m_mp3), c = QUrl::fromLocalFile(m_wav);

        // Queue the following track each time one starts, like the controller does.
        connect(&p, &Player::advancedToNext, &p, [&](const QUrl &url) {
            if (url == b)
                p.setNextSource(c);
        });
        p.setSource(a);
        p.setNextSource(b);
        QCOMPARE(p.nextSource(), b);
        QElapsedTimer t;
        t.start();
        p.play();
        waitForState(p, State::Playing);
        states.clear();

        QTRY_COMPARE_WITH_TIMEOUT(advanced.count(), 1, kTimeoutMs);
        QCOMPARE(advanced.at(0).at(0).toUrl(), b);
        QCOMPARE(p.source(), b);
        QVERIFY2(p.position() < 1000, qPrintable(QString::number(p.position()))); // position restarts per track
        QTRY_VERIFY_WITH_TIMEOUT(p.duration() > 0, kTimeoutMs); // re-queried for the new track

        QTRY_COMPARE_WITH_TIMEOUT(advanced.count(), 2, kTimeoutMs);
        QCOMPARE(advanced.at(1).at(0).toUrl(), c);
        QTRY_COMPARE_WITH_TIMEOUT(eos.count(), 1, kTimeoutMs);
        const qint64 elapsed = t.elapsed();

        // Never stopped between tracks: the only state change is the final Stopped.
        QCOMPARE(states.count(), 1);
        QCOMPARE(states.at(0).at(0).value<State>(), State::Stopped);
        QCOMPARE(errors.count(), 0);
        // Three 2 s tracks back to back (real-time sink): ~6 s, without per-track restarts.
        QVERIFY2(elapsed > 5500 && elapsed < 9000, qPrintable(QString::number(elapsed)));
    }

    void setSourceClearsUpcomingStopKeepsIt()
    {
        Player p(testOptions());
        QSignalSpy advanced(&p, &Player::advancedToNext);
        QSignalSpy eos(&p, &Player::endOfStream);
        p.setSource(QUrl::fromLocalFile(m_flac));
        p.setNextSource(QUrl::fromLocalFile(m_wav));
        p.setSource(QUrl::fromLocalFile(m_mp3)); // replacing the source discards what followed it
        QVERIFY(p.nextSource().isEmpty());
        p.play();
        QTRY_COMPARE_WITH_TIMEOUT(eos.count(), 1, kTimeoutMs);
        QCOMPARE(advanced.count(), 0);

        // stop() after the next was committed (very early for short local files): play()
        // must replay the *current* source (committing overwrote playbin3's uri), and the
        // upcoming source still follows it.
        p.setSource(QUrl::fromLocalFile(m_flac));
        p.setNextSource(QUrl::fromLocalFile(m_wav));
        p.play();
        waitForState(p, State::Playing);
        QTest::qWait(300);
        p.stop();
        QCOMPARE(p.nextSource(), QUrl::fromLocalFile(m_wav)); // handed back, not lost
        p.play();
        waitForState(p, State::Playing);
        QCOMPARE(p.source(), QUrl::fromLocalFile(m_flac));
        QTest::qWait(500);
        QCOMPARE(advanced.count(), 0); // still the current track, not jumped to the next
        QTRY_COMPARE_WITH_TIMEOUT(advanced.count(), 1, kTimeoutMs);
        QCOMPARE(advanced.at(0).at(0).toUrl(), QUrl::fromLocalFile(m_wav));
        QTRY_COMPARE_WITH_TIMEOUT(eos.count(), 2, kTimeoutMs);
    }

    // --- ReplayGain (M5) ---

    void replayGain()
    {
        const QString tagged = m_dir.filePath(QStringLiteral("rg.flac"));
        QVERIFY(QFile::copy(m_flac, tagged));
        QFile::setPermissions(tagged, QFile::ReadOwner | QFile::WriteOwner);
        {
            const QByteArray name = QFile::encodeName(tagged);
            TagLib::FileRef ref(name.constData());
            TagLib::PropertyMap props;
            props.replace("REPLAYGAIN_TRACK_GAIN", TagLib::String("-6.00 dB"));
            props.replace("REPLAYGAIN_TRACK_PEAK", TagLib::String("0.5"));
            props.replace("REPLAYGAIN_ALBUM_GAIN", TagLib::String("-9.00 dB"));
            props.replace("REPLAYGAIN_ALBUM_PEAK", TagLib::String("0.5"));
            ref.setProperties(props);
            QVERIFY(ref.save());
        }

        auto appliedGain = [&](Player::ReplayGainMode mode, const QString &file) {
            Player p(testOptions());
            p.setReplayGainMode(mode);
            p.setSource(QUrl::fromLocalFile(file));
            p.play();
            if (!QTest::qWaitFor([&] { return p.playbackState() == State::Playing; }, kTimeoutMs))
                return 999.0;
            QTest::qWait(300); // tags reach rgvolume with the first buffers
            return p.replayGainDb();
        };

        QCOMPARE(Player(testOptions()).replayGainMode(), Player::ReplayGainMode::Album); // default
        QVERIFY(qAbs(appliedGain(Player::ReplayGainMode::Track, tagged) - (-6.0)) < 0.01);
        QVERIFY(qAbs(appliedGain(Player::ReplayGainMode::Album, tagged) - (-9.0)) < 0.01);
        QCOMPARE(appliedGain(Player::ReplayGainMode::Off, tagged), 0.0);
        QCOMPARE(appliedGain(Player::ReplayGainMode::Album, m_flac), 0.0); // untagged: no change

        // Off → Album while stopped installs the filter right away.
        Player p(testOptions());
        p.setReplayGainMode(Player::ReplayGainMode::Off);
        QSignalSpy modeSpy(&p, &Player::replayGainModeChanged);
        p.setReplayGainMode(Player::ReplayGainMode::Album);
        QCOMPARE(modeSpy.count(), 1);
        p.setSource(QUrl::fromLocalFile(tagged));
        p.play();
        waitForState(p, State::Playing);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(p.replayGainDb() - (-9.0)) < 0.01, kTimeoutMs);
    }

    void destroyWhilePlaying()
    {
        for (int i = 0; i < 5; ++i) {
            auto p = std::make_unique<Player>(testOptions());
            p->setSource(QUrl::fromLocalFile(m_flac));
            p->play();
            if (i % 2)
                QTest::qWait(50);
            p.reset(); // must not crash, leak, or deliver queued messages to a dead object
        }
        QTest::qWait(100); // drain the event loop
    }
};

QTEST_GUILESS_MAIN(TestPlayer)
#include "tst_player.moc"

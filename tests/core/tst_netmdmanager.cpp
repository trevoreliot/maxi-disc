// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSignalSpy>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTest>

#include "fakenetmd.h"
#include "mdtitle.h"
#include "netmdmanager.h"

using namespace mmm;
using State = NetMdManager::State;

namespace {

NetMdDevice device(const char *id)
{
    NetMdDevice d;
    d.id = QString::fromLatin1(id);
    d.name = QStringLiteral("Sony MZ-NE410");
    return d;
}

} // namespace

// The manager and its worker thread against the in-memory recorder (no USB).
class TestNetMdManager : public QObject
{
    Q_OBJECT

    std::shared_ptr<test::FakeRecorder> m_recorder;
    QTemporaryDir m_temp;
    int m_decodeDelayMs = 0;
    std::unique_ptr<NetMdManager> m_manager;

    NetMdManager &manager() { return *m_manager; }
    void openRecorder()
    {
        manager().setDevicesForTesting({device("netmd:054c:00ca:1:5")});
        manager().select(QStringLiteral("netmd:054c:00ca:1:5"));
        QCOMPARE(manager().state(), State::Opening);
        QVERIFY(manager().isBusy());
        QTRY_COMPARE(manager().state(), State::Ready);
    }

private Q_SLOTS:
    void init()
    {
        m_recorder = std::make_shared<test::FakeRecorder>();
        QVERIFY(m_temp.isValid());
        m_manager = std::make_unique<NetMdManager>(
            NetMdManager::Options{test::fakeNetMd(m_recorder), false, 2000, test::fakeEncoder(m_decodeDelayMs, m_recorder), m_temp.path()});
    }
    void cleanup()
    {
        m_manager.reset(); // closes the recorder on its thread
        m_recorder.reset();
    }

    void readsTheDisc()
    {
        QSignalSpy disc(&manager(), &NetMdManager::discChanged);
        openRecorder();
        QVERIFY(manager().hasDisc());
        QCOMPARE(manager().error(), NetMdError::None);
        const MdDisc d = manager().disc();
        QCOMPARE(d.title, QStringLiteral("Scratch"));
        QCOMPARE(d.groups.size(), 1);
        QCOMPARE(d.groups[0], (MdGroup{QStringLiteral("Side A"), 0, 1}));
        QCOMPARE(d.tracks.size(), 3);
        QCOMPARE(d.tracks[2].encoding, MdEncoding::Lp4);
        QVERIFY(d.capacity.valid);
        QVERIFY(disc.count() >= 1);
        QCOMPARE(m_recorder->takeCalls(), (QStringList{QStringLiteral("open"), QStringLiteral("readDisc")}));
    }

    void renamesTracksCleanly()
    {
        openRecorder();
        m_recorder->takeCalls();
        manager().renameTrack(1, QStringLiteral("Björk – Jóga"));
        QCOMPARE(manager().state(), State::Writing);
        QTRY_COMPARE(manager().state(), State::Ready);
        QCOMPARE(manager().error(), NetMdError::None);
        QCOMPARE(manager().disc().tracks[1].title, QStringLiteral("Bjork - Joga"));
        // Written, then read back from the recorder.
        QCOMPARE(m_recorder->takeCalls(),
                 (QStringList{QStringLiteral("writeTrackTitle:1:Bjork - Joga"), QStringLiteral("readTrackTitle:1")}));
        manager().renameTrack(7, QStringLiteral("no such track")); // ignored
        QCOMPARE(manager().state(), State::Ready);
    }

    void renamesTheDiscKeepingGroups()
    {
        openRecorder();
        m_recorder->takeCalls();
        manager().renameDisc(QStringLiteral("Road Trip"));
        QTRY_COMPARE(manager().state(), State::Ready);
        QCOMPARE(manager().disc().title, QStringLiteral("Road Trip"));
        QCOMPARE(manager().disc().groups.size(), 1);
        // The header is read fresh first (groups changed elsewhere aren't lost), then read back.
        QCOMPARE(m_recorder->takeCalls(), (QStringList{QStringLiteral("readHeader"),
                                                       QStringLiteral("writeHeader:0;Road Trip//1-2;Side A//"),
                                                       QStringLiteral("readHeader")}));
        // Clearing it.
        manager().renameDisc(QString());
        QTRY_COMPARE(manager().state(), State::Ready);
        QCOMPARE(manager().disc().title, QString());
        QCOMPARE(m_recorder->header, QStringLiteral("0;//1-2;Side A//"));
        // A disc without groups, cleared: untitled (an empty header is a real answer).
        m_recorder->header = QStringLiteral("Plain");
        manager().refresh();
        QTRY_COMPARE(manager().state(), State::Ready);
        QCOMPARE(manager().disc().title, QStringLiteral("Plain"));
        manager().renameDisc(QString());
        QTRY_COMPARE(manager().state(), State::Ready);
        QCOMPARE(m_recorder->header, QString());
        QCOMPARE(manager().disc().title, QString());
    }

    void aFailedHeaderReadDoesntWrite()
    {
        openRecorder();
        m_recorder->takeCalls();
        m_recorder->headerReadError = NetMdError::Failed;
        manager().renameDisc(QStringLiteral("Road Trip"));
        QTRY_COMPARE(manager().state(), State::Ready);
        QCOMPARE(manager().error(), NetMdError::Failed);
        QCOMPARE(m_recorder->takeCalls(), QStringList{QStringLiteral("readHeader")}); // nothing written
        QCOMPARE(m_recorder->header, QStringLiteral("0;Scratch//1-2;Side A//"));
        QCOMPARE(manager().disc().groups.size(), 1); // the groups shown stay
        // Groups alone over 255 characters: refused, not written.
        m_recorder->headerReadError = NetMdError::None;
        QString groups = QStringLiteral("0;x//");
        for (int i = 1; i <= 30; ++i)
            groups += QStringLiteral("%1;Group number %1//").arg(i);
        m_recorder->header = groups;
        manager().renameDisc(QStringLiteral("T"));
        QTRY_COMPARE(manager().state(), State::Ready);
        QCOMPARE(manager().error(), NetMdError::Rejected);
        QVERIFY(!m_recorder->takeCalls().join(QLatin1Char(',')).contains(QStringLiteral("writeHeader")));
    }

    void refusedWritesAreReported()
    {
        openRecorder();
        m_recorder->writeError = NetMdError::Rejected;
        manager().renameTrack(0, QStringLiteral("New"));
        QTRY_COMPARE(manager().state(), State::Ready);
        QCOMPARE(manager().error(), NetMdError::Rejected);
        QCOMPARE(manager().disc().tracks[0].title, QStringLiteral("Intro")); // what the recorder still has
        QVERIFY(manager().hasDisc());
    }

    void openingErrors_data()
    {
        QTest::addColumn<int>("error");
        QTest::newRow("permission") << int(NetMdError::PermissionDenied);
        QTest::newRow("busy") << int(NetMdError::Busy);
    }
    void openingErrors()
    {
        QFETCH(int, error);
        m_recorder->openError = NetMdError(error);
        openRecorder();
        QVERIFY(!manager().hasDisc());
        QCOMPARE(manager().error(), NetMdError(error));
        QVERIFY(!manager().errorString().isEmpty());
        // Fixed (udev rule installed / other program closed): refresh opens it again.
        m_recorder->openError = NetMdError::None;
        manager().refresh();
        QTRY_VERIFY(manager().hasDisc());
        QCOMPARE(manager().error(), NetMdError::None);
    }

    void noDisc()
    {
        m_recorder->noDisc = true;
        openRecorder();
        QVERIFY(!manager().hasDisc());
        QCOMPARE(manager().error(), NetMdError::NoDisc);
        m_recorder->noDisc = false;
        manager().refresh();
        QTRY_VERIFY(manager().hasDisc());
    }

    void unplugAndSwitch()
    {
        openRecorder();
        // Unplugged: the disc goes, the handle is let go.
        manager().setDevicesForTesting({});
        QVERIFY(!manager().hasDisc());
        QCOMPARE(manager().error(), NetMdError::Disconnected);
        QStringList seen;
        QTRY_VERIFY2((seen += m_recorder->takeCalls()).contains(QStringLiteral("close")), qPrintable(seen.join(QLatin1Char(','))));

        // A slow read, then another device: the first answer is dropped.
        manager().setDevicesForTesting({device("netmd:a"), device("netmd:b")});
        m_recorder->delayMs = 150;
        manager().select(QStringLiteral("netmd:a"));
        manager().select(QStringLiteral("netmd:b"));
        m_recorder->tracks.removeLast();
        QTRY_COMPARE_WITH_TIMEOUT(manager().state(), State::Ready, 5000);
        QCOMPARE(manager().selectedId(), QStringLiteral("netmd:b"));
        QCOMPARE(manager().disc().tracks.size(), 2); // b's read, not a's

        // Deselect: closed, idle.
        manager().select(QString());
        QCOMPARE(manager().state(), State::Idle);
        QVERIFY(!manager().hasDisc());
    }

    // --- Recording (M11) ---
    QList<MdUploadItem> items(int n, const QString &brokenAt = {})
    {
        QList<MdUploadItem> out;
        for (int i = 0; i < n; ++i) {
            MdUploadItem item;
            item.track.path = QStringLiteral("/lib/%1.flac").arg(QString::number(i) == brokenAt ? QStringLiteral("broken") : QString::number(i));
            item.track.title = QStringLiteral("Song %1").arg(i + 1);
            item.title = item.track.title;
            out << item;
        }
        return out;
    }
    // Tracks named "/lib/<name>.flac" ("broken…" can't be decoded, "slow…" takes 600 ms).
    QList<MdUploadItem> itemsNamed(const QStringList &names)
    {
        QList<MdUploadItem> out;
        for (int i = 0; i < names.size(); ++i) {
            MdUploadItem item;
            item.track.path = QStringLiteral("/lib/%1.flac").arg(names[i]);
            item.title = QStringLiteral("Song %1").arg(i + 1);
            out << item;
        }
        return out;
    }
    // Prepared tracks on disk (in the recordings' temporary folders).
    int tempFiles() const
    {
        int n = 0;
        QDirIterator it(m_temp.path(), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            ++n;
        }
        return n;
    }
    void restartWithDecodeDelay(int ms)
    {
        m_decodeDelayMs = ms;
        cleanup();
        init();
        m_decodeDelayMs = 0;
        openRecorder();
    }
    QList<MdItemState> states() const
    {
        QList<MdItemState> out;
        for (const MdUploadItem &i : m_manager->uploadItems())
            out << i.state;
        return out;
    }

    void recordsTracksAndTitlesTheDisc()
    {
        openRecorder();
        m_recorder->takeCalls();
        QSignalSpy progress(&manager(), &NetMdManager::uploadProgressChanged);
        QSignalSpy finished(&manager(), &NetMdManager::uploadFinished);
        QVERIFY(manager().startUpload(items(3), QStringLiteral("Café Mix")));
        QVERIFY(manager().isUploading());
        QVERIFY(manager().isBusy());
        QVERIFY(!manager().startUpload(items(1))); // one at a time
        manager().renameTrack(0, QStringLiteral("x")); // blocked while recording
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.at(0).at(0).toBool(), true);
        QVERIFY(!manager().isUploading());
        QCOMPARE(manager().state(), State::Ready);
        QCOMPARE(states(), (QList<MdItemState>{MdItemState::Done, MdItemState::Done, MdItemState::Done}));
        QCOMPARE(manager().disc().tracks.size(), 6);
        QCOMPARE(manager().disc().tracks.last().title, QStringLiteral("Song 3"));
        QCOMPARE(manager().disc().title, QStringLiteral("Cafe Mix")); // groups kept, cleaned
        QCOMPARE(manager().disc().groups.size(), 1);
        QVERIFY(!progress.isEmpty());
        const QStringList calls = m_recorder->takeCalls();
        QCOMPARE(calls.filter(QStringLiteral("uploadTrack:")).size(), 3);
        QVERIFY(calls.contains(QStringLiteral("uploadTrack:Song 1:176400")));
        QVERIFY(calls.contains(QStringLiteral("writeHeader:0;Cafe Mix//1-2;Side A//")));
        QVERIFY(!calls.contains(QStringLiteral("writeTrackTitle:0:x")));
        QVERIFY(QDir(m_temp.path()).entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty()); // decoded audio is gone
    }

    // M12: the mode goes to the encoder and the recorder, for the whole run.
    void recordsInLp2AndLp4()
    {
        openRecorder();
        m_recorder->uploadModes.clear();
        const int before = static_cast<int>(manager().disc().tracks.size());
        QSignalSpy finished(&manager(), &NetMdManager::uploadFinished);
        QVERIFY(manager().startUpload(items(2), {}, MdRecordMode::Lp2));
        QCOMPARE(manager().uploadMode(), MdRecordMode::Lp2);
        QVERIFY(finished.wait(5000));
        QVERIFY(manager().startUpload(items(1), {}, MdRecordMode::Lp4));
        QVERIFY(finished.wait(5000));
        QCOMPARE(m_recorder->uploadModes, (QList<MdRecordMode>{MdRecordMode::Lp2, MdRecordMode::Lp2, MdRecordMode::Lp4}));
        const QStringList calls = m_recorder->takeCalls();
        QVERIFY(calls.contains(QStringLiteral("uploadTrack:Song 1:%1").arg(43 * 384)));
        QVERIFY(calls.contains(QStringLiteral("uploadTrack:Song 1:%1").arg(43 * 192)));
        const QList<MdTrack> tracks = manager().disc().tracks;
        QCOMPARE(tracks.size(), before + 3);
        QCOMPARE(tracks[before].encoding, MdEncoding::Lp2);
        QCOMPARE(tracks.last().encoding, MdEncoding::Lp4);
    }

    void cancelFinishesTheTrackBeingRecorded()
    {
        openRecorder();
        m_recorder->uploadStepMs = 60; // ~240 ms per track
        QSignalSpy finished(&manager(), &NetMdManager::uploadFinished);
        QVERIFY(manager().startUpload(items(3)));
        QTRY_COMPARE(manager().uploadProgress().bytesSent > 0, true);
        manager().cancelUpload();
        QVERIFY(manager().isCancelling());
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.at(0).at(0).toBool(), false);
        QCOMPARE(states(), (QList<MdItemState>{MdItemState::Done, MdItemState::Skipped, MdItemState::Skipped}));
        QCOMPARE(manager().disc().tracks.size(), 4); // the first went on whole
        QCOMPARE(manager().state(), State::Ready);
    }

    void cancelWhileDecodingStopsAtOnce()
    {
        m_decodeDelayMs = 2000;
        cleanup();
        init();
        openRecorder();
        QSignalSpy finished(&manager(), &NetMdManager::uploadFinished);
        QVERIFY(manager().startUpload(items(2)));
        QTRY_COMPARE(manager().uploadItems().first().state, MdItemState::Decoding);
        manager().cancelUpload();
        QVERIFY(finished.wait(1000)); // not the 2 s decode
        QCOMPARE(states(), (QList<MdItemState>{MdItemState::Skipped, MdItemState::Skipped}));
        QCOMPARE(m_recorder->uploads, 0);
        m_decodeDelayMs = 0;
    }

    void aFailedTrackStopsTheRest()
    {
        openRecorder();
        m_recorder->uploadError = NetMdError::Rejected;
        m_recorder->failUploadAt = 1;
        QSignalSpy finished(&manager(), &NetMdManager::uploadFinished);
        QVERIFY(manager().startUpload(items(3)));
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.at(0).at(0).toBool(), false);
        QCOMPARE(states(), (QList<MdItemState>{MdItemState::Done, MdItemState::Failed, MdItemState::Skipped}));
        QVERIFY(!manager().uploadItems()[1].error.isEmpty());
        QCOMPARE(manager().error(), NetMdError::Rejected);
        QCOMPARE(manager().disc().tracks.size(), 4);

        // A file that can't be decoded: same, nothing sent for it.
        m_recorder->uploadError = NetMdError::None;
        finished.clear();
        QVERIFY(manager().startUpload(items(3, QStringLiteral("0"))));
        QVERIFY(finished.wait(5000));
        QCOMPARE(states(), (QList<MdItemState>{MdItemState::Failed, MdItemState::Skipped, MdItemState::Skipped}));
        QVERIFY(manager().uploadItems()[0].error.contains(QStringLiteral("decode")));
    }

    // A recorder that stops answering mid-recording but stays plugged in is reopened (the real
    // backend resets its USB port first): the disc comes back without a replug.
    // --- M12c: the next track is prepared while one uploads ---
    void preparesTheNextTrackWhileOneUploads()
    {
        restartWithDecodeDelay(100);
        m_recorder->uploadStepMs = 50; // ~200 ms per track
        m_recorder->takeEvents();
        QSignalSpy finished(&manager(), &NetMdManager::uploadFinished);
        QVERIFY(manager().startUpload(items(3), QStringLiteral("Mix")));
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.first().first().toBool(), true);
        QCOMPARE(states(), (QList<MdItemState>{MdItemState::Done, MdItemState::Done, MdItemState::Done}));
        const QStringList e = m_recorder->takeEvents();
        const auto at = [&](const char *what) {
            const qsizetype i = e.indexOf(QString::fromLatin1(what));
            return i < 0 ? std::numeric_limits<qsizetype>::max() : i;
        };
        // In order on the disc; each next one prepared while the one before uploads...
        QVERIFY2(at("uploaded:Song 1") < at("upload:Song 2") && at("uploaded:Song 2") < at("upload:Song 3"), qPrintable(e.join(u',')));
        QVERIFY2(at("encode:1") < at("uploaded:Song 1"), qPrintable(e.join(u',')));
        QVERIFY2(at("encode:2") < at("uploaded:Song 2"), qPrintable(e.join(u',')));
        // ... but no further ahead than that.
        QVERIFY2(at("encode:2") > at("uploaded:Song 1"), qPrintable(e.join(u',')));
        QVERIFY(m_recorder->takeCalls().contains(QStringLiteral("writeHeader:0;Mix//1-2;Side A//")));
        QCOMPARE(tempFiles(), 0);
    }

    void aTrackPreparedAheadWaitsItsTurn()
    {
        openRecorder();
        m_recorder->uploadStepMs = 100; // ~400 ms per track; preparing is instant
        QSignalSpy finished(&manager(), &NetMdManager::uploadFinished);
        QVERIFY(manager().startUpload(items(4)));
        QTRY_COMPARE(manager().uploadItems().at(1).state, MdItemState::Ready);
        QCOMPARE(manager().uploadItems().at(0).state, MdItemState::Uploading);
        QCOMPARE(manager().uploadItems().at(2).state, MdItemState::Pending); // one ahead at most
        QCOMPARE(manager().uploadIndex(), 0);
        QVERIFY(tempFiles() <= 2);
        QVERIFY(finished.wait(5000));
        QCOMPARE(states(), QList<MdItemState>(4, MdItemState::Done));
        QCOMPARE(tempFiles(), 0);
    }

    void aSlowTrackToPrepareIsWaitedFor()
    {
        openRecorder();
        QSignalSpy finished(&manager(), &NetMdManager::uploadFinished);
        QVERIFY(manager().startUpload(itemsNamed({QStringLiteral("0"), QStringLiteral("slow1")})));
        // The first is on the disc, the second still being prepared: it's the one shown.
        QTRY_COMPARE(manager().uploadItems().at(0).state, MdItemState::Done);
        QCOMPARE(manager().uploadItems().at(1).state, MdItemState::Decoding);
        QCOMPARE(manager().uploadIndex(), 1);
        QVERIFY(finished.wait(5000));
        QCOMPARE(states(), (QList<MdItemState>{MdItemState::Done, MdItemState::Done}));
    }

    void aTrackThatCantBePreparedWaitsForTheOneBefore()
    {
        openRecorder();
        m_recorder->uploadStepMs = 50;
        m_recorder->takeEvents();
        m_recorder->takeCalls();
        QSignalSpy finished(&manager(), &NetMdManager::uploadFinished);
        QVERIFY(manager().startUpload(itemsNamed({QStringLiteral("0"), QStringLiteral("broken1"), QStringLiteral("2")}),
                                      QStringLiteral("Mix")));
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.first().first().toBool(), false);
        // The one before it went on whole; it failed in its turn; nothing after it was prepared.
        QCOMPARE(states(), (QList<MdItemState>{MdItemState::Done, MdItemState::Failed, MdItemState::Skipped}));
        QVERIFY(manager().uploadItems()[1].error.contains(QStringLiteral("decode")));
        const QStringList e = m_recorder->takeEvents();
        QVERIFY(e.contains(QStringLiteral("uploaded:Song 1")));
        QVERIFY(!e.contains(QStringLiteral("encode:2")));
        QVERIFY(!m_recorder->takeCalls().join(u',').contains(QStringLiteral("writeHeader"))); // not titled
        QCOMPARE(tempFiles(), 0);
    }

    void aFailedUploadStopsTheTrackBeingPrepared()
    {
        openRecorder();
        m_recorder->uploadStepMs = 30;
        m_recorder->uploadError = NetMdError::Rejected;
        m_recorder->failUploadAt = 0;
        m_recorder->takeEvents();
        QSignalSpy finished(&manager(), &NetMdManager::uploadFinished);
        QVERIFY(manager().startUpload(itemsNamed({QStringLiteral("0"), QStringLiteral("slow1"), QStringLiteral("2")})));
        QVERIFY(finished.wait(5000));
        QCOMPARE(states(), (QList<MdItemState>{MdItemState::Failed, MdItemState::Skipped, MdItemState::Skipped}));
        QCOMPARE(manager().error(), NetMdError::Rejected); // the recorder's error, not a decode one
        QTRY_VERIFY(m_recorder->events.contains(QStringLiteral("encode-canceled:slow1")));
        QTRY_COMPARE(tempFiles(), 0);
        QVERIFY(!m_recorder->takeEvents().contains(QStringLiteral("encode:2")));
    }

    void cancelStopsTheTrackBeingPrepared()
    {
        openRecorder();
        m_recorder->uploadStepMs = 60;
        QSignalSpy finished(&manager(), &NetMdManager::uploadFinished);
        QVERIFY(manager().startUpload(itemsNamed({QStringLiteral("0"), QStringLiteral("slow1"), QStringLiteral("2")})));
        QTRY_COMPARE(manager().uploadItems().at(0).state, MdItemState::Uploading);
        QTRY_COMPARE(manager().uploadItems().at(1).state, MdItemState::Decoding);
        manager().cancelUpload();
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.first().first().toBool(), false);
        QCOMPARE(states(), (QList<MdItemState>{MdItemState::Done, MdItemState::Skipped, MdItemState::Skipped}));
        QCOMPARE(m_recorder->uploads, 1);
        QTRY_COMPARE(tempFiles(), 0);
    }

    void cancelDropsTheTrackPreparedAhead()
    {
        openRecorder();
        m_recorder->uploadStepMs = 100;
        QSignalSpy finished(&manager(), &NetMdManager::uploadFinished);
        QVERIFY(manager().startUpload(items(3)));
        QTRY_COMPARE(manager().uploadItems().at(1).state, MdItemState::Ready);
        QCOMPARE(tempFiles(), 2);
        manager().cancelUpload();
        QCOMPARE(tempFiles(), 1); // the one prepared ahead went at once; the uploading one stays
        QVERIFY(finished.wait(5000));
        QCOMPARE(states(), (QList<MdItemState>{MdItemState::Done, MdItemState::Skipped, MdItemState::Skipped}));
        QCOMPARE(m_recorder->uploads, 1);
        QCOMPARE(tempFiles(), 0);
    }

    void unplugWhileOneUploadsAndTheNextIsPrepared()
    {
        openRecorder();
        m_recorder->uploadStepMs = 100;
        QSignalSpy finished(&manager(), &NetMdManager::uploadFinished);
        QVERIFY(manager().startUpload(items(3)));
        QTRY_COMPARE(manager().uploadItems().at(1).state, MdItemState::Ready);
        manager().setDevicesForTesting({});
        QCOMPARE(finished.count(), 1); // at once
        QCOMPARE(states(), (QList<MdItemState>{MdItemState::Failed, MdItemState::Skipped, MdItemState::Skipped}));
        QCOMPARE(manager().error(), NetMdError::Disconnected);
        QTRY_COMPARE(tempFiles(), 0); // once the upload on its way gives up
    }

    void unplugAfterACancelLeavesTheRestSkipped()
    {
        openRecorder();
        QSignalSpy finished(&manager(), &NetMdManager::uploadFinished);
        QVERIFY(manager().startUpload(itemsNamed({QStringLiteral("0"), QStringLiteral("slow1")})));
        QTRY_COMPARE(manager().uploadItems().at(1).state, MdItemState::Decoding);
        QTRY_COMPARE(manager().uploadItems().at(0).state, MdItemState::Done);
        manager().cancelUpload(); // waits for the encode to stop...
        manager().setDevicesForTesting({}); // ... and the recorder goes meanwhile
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(states(), (QList<MdItemState>{MdItemState::Done, MdItemState::Skipped})); // canceled, not failed
        QCOMPARE(manager().error(), NetMdError::Disconnected);
        QTRY_COMPARE(tempFiles(), 0);
    }

    void quittingWhileOneUploadsAndTheNextIsPrepared()
    {
        openRecorder();
        m_recorder->uploadStepMs = 50;
        QVERIFY(manager().startUpload(itemsNamed({QStringLiteral("0"), QStringLiteral("slow1"), QStringLiteral("2")})));
        QTRY_COMPARE(manager().uploadItems().at(1).state, MdItemState::Decoding);
        QElapsedTimer quitting;
        quitting.start();
        m_manager.reset(); // both threads wind down: the upload finishes, the encode stops
        QVERIFY(quitting.elapsed() < 4000);
        QCOMPARE(m_recorder->uploads, 1);
        QTRY_COMPARE(tempFiles(), 0);
    }

    void aNewRecordingRightAfterAFailedOne()
    {
        openRecorder();
        m_recorder->uploadError = NetMdError::Rejected;
        m_recorder->failUploadAt = 0;
        QSignalSpy finished(&manager(), &NetMdManager::uploadFinished);
        QVERIFY(manager().startUpload(itemsNamed({QStringLiteral("0"), QStringLiteral("slow1")})));
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.last().first().toBool(), false);
        // The old run's encode may still be winding down: the new one isn't affected.
        m_recorder->uploadError = NetMdError::None;
        QVERIFY(manager().startUpload(items(2)));
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.last().first().toBool(), true);
        QCOMPARE(states(), (QList<MdItemState>{MdItemState::Done, MdItemState::Done}));
        QTRY_COMPARE(tempFiles(), 0);
    }

    void aRecorderThatStopsAnsweringIsReopened()
    {
        openRecorder();
        m_recorder->uploadError = NetMdError::Disconnected;
        m_recorder->failUploadAt = 0;
        QSignalSpy finished(&manager(), &NetMdManager::uploadFinished);
        QVERIFY(manager().startUpload(items(2)));
        QVERIFY(finished.wait(5000));
        QCOMPARE(states(), (QList<MdItemState>{MdItemState::Failed, MdItemState::Skipped}));
        QVERIFY(!manager().uploadItems()[0].error.isEmpty());
        QTRY_VERIFY(manager().hasDisc());
        QCOMPARE(manager().state(), NetMdManager::State::Ready);
        QCOMPARE(manager().error(), NetMdError::None);
        m_recorder->uploadError = NetMdError::None;
    }

    void unplugDuringARecording()
    {
        openRecorder();
        m_recorder->uploadStepMs = 100;
        QSignalSpy finished(&manager(), &NetMdManager::uploadFinished);
        QVERIFY(manager().startUpload(items(2)));
        QTRY_COMPARE(manager().uploadItems().first().state, MdItemState::Uploading);
        manager().select(QString()); // ignored while recording
        QVERIFY(manager().isUploading());
        manager().setDevicesForTesting({});
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.at(0).at(0).toBool(), false);
        QCOMPARE(states(), (QList<MdItemState>{MdItemState::Failed, MdItemState::Skipped}));
        QCOMPARE(manager().error(), NetMdError::Disconnected);
        QVERIFY(!manager().isUploading());
    }

    void unplugWhileTitlingTheDisc()
    {
        openRecorder();
        QSignalSpy finished(&manager(), &NetMdManager::uploadFinished);
        QVERIFY(manager().startUpload(items(1), QStringLiteral("Mix")));
        m_recorder->delayMs = 200; // the header read for the title is slow
        QStringList seen;
        QTRY_VERIFY2((seen += m_recorder->takeCalls()).contains(QStringLiteral("readHeader")) || finished.count() > 0,
                     qPrintable(seen.join(QLatin1Char(','))));
        if (finished.isEmpty())
            manager().setDevicesForTesting({});
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(states(), QList<MdItemState>{MdItemState::Done}); // on the disc: not "failed"
        m_recorder->delayMs = 0;
    }

    void quittingDuringARecording()
    {
        openRecorder();
        m_recorder->uploadStepMs = 50;
        QVERIFY(manager().startUpload(items(3)));
        QTRY_COMPARE(manager().uploadItems().first().state, MdItemState::Uploading);
        m_manager.reset(); // the track in progress finishes on the worker; nothing hangs or leaks
        QVERIFY(m_recorder->uploads <= 2);
    }

    // --- Editing the disc (M12b). The fake, like a recorder, leaves the header alone. ---
    QStringList titles()
    {
        QStringList out;
        for (const MdTrack &t : manager().disc().tracks)
            out << t.title;
        return out;
    }
    bool finishEdit(QSignalSpy &finished)
    {
        if (!finished.wait(5000))
            return false;
        return manager().state() == State::Ready && !manager().isBusy() && manager().edit() == NetMdManager::Edit::None;
    }

    void deletesTracksTheLastFirstAndFixesGroups()
    {
        openRecorder();
        m_recorder->header = QStringLiteral("0;Scratch//1-2;Side A//3;Outro//");
        m_recorder->takeCalls();
        QSignalSpy finished(&manager(), &NetMdManager::editFinished);
        QVERIFY(manager().deleteTracks({0, 2}));
        QCOMPARE(manager().edit(), NetMdManager::Edit::Delete);
        QVERIFY(manager().isBusy());
        QVERIFY(!manager().deleteTracks({1})); // one at a time
        QVERIFY(finishEdit(finished));
        QCOMPARE(finished.first().first().toBool(), true);
        QCOMPARE(m_recorder->takeCalls(),
                 (QStringList{QStringLiteral("readHeader"), QStringLiteral("deleteTrack:2"), QStringLiteral("deleteTrack:0"),
                              QStringLiteral("writeHeader:0;Scratch//1;Side A//"), QStringLiteral("readDisc")}));
        QCOMPARE(titles(), QStringList{QString()});
        QCOMPARE(manager().disc().tracks[0].index, 0);
        QCOMPARE(manager().disc().groups, (QList<MdGroup>{MdGroup{QStringLiteral("Side A"), 0, 0}}));
        QCOMPARE(manager().disc().capacity.availableMs, 3'800'000 + 61'000 + 3'725'000);
        QCOMPARE(manager().error(), NetMdError::None);
    }

    void aDeleteThatKeepsTheGroupsDoesntWriteThem()
    {
        openRecorder();
        m_recorder->takeCalls();
        QSignalSpy finished(&manager(), &NetMdManager::editFinished);
        QVERIFY(manager().deleteTracks({2}));
        QVERIFY(finishEdit(finished));
        QCOMPARE(m_recorder->takeCalls(),
                 (QStringList{QStringLiteral("readHeader"), QStringLiteral("deleteTrack:2"), QStringLiteral("readDisc")}));
        QCOMPARE(titles(), (QStringList{QStringLiteral("Intro"), QString()}));
    }

    void aRefusedDeleteKeepsTheOnesDone()
    {
        openRecorder();
        m_recorder->header = QStringLiteral("0;Scratch//1-2;Side A//3;Outro//");
        m_recorder->takeCalls();
        m_recorder->editError = NetMdError::Rejected;
        m_recorder->failEditAt = 1; // the second (track 0)
        QSignalSpy finished(&manager(), &NetMdManager::editFinished);
        QVERIFY(manager().deleteTracks({0, 2}));
        QVERIFY(finishEdit(finished));
        QCOMPARE(finished.first().first().toBool(), false);
        QCOMPARE(manager().error(), NetMdError::Rejected);
        // Track 3 went: its group too. Then the refusal, and the disc as it is.
        QCOMPARE(m_recorder->takeCalls(),
                 (QStringList{QStringLiteral("readHeader"), QStringLiteral("deleteTrack:2"), QStringLiteral("deleteTrack:0"),
                              QStringLiteral("writeHeader:0;Scratch//1-2;Side A//"), QStringLiteral("readDisc")}));
        QCOMPARE(titles(), (QStringList{QStringLiteral("Intro"), QString()}));
        QCOMPARE(manager().disc().groups.size(), 1);
        QVERIFY(manager().hasDisc());
    }

    void withoutTheHeaderNothingIsDeleted()
    {
        openRecorder();
        m_recorder->takeCalls();
        m_recorder->headerReadError = NetMdError::Failed;
        QSignalSpy finished(&manager(), &NetMdManager::editFinished);
        QVERIFY(manager().deleteTracks({0}));
        QVERIFY(finishEdit(finished));
        QCOMPARE(manager().error(), NetMdError::Failed);
        QCOMPARE(m_recorder->takeCalls(), QStringList{QStringLiteral("readHeader")});
        QCOMPARE(titles().size(), 3);
        QVERIFY(manager().hasDisc());
        QVERIFY(manager().moveTrack(0, 1)); // the same for a move
        QVERIFY(finishEdit(finished));
        QCOMPARE(m_recorder->takeCalls(), QStringList{QStringLiteral("readHeader")});
    }

    void movesATrackWithoutReadingTheDiscAgain()
    {
        openRecorder();
        m_recorder->takeCalls();
        QSignalSpy finished(&manager(), &NetMdManager::editFinished);
        // Within "Side A" (tracks 1-2): the groups stay.
        QVERIFY(manager().moveTrack(0, 1));
        QCOMPARE(manager().edit(), NetMdManager::Edit::Move);
        QVERIFY(finishEdit(finished));
        QCOMPARE(m_recorder->takeCalls(), (QStringList{QStringLiteral("readHeader"), QStringLiteral("moveTrack:0:1")}));
        QCOMPARE(titles(), (QStringList{QString(), QStringLiteral("Intro"), QStringLiteral("Outro")}));
        QCOMPARE(manager().disc().tracks[1].index, 1);
        QCOMPARE(manager().disc().tracks, m_recorder->tracks); // what a re-read would say
        // Out of the group, past its end.
        QVERIFY(manager().moveTrack(1, 2));
        QVERIFY(finishEdit(finished));
        QCOMPARE(m_recorder->takeCalls(), (QStringList{QStringLiteral("readHeader"), QStringLiteral("moveTrack:1:2"),
                                                       QStringLiteral("writeHeader:0;Scratch//1;Side A//")}));
        QCOMPARE(manager().disc().groups, (QList<MdGroup>{MdGroup{QStringLiteral("Side A"), 0, 0}}));
        QCOMPARE(manager().disc().rawHeader, QStringLiteral("0;Scratch//1;Side A//"));
        QCOMPARE(manager().disc().tracks, m_recorder->tracks);
        QCOMPARE(manager().disc().title, QStringLiteral("Scratch"));
        QCOMPARE(finished.size(), 2);
        QCOMPARE(finished.last().first().toBool(), true);
    }

    void aRefusedMoveReadsTheDisc()
    {
        openRecorder();
        m_recorder->takeCalls();
        m_recorder->editError = NetMdError::Rejected;
        QSignalSpy finished(&manager(), &NetMdManager::editFinished);
        QVERIFY(manager().moveTrack(2, 0));
        QVERIFY(finishEdit(finished));
        QCOMPARE(manager().error(), NetMdError::Rejected);
        QCOMPARE(m_recorder->takeCalls(),
                 (QStringList{QStringLiteral("readHeader"), QStringLiteral("moveTrack:2:0"), QStringLiteral("readDisc")}));
        QCOMPARE(titles(), (QStringList{QStringLiteral("Intro"), QString(), QStringLiteral("Outro")}));
    }

    void aMoveWhoseGroupsWouldntFitIsRefused()
    {
        openRecorder();
        // 10 tracks, group "Nine" on tracks 8-9, the header full to the byte.
        for (int i = 3; i < 10; ++i)
            m_recorder->tracks << MdTrack{i, QStringLiteral("T"), 1000, MdEncoding::Sp, false, false};
        const QString tail = QStringLiteral("//8-9;Nine//");
        m_recorder->header = QStringLiteral("0;") + QString(MdTitle::kMaxHeaderBytes - 2 - tail.size(), QLatin1Char('x')) + tail;
        QCOMPARE(m_recorder->header.size(), MdTitle::kMaxHeaderBytes);
        manager().refresh();
        QTRY_COMPARE(manager().disc().tracks.size(), 10);
        m_recorder->takeCalls();
        QSignalSpy finished(&manager(), &NetMdManager::editFinished);
        QVERIFY(manager().moveTrack(8, 7)); // within the group: nothing to write
        QVERIFY(finishEdit(finished));
        QCOMPARE(manager().error(), NetMdError::None);
        m_recorder->takeCalls();
        QVERIFY(manager().moveTrack(9, 8)); // into it: "8-10", a byte more than there is
        QVERIFY(finishEdit(finished));
        QCOMPARE(manager().error(), NetMdError::Rejected);
        QCOMPARE(m_recorder->takeCalls(), QStringList{QStringLiteral("readHeader")}); // not moved
        QCOMPARE(manager().disc().tracks, m_recorder->tracks);
    }

    void aDeleteWhoseGroupsWouldntFitIsRefused()
    {
        openRecorder();
        // Written without the "0;" the app writes: rewritten, it would be 2 bytes longer.
        const QString tail = QStringLiteral("//2;G//");
        m_recorder->header = QString(MdTitle::kMaxHeaderBytes - tail.size(), QLatin1Char('x')) + tail;
        QCOMPARE(MdTitle::parseHeader(m_recorder->header).groups.size(), 1);
        m_recorder->takeCalls();
        QSignalSpy finished(&manager(), &NetMdManager::editFinished);
        QVERIFY(manager().deleteTracks({0}));
        QVERIFY(finishEdit(finished));
        QCOMPARE(manager().error(), NetMdError::Rejected);
        QCOMPARE(m_recorder->takeCalls(), QStringList{QStringLiteral("readHeader")}); // nothing deleted
        QCOMPARE(m_recorder->tracks.size(), 3);
    }

    void aDiscThatCantBeReadAfterAnEditIsOpenedAgain()
    {
        openRecorder();
        m_recorder->noDisc = true; // the erase works, reading it back doesn't
        QSignalSpy finished(&manager(), &NetMdManager::editFinished);
        QVERIFY(manager().eraseDisc());
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.first().first().toBool(), false);
        QTRY_COMPARE(manager().state(), State::Ready);
        QTRY_COMPARE(manager().error(), NetMdError::NoDisc); // reopened: what it says now
        QVERIFY(!manager().hasDisc());
        QVERIFY(manager().disc().tracks.isEmpty()); // not the list from before
    }

    void erasesTheDisc()
    {
        openRecorder();
        m_recorder->takeCalls();
        QSignalSpy finished(&manager(), &NetMdManager::editFinished);
        QVERIFY(manager().eraseDisc());
        QCOMPARE(manager().edit(), NetMdManager::Edit::Erase);
        QVERIFY(finishEdit(finished));
        QCOMPARE(m_recorder->takeCalls(), (QStringList{QStringLiteral("eraseDisc"), QStringLiteral("readDisc")}));
        QVERIFY(manager().hasDisc());
        QVERIFY(manager().disc().tracks.isEmpty());
        QVERIFY(manager().disc().title.isEmpty());
        QVERIFY(manager().disc().groups.isEmpty());
        QCOMPARE(manager().disc().capacity.availableMs, manager().disc().capacity.totalMs);
        // A refusal (write-protected) leaves the disc as it was.
        m_recorder->tracks << MdTrack{0, QStringLiteral("New"), 1000, MdEncoding::Sp, false, false};
        manager().refresh();
        QTRY_COMPARE(manager().disc().tracks.size(), 1);
        m_recorder->editError = NetMdError::Rejected;
        QVERIFY(manager().eraseDisc());
        QVERIFY(finishEdit(finished));
        QCOMPARE(manager().error(), NetMdError::Rejected);
        QCOMPARE(titles(), QStringList{QStringLiteral("New")});
    }

    void editsAreRefusedWhenTheyCant()
    {
        QVERIFY(!manager().eraseDisc()); // nothing selected
        openRecorder();
        QVERIFY(!manager().deleteTracks({}));
        QVERIFY(!manager().deleteTracks({3}));
        QVERIFY(!manager().deleteTracks({-1}));
        QVERIFY(!manager().moveTrack(1, 1));
        QVERIFY(!manager().moveTrack(0, 3));
        QVERIFY(!manager().moveTrack(-1, 0));
        // Not while recording.
        m_recorder->uploadStepMs = 50;
        QVERIFY(manager().startUpload(items(1)));
        QVERIFY(!manager().canEdit());
        QVERIFY(!manager().deleteTracks({0}));
        QVERIFY(!manager().moveTrack(0, 1));
        QVERIFY(!manager().eraseDisc());
        QTRY_VERIFY(!manager().isUploading());
        QVERIFY(manager().canEdit());
        // No disc.
        m_recorder->noDisc = true;
        manager().refresh();
        QTRY_COMPARE(manager().state(), State::Ready);
        QVERIFY(!manager().eraseDisc());
        QVERIFY(!manager().deleteTracks({0}));
    }

    void aRecorderThatStopsAnsweringDuringAnEditIsReopened()
    {
        openRecorder();
        m_recorder->editError = NetMdError::Disconnected;
        m_recorder->failEditAt = 0;
        m_recorder->takeCalls();
        QSignalSpy finished(&manager(), &NetMdManager::editFinished);
        QVERIFY(manager().deleteTracks({1}));
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.first().first().toBool(), false);
        // Reopened (the USB port reset on close), the disc read again.
        QTRY_VERIFY(manager().hasDisc());
        QTRY_COMPARE(manager().state(), State::Ready);
        QCOMPARE(manager().error(), NetMdError::None);
        const QStringList calls = m_recorder->takeCalls();
        QCOMPARE(calls.mid(0, 2), (QStringList{QStringLiteral("readHeader"), QStringLiteral("deleteTrack:1")}));
        QVERIFY(calls.contains(QStringLiteral("close")));
        QCOMPARE(calls.last(), QStringLiteral("readDisc"));
        QCOMPARE(titles().size(), 3);
    }

    void unknownDevice()
    {
        manager().setDevicesForTesting({});
        manager().select(QStringLiteral("netmd:gone"));
        QCOMPARE(manager().error(), NetMdError::NotFound);
        QCOMPARE(manager().state(), State::Ready);
    }
};

QTEST_GUILESS_MAIN(TestNetMdManager)
#include "tst_netmdmanager.moc"

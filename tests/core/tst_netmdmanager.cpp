// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSignalSpy>
#include <QTest>

#include "fakenetmd.h"
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
        m_manager = std::make_unique<NetMdManager>(NetMdManager::Options{test::fakeNetMd(m_recorder), false, 2000});
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

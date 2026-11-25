// SPDX-License-Identifier: GPL-3.0-or-later
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "discordipc.h"
#include "fakediscord.h"

using namespace mmm;
using mmm::test::FakeDiscord;
using State = DiscordIpc::State;

// The Discord IPC transport against a stand-in Discord in a temp dir (never the real one, M15).
class TestDiscordIpc : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<FakeDiscord> m_discord;
    const QString m_id = QStringLiteral("123456789012345678");

    std::unique_ptr<DiscordIpc> ipc(int handshakeTimeoutMs = 5000)
    {
        DiscordIpc::Options o;
        o.ipcDirs = QStringList{m_dir->path()};
        o.firstRetryMs = 50;
        o.maxRetryMs = 200;
        o.handshakeTimeoutMs = handshakeTimeoutMs;
        auto c = std::make_unique<DiscordIpc>(o);
        c->setClientId(m_id);
        return c;
    }

private Q_SLOTS:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        m_discord = std::make_unique<FakeDiscord>(m_dir->path());
    }
    void cleanup()
    {
        m_discord.reset();
        m_dir.reset();
    }

    void framesAreLittleEndian()
    {
        QCOMPARE(DiscordIpc::frame(1, "{}"), QByteArray("\x01\x00\x00\x00\x02\x00\x00\x00{}", 10));
    }

    void whereItLooks()
    {
        const QByteArray saved = qgetenv("MMM_DISCORD_IPC_DIR");
        const bool wasSet = qEnvironmentVariableIsSet("MMM_DISCORD_IPC_DIR");
        DiscordIpc::Options o;
        DiscordIpc plain(o);
        // MMM_DISCORD_IPC_DIR (ctest: an empty dir) wins over the real places; "" means nowhere.
        qputenv("MMM_DISCORD_IPC_DIR", m_dir->path().toUtf8());
        QCOMPARE(plain.ipcDirs(), QStringList{m_dir->path()});
        qputenv("MMM_DISCORD_IPC_DIR", "");
        QVERIFY(plain.ipcDirs().isEmpty());
        qunsetenv("MMM_DISCORD_IPC_DIR");
        const QStringList dirs = plain.ipcDirs();
        const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
        if (!runtime.isEmpty()) {
            QCOMPARE(dirs.first(), runtime);
            QVERIFY(dirs.contains(runtime + QStringLiteral("/app/com.discordapp.Discord"))); // Flatpak
            QVERIFY(dirs.contains(runtime + QStringLiteral("/snap.discord")));
        }
        QCOMPARE(dirs.last(), QStringLiteral("/tmp"));
        if (wasSet)
            qputenv("MMM_DISCORD_IPC_DIR", saved);
        // Given dirs win over everything.
        o.ipcDirs = QStringList{QStringLiteral("/nowhere")};
        QCOMPARE(DiscordIpc(o).ipcDirs(), QStringList{QStringLiteral("/nowhere")});
    }

    void anEmptyDirFindsNoDiscord()
    {
        // The ctest setting: whatever runs on this machine, it isn't found.
        const QByteArray saved = qgetenv("MMM_DISCORD_IPC_DIR");
        const bool wasSet = qEnvironmentVariableIsSet("MMM_DISCORD_IPC_DIR");
        qputenv("MMM_DISCORD_IPC_DIR", m_dir->path().toUtf8());
        DiscordIpc::Options o;
        o.firstRetryMs = 20;
        DiscordIpc c(o);
        c.setClientId(m_id);
        c.start();
        QTRY_COMPARE(c.state(), State::NotRunning);
        QTest::qWait(150);
        QCOMPARE(c.state(), State::NotRunning);
        if (wasSet)
            qputenv("MMM_DISCORD_IPC_DIR", saved);
        else
            qunsetenv("MMM_DISCORD_IPC_DIR");
    }

    void handshakeThenReady()
    {
        QVERIFY(m_discord->listen());
        auto c = ipc();
        QSignalSpy states(c.get(), &DiscordIpc::stateChanged);
        QCOMPARE(c->state(), State::Stopped);
        QVERIFY(!c->send(QStringLiteral("SET_ACTIVITY"), {})); // not yet
        c->start();
        QTRY_COMPARE(c->state(), State::Ready);
        const FakeDiscord::Received hello = m_discord->received().first();
        QCOMPARE(hello.opcode, quint32(0));
        QCOMPARE(hello.json.value(QStringLiteral("v")).toInt(), 1);
        QCOMPARE(hello.json.value(QStringLiteral("client_id")).toString(), m_id);
        QVERIFY(states.size() >= 2);

        QVERIFY(c->send(QStringLiteral("SET_ACTIVITY"), QJsonObject{{QStringLiteral("pid"), 42}}));
        QTRY_COMPARE(m_discord->received().size(), 2);
        const FakeDiscord::Received cmd = m_discord->received().last();
        QCOMPARE(cmd.opcode, quint32(1));
        QCOMPARE(cmd.json.value(QStringLiteral("cmd")).toString(), QStringLiteral("SET_ACTIVITY"));
        QCOMPARE(cmd.json.value(QStringLiteral("args")).toObject().value(QStringLiteral("pid")).toInt(), 42);
        QVERIFY(!cmd.json.value(QStringLiteral("nonce")).toString().isEmpty());
    }

    void skipsWhatIsntDiscord()
    {
        // discord-ipc-0 is a leftover plain file; Discord is on -1.
        QFile stale(m_dir->filePath(QStringLiteral("discord-ipc-0")));
        QVERIFY(stale.open(QIODevice::WriteOnly));
        stale.close();
        FakeDiscord second(m_dir->path(), 1);
        QVERIFY(second.listen());
        auto c = ipc();
        c->start();
        QTRY_COMPARE(c->state(), State::Ready);
        QCOMPARE(second.connections(), 1);
    }

    void aSocketThatHangsUpOnUsIsSkipped()
    {
        // discord-ipc-0 answers the handshake with an ordinary CLOSE: not Discord (or not now).
        m_discord->closeOnHandshake = 1000;
        QVERIFY(m_discord->listen());
        FakeDiscord second(m_dir->path(), 1);
        QVERIFY(second.listen());
        auto c = ipc();
        c->start();
        QTRY_COMPARE(c->state(), State::Ready);
        QCOMPARE(m_discord->connections(), 1);
        QCOMPARE(second.connections(), 1);
    }

    void framesInPiecesAndTogether()
    {
        m_discord->answerHandshake = false;
        QVERIFY(m_discord->listen());
        auto c = ipc();
        c->start();
        QTRY_COMPARE(m_discord->received().size(), 1); // the handshake
        const QByteArray ready = FakeDiscord::frame(1, "{\"cmd\":\"DISPATCH\",\"evt\":\"READY\"}");
        m_discord->writeRawToAll(ready.left(3));
        QTest::qWait(30);
        m_discord->writeRawToAll(ready.mid(3, 10));
        QTest::qWait(30);
        QCOMPARE(c->state(), State::Connecting);
        m_discord->writeRawToAll(ready.mid(13));
        QTRY_COMPARE(c->state(), State::Ready);

        // Two pings in one write: two pongs, each with its ping's payload.
        m_discord->writeRawToAll(FakeDiscord::frame(3, "{\"n\":1}") + FakeDiscord::frame(3, "{\"n\":2}"));
        QTRY_COMPARE(m_discord->received().size(), 3);
        QCOMPARE(m_discord->received()[1].opcode, quint32(4));
        QCOMPARE(m_discord->received()[1].raw, QByteArray("{\"n\":1}"));
        QCOMPARE(m_discord->received()[2].raw, QByteArray("{\"n\":2}"));
    }

    void anUnknownApplicationIdIsRefused()
    {
        QVERIFY(m_discord->listen());
        auto c = ipc();
        c->setClientId(QStringLiteral("999"));
        c->start();
        QTRY_COMPARE(c->state(), State::Error);
        QCOMPARE(c->lastError(), QStringLiteral("Invalid Client ID"));
        QTest::qWait(200);
        QCOMPARE(m_discord->connections(), 1); // not retried
        c->setClientId(m_id); // a new id: tried again
        QTRY_COMPARE(c->state(), State::Ready);
        QVERIFY(c->lastError().isEmpty());
    }

    void aHungDiscordIsLeftForLater()
    {
        m_discord->answerHandshake = false;
        QVERIFY(m_discord->listen());
        auto c = ipc(100);
        c->start();
        QTRY_VERIFY(m_discord->connections() >= 2); // gave up on READY, came back later
        QVERIFY(c->state() != State::Ready);
        m_discord->answerHandshake = true;
        QTRY_COMPARE(c->state(), State::Ready);
    }

    void waitsForDiscordAndComesBackAfterARestart()
    {
        auto c = ipc();
        c->start();
        QTRY_COMPARE(c->state(), State::NotRunning);
        QVERIFY(m_discord->listen()); // Discord starts
        QTRY_COMPARE(c->state(), State::Ready);
        m_discord->quit(); // ... quits
        QTRY_COMPARE(c->state(), State::NotRunning);
        QVERIFY(m_discord->listen()); // ... and starts again
        QTRY_COMPARE(c->state(), State::Ready);
        QCOMPARE(m_discord->connections(), 2);
    }

    void aHugeFrameDropsTheConnection()
    {
        QVERIFY(m_discord->listen());
        auto c = ipc();
        c->start();
        QTRY_COMPARE(c->state(), State::Ready);
        QByteArray header(8, Qt::Uninitialized);
        qToLittleEndian<quint32>(1, header.data());
        qToLittleEndian<quint32>(50'000'000, header.data() + 4);
        m_discord->writeRawToAll(header);
        QTRY_VERIFY(c->state() != State::Ready);
        QTRY_COMPARE(c->state(), State::Ready); // a fresh connection
        QCOMPARE(m_discord->connections(), 2);
    }

    void aRefusedCommandKeepsTheConnection()
    {
        QVERIFY(m_discord->listen());
        auto c = ipc();
        QSignalSpy errors(c.get(), &DiscordIpc::commandError);
        c->start();
        QTRY_COMPARE(c->state(), State::Ready);
        m_discord->sendToAll(1, "{\"cmd\":\"SET_ACTIVITY\",\"evt\":\"ERROR\",\"data\":{\"code\":4000,\"message\":\"child \\\"activity\\\" fails\"}}");
        QTRY_COMPARE(errors.size(), 1);
        QCOMPARE(errors.first().first().toString(), QStringLiteral("child \"activity\" fails"));
        QCOMPARE(c->state(), State::Ready);
    }

    void sendAndCloseGetsThereThenHangsUp()
    {
        QVERIFY(m_discord->listen());
        auto c = ipc();
        c->start();
        QTRY_COMPARE(c->state(), State::Ready);
        c->sendAndClose(QStringLiteral("SET_ACTIVITY"), QJsonObject{{QStringLiteral("pid"), 7}});
        QCOMPARE(c->state(), State::Stopped);
        QTRY_COMPARE(m_discord->activities().size(), 1);
        QVERIFY(!m_discord->activities().first().contains(QStringLiteral("activity")));
        QTest::qWait(150);
        QCOMPARE(m_discord->connections(), 1); // stopped: no reconnect
    }

    void stopAndStart()
    {
        QVERIFY(m_discord->listen());
        auto c = ipc();
        c->start();
        QTRY_COMPARE(c->state(), State::Ready);
        c->stop();
        QCOMPARE(c->state(), State::Stopped);
        QTest::qWait(150);
        QCOMPARE(m_discord->connections(), 1);
        c->start();
        QTRY_COMPARE(c->state(), State::Ready);
        // No application id: nothing to start.
        DiscordIpc::Options o;
        o.ipcDirs = QStringList{m_dir->path()};
        DiscordIpc none(o);
        none.start();
        QCOMPARE(none.state(), State::Stopped);
    }
};

QTEST_GUILESS_MAIN(TestDiscordIpc)
#include "tst_discordipc.moc"

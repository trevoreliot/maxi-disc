// SPDX-License-Identifier: GPL-3.0-or-later
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "scrobbler.h"
#include "testhttpserver.h"

using namespace mmm;
using mmm::test::TestHttpServer;
using Account = Scrobbler::Account;

namespace {

QJsonObject bodyOf(const TestHttpServer::Request &r)
{
    return QJsonDocument::fromJson(r.body).object();
}
QString listenType(const TestHttpServer::Request &r)
{
    return bodyOf(r).value(QStringLiteral("listen_type")).toString();
}
// The titles a submit-listens request carries, in order.
QStringList titles(const TestHttpServer::Request &r)
{
    QStringList out;
    for (const QJsonValue &v : bodyOf(r).value(QStringLiteral("payload")).toArray())
        out << v.toObject().value(QStringLiteral("track_metadata")).toObject().value(QStringLiteral("track_name")).toString();
    return out;
}
qint64 listenedAt(const TestHttpServer::Request &r, int i = 0)
{
    return bodyOf(r).value(QStringLiteral("payload")).toArray().at(i).toObject().value(QStringLiteral("listened_at")).toInteger();
}

TestHttpServer::Response json(int status, const QByteArray &body, QList<std::pair<QByteArray, QByteArray>> headers = {})
{
    return TestHttpServer::Response{status, body, std::move(headers), 0, -1};
}
TestHttpServer::Response ok()
{
    return json(200, "{\"status\":\"ok\"}");
}

const QString kToken = QString::fromLatin1(Scrobbler::kTokenKey);

} // namespace

// The scrobbler against a loopback ListenBrainz, an in-memory keyring and a fake clock (M14).
// "GOOD" is alice's token; any other is unknown.
class TestScrobbler : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    std::unique_ptr<TestHttpServer> m_server;
    std::shared_ptr<MemorySecretStore> m_secrets;
    std::unique_ptr<Scrobbler> m_s;
    qint64 m_mono = 0;
    qint64 m_utc = 1'700'000'000;
    int m_dbCount = 0;
    QString m_dbPath;
    // What the fake ListenBrainz does with listens (default: takes them) and with "playing now".
    std::function<TestHttpServer::Response(const TestHttpServer::Request &)> m_listens;
    std::function<TestHttpServer::Response(const TestHttpServer::Request &)> m_playingNow;
    int m_validateDelayMs = 0;

    Scrobbler::Options options()
    {
        Scrobbler::Options o;
        o.databasePath = m_dbPath;
        o.secrets = m_secrets;
        o.baseUrl = m_server->url(QStringLiteral("/"));
        o.monotonicMs = [this] { return m_mono; };
        o.utcSeconds = [this] { return m_utc; };
        o.nowPlayingDelayMs = 30;
        o.retryFirstMs = 100;
        o.retryMaxMs = 400;
        o.batchGapMs = 10;
        o.minRequestGapMs = 0;
        o.watchNetwork = false;
        return o;
    }
    // Submitted listens ("single" / "import"), or "playing_now"s.
    QList<TestHttpServer::Request> listens() const
    {
        QList<TestHttpServer::Request> out;
        for (const TestHttpServer::Request &r : m_server->requestsTo(QStringLiteral("/1/submit-listens"))) {
            if (listenType(r) != QLatin1String("playing_now"))
                out << r;
        }
        return out;
    }
    QList<TestHttpServer::Request> playingNows() const
    {
        QList<TestHttpServer::Request> out;
        for (const TestHttpServer::Request &r : m_server->requestsTo(QStringLiteral("/1/submit-listens"))) {
            if (listenType(r) == QLatin1String("playing_now"))
                out << r;
        }
        return out;
    }
    static TrackRow track(const QString &title, qint64 durationMs = 200'000)
    {
        TrackRow t;
        t.id = 1;
        t.title = title;
        t.artist = QStringLiteral("Boards of Canada");
        t.album = QStringLiteral("Geogaddi");
        t.durationMs = durationMs;
        return t;
    }
    // One play listened to the end (due at half: 100 s of a 200 s track).
    void play(const QString &title)
    {
        m_s->playStarted(track(title), true);
        m_mono += 200'000;
        m_utc += 200;
        m_s->playEnded();
    }
    void connect()
    {
        m_s->connectAccount(QStringLiteral("GOOD"));
        QTRY_COMPARE(m_s->account(), Account::Connected);
    }
    // Plays queued while ListenBrainz is down, then it's back and retryNow() sends them.
    void queueWhileDown(const QStringList &plays, std::function<TestHttpServer::Response(const TestHttpServer::Request &)> whenUp)
    {
        m_listens = [](const TestHttpServer::Request &) { return json(503, "down"); };
        for (const QString &t : plays)
            play(t);
        QTRY_VERIFY(m_s->isRetrying());
        m_listens = std::move(whenUp);
    }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    void init()
    {
        m_mono = 0;
        m_utc = 1'700'000'000;
        m_dbPath = m_dir.filePath(QStringLiteral("q%1.db").arg(++m_dbCount));
        m_secrets = std::make_shared<MemorySecretStore>();
        m_server = std::make_unique<TestHttpServer>();
        QVERIFY(m_server->listen());
        m_listens = [](const TestHttpServer::Request &) { return ok(); };
        m_playingNow = [](const TestHttpServer::Request &) { return ok(); };
        m_validateDelayMs = 0;
        m_server->route(QStringLiteral("/1/validate-token"), [this](const TestHttpServer::Request &r) {
            TestHttpServer::Response resp = r.headers.value("authorization") == "Token GOOD"
                ? json(200, "{\"code\":200,\"message\":\"Token valid.\",\"valid\":true,\"user_name\":\"alice\"}")
                : json(200, "{\"code\":200,\"message\":\"Token invalid.\",\"valid\":false}");
            resp.delayMs = m_validateDelayMs;
            return resp;
        });
        m_server->route(QStringLiteral("/1/submit-listens"), [this](const TestHttpServer::Request &r) {
            return listenType(r) == QLatin1String("playing_now") ? m_playingNow(r) : m_listens(r);
        });
        m_s = std::make_unique<Scrobbler>(options());
    }
    void cleanup()
    {
        m_s.reset();
        m_server.reset();
    }

    void connectsAndKeepsTheTokenInTheKeyring()
    {
        QSignalSpy settings(m_s.get(), &Scrobbler::settingsChanged);
        m_s->connectAccount(QStringLiteral(" GOOD "));
        QCOMPARE(m_s->account(), Account::Checking);
        QVERIFY(m_secrets->values.isEmpty()); // nothing saved until it works
        QTRY_COMPARE(m_s->account(), Account::Connected);
        QCOMPARE(m_s->user(), QStringLiteral("alice"));
        QVERIFY(m_s->isEnabled());
        QVERIFY(settings.size() >= 1);
        QTRY_COMPARE(m_secrets->values.value(kToken), QStringLiteral("GOOD"));
        const TestHttpServer::Request check = m_server->requestsTo(QStringLiteral("/1/validate-token")).first();
        QCOMPARE(check.headers.value("authorization"), QByteArray("Token GOOD"));

        // Connected: another token waits for a disconnect.
        m_s->connectAccount(QStringLiteral("OTHER"));
        QCOMPARE(m_s->account(), Account::Connected);
        QCOMPARE(m_server->requestsTo(QStringLiteral("/1/validate-token")).size(), 1);

        // Disconnecting forgets the token.
        m_s->disconnectAccount();
        QCOMPARE(m_s->account(), Account::Disconnected);
        QVERIFY(m_s->user().isEmpty());
        QVERIFY(!m_s->isEnabled());
        QTRY_VERIFY(!m_secrets->values.contains(kToken));
    }

    void anUnknownTokenGoesBack()
    {
        m_s->connectAccount(QString());
        QCOMPARE(m_s->account(), Account::Disconnected);
        QVERIFY(!m_s->lastError().isEmpty());
        m_s->connectAccount(QStringLiteral("two words"));
        QCOMPARE(m_s->account(), Account::Disconnected);
        QVERIFY(m_server->requests().isEmpty()); // neither was worth asking about
        m_s->connectAccount(QStringLiteral("WRONG"));
        QTRY_VERIFY(m_s->lastError().contains(QStringLiteral("doesn't know that token")));
        QCOMPARE(m_s->account(), Account::Disconnected);
        QVERIFY(m_secrets->values.isEmpty());
        connect();
        QVERIFY(m_s->lastError().isEmpty());
    }

    void aCanceledCheckIgnoresTheLateAnswer()
    {
        m_validateDelayMs = 200;
        m_s->connectAccount(QStringLiteral("GOOD"));
        QCOMPARE(m_s->account(), Account::Checking);
        m_s->cancelConnect();
        QCOMPARE(m_s->account(), Account::Disconnected);
        QTRY_COMPARE(m_server->requests().size(), 1);
        QTest::qWait(400); // the answer came and went
        QCOMPARE(m_s->account(), Account::Disconnected);
        QVERIFY(m_s->user().isEmpty());
        QVERIFY(m_secrets->values.isEmpty());
    }

    void restoresFromTheKeyring()
    {
        m_secrets->values.insert(kToken, QStringLiteral("GOOD"));
        m_s->restore(QStringLiteral("alice"), true);
        QTRY_COMPARE(m_s->account(), Account::Connected);
        play(QStringLiteral("Dawn Chorus"));
        QTRY_COMPARE(listens().size(), 1);
        QCOMPARE(listens().first().headers.value("authorization"), QByteArray("Token GOOD"));
        QVERIFY(m_server->requestsTo(QStringLiteral("/1/validate-token")).isEmpty()); // not asked again

        // A user but no token: expired. Nothing at all: disconnected. A locked keyring: said so (and expired).
        m_secrets->values.remove(kToken);
        m_s->restore(QStringLiteral("alice"), true);
        QTRY_COMPARE(m_s->account(), Account::Expired);
        m_s->restore(QString(), false);
        QCOMPARE(m_s->account(), Account::Disconnected);
        m_secrets->failWith = QStringLiteral("The keyring is locked");
        m_s->restore(QStringLiteral("alice"), true);
        QTRY_COMPARE(m_s->keyringError(), QStringLiteral("The keyring is locked"));
        QCOMPARE(m_s->account(), Account::Expired); // a known user: plays wait for a new token
    }

    void playsCountWhileTheKeyringIsRead()
    {
        m_secrets->values.insert(kToken, QStringLiteral("GOOD"));
        m_s->restore(QStringLiteral("alice"), true);
        QCOMPARE(m_s->account(), Account::Disconnected); // the keyring hasn't answered yet
        play(QStringLiteral("Early"));
        QCOMPARE(m_s->queued(), 1);
        QTRY_COMPARE(m_s->account(), Account::Connected);
        QTRY_COMPARE(m_s->queued(), 0);
        // A locked keyring with a known user: plays wait for a new token.
        m_secrets->failWith = QStringLiteral("locked");
        m_s->restore(QStringLiteral("alice"), true);
        QTRY_COMPARE(m_s->account(), Account::Expired);
        play(QStringLiteral("While locked"));
        QCOMPARE(m_s->queued(), 1);
    }

    void playingNowThenTheListen()
    {
        connect();
        // Skipping through: nothing sent for the tracks barely heard.
        m_s->playStarted(track(QStringLiteral("Skipped")), true);
        m_s->playStarted(track(QStringLiteral("Also skipped")), true);
        m_s->playStarted(track(QStringLiteral("Gyroscope")), true);
        QTRY_COMPARE(playingNows().size(), 1);
        QCOMPARE(titles(playingNows().first()), QStringList{QStringLiteral("Gyroscope")});
        const qint64 startedAt = m_utc;
        m_mono += 99'000;
        m_s->playingChanged(false); // paused before it counts
        QCOMPARE(m_s->queued(), 0);
        m_mono += 600'000;
        m_s->playingChanged(true);
        m_mono += 1000;
        m_s->playEnded();
        QTRY_COMPARE(listens().size(), 1);
        const TestHttpServer::Request r = listens().first();
        QCOMPARE(listenType(r), QStringLiteral("single"));
        QCOMPARE(titles(r), QStringList{QStringLiteral("Gyroscope")});
        QCOMPARE(listenedAt(r), startedAt);
        QTRY_COMPARE(m_s->queued(), 0);
        QCOMPARE(m_s->lastListen(), QStringLiteral("Boards of Canada – Gyroscope"));
    }

    void offlinePlaysWaitAndGoWhenBackOnline()
    {
        connect();
        bool online = false;
        m_listens = [&](const TestHttpServer::Request &) { return online ? ok() : json(503, "down"); };
        play(QStringLiteral("One"));
        play(QStringLiteral("Two"));
        QTRY_VERIFY(m_s->isRetrying());
        QCOMPARE(m_s->queued(), 2);
        QVERIFY(!m_s->lastError().isEmpty());
        online = true;
        m_s->networkCameBack();
        QTRY_COMPARE(m_s->queued(), 0);
        const TestHttpServer::Request last = listens().last();
        QCOMPARE(listenType(last), QStringLiteral("import"));
        QCOMPARE(titles(last), QStringList({QStringLiteral("One"), QStringLiteral("Two")})); // in the order played
        QCOMPARE(listenedAt(last, 0), 1'700'000'000);
        QCOMPARE(listenedAt(last, 1), 1'700'000'200);
        QVERIFY(m_s->lastError().isEmpty());

        // And on its own, after a while.
        online = false;
        play(QStringLiteral("Three"));
        QTRY_VERIFY(m_s->isRetrying());
        online = true;
        QTRY_COMPARE(m_s->queued(), 0); // the retry timer (100 ms in this test)
    }

    void theQueueSurvivesARestart()
    {
        connect();
        m_listens = [](const TestHttpServer::Request &) { return json(503, "down"); };
        play(QStringLiteral("Before quitting"));
        m_s->playStarted(track(QStringLiteral("Due as it quits")), true);
        m_mono += 150'000;
        QTRY_VERIFY(m_s->isRetrying());
        m_s.reset(); // quitting: the playing one counts too
        m_listens = [](const TestHttpServer::Request &) { return ok(); };
        m_s = std::make_unique<Scrobbler>(options());
        QCOMPARE(m_s->queued(), 2);
        m_s->restore(QStringLiteral("alice"), true);
        QTRY_COMPARE(m_s->queued(), 0);
    }

    void fiftyAtATime()
    {
        connect();
        QStringList plays;
        for (int i = 0; i < 55; ++i)
            plays << QStringLiteral("Track %1").arg(i);
        queueWhileDown(plays, [](const TestHttpServer::Request &) { return ok(); });
        const qsizetype before = listens().size();
        m_s->retryNow();
        QTRY_COMPARE(m_s->queued(), 0);
        const QList<TestHttpServer::Request> sent = listens().mid(before);
        QCOMPARE(sent.size(), 2);
        QCOMPARE(titles(sent[0]).size(), 50);
        QCOMPARE(titles(sent[1]).size(), 5);
        QCOMPARE(titles(sent[1]).last(), QStringLiteral("Track 54"));
    }

    void aRefusedBatchIsSentOneByOne()
    {
        connect();
        // Any request with "Bad" in it is refused whole.
        queueWhileDown({QStringLiteral("Good 1"), QStringLiteral("Bad"), QStringLiteral("Good 2")},
                       [](const TestHttpServer::Request &r) {
                           return titles(r).contains(QStringLiteral("Bad")) ? json(400, "{\"code\":400,\"error\":\"bad listen\"}") : ok();
                       });
        const qsizetype before = listens().size();
        m_s->retryNow();
        QTRY_COMPARE(m_s->queued(), 0);
        const QList<TestHttpServer::Request> sent = listens().mid(before);
        QCOMPARE(sent.size(), 4); // the batch, then each alone
        QCOMPARE(titles(sent[1]), QStringList{QStringLiteral("Good 1")});
        QCOMPARE(titles(sent[2]), QStringList{QStringLiteral("Bad")});
        QCOMPARE(titles(sent[3]), QStringList{QStringLiteral("Good 2")});
        QVERIFY(m_s->lastError().isEmpty());
    }

    void aServerRefusingEverythingCantEmptyTheQueue()
    {
        auto o = options();
        o.retryFirstMs = 60'000; // the retry is ours to start (retryNow)
        o.retryMaxMs = 60'000;
        m_s = std::make_unique<Scrobbler>(o);
        connect();
        const auto refuse = [](const TestHttpServer::Request &) { return json(400, "{\"code\":400,\"error\":\"nope\"}"); };
        queueWhileDown({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}, refuse);
        m_s->retryNow();
        // The batch, A alone (dropped), B alone (refused too: kept, wait).
        QTRY_VERIFY(m_s->isRetrying());
        QCOMPARE(m_s->queued(), 2);
        QVERIFY(m_s->lastError().contains(QStringLiteral("nope")));
        // Each retry may drop one more, no faster.
        m_s->retryNow();
        QTRY_COMPARE(m_s->queued(), 1);
        QTRY_VERIFY(m_s->isRetrying());
        // Back to normal: the rest goes.
        m_listens = [](const TestHttpServer::Request &) { return ok(); };
        m_s->retryNow();
        QTRY_COMPARE(m_s->queued(), 0);
        QCOMPARE(titles(listens().last()), QStringList{QStringLiteral("C")});
    }

    void anExpiredTokenKeepsThePlays()
    {
        connect();
        m_listens = [](const TestHttpServer::Request &) { return json(401, "{\"code\":401,\"error\":\"Invalid authorization token.\"}"); };
        play(QStringLiteral("One"));
        QTRY_COMPARE(m_s->account(), Account::Expired);
        QTRY_VERIFY(!m_secrets->values.contains(kToken));
        QCOMPARE(m_s->user(), QStringLiteral("alice"));
        play(QStringLiteral("Two")); // still counted while it waits
        QCOMPARE(m_s->queued(), 2);
        m_listens = [](const TestHttpServer::Request &) { return ok(); };
        connect();
        QTRY_COMPARE(m_s->queued(), 0);
    }

    void playsDuringTheNewCheckStillCount()
    {
        connect();
        m_listens = [](const TestHttpServer::Request &) { return json(401, "{}"); };
        play(QStringLiteral("One"));
        QTRY_COMPARE(m_s->account(), Account::Expired);
        m_listens = [](const TestHttpServer::Request &) { return ok(); };
        m_validateDelayMs = 200;
        m_s->connectAccount(QStringLiteral("GOOD"));
        QCOMPARE(m_s->account(), Account::Checking);
        play(QStringLiteral("While checking"));
        QCOMPARE(m_s->queued(), 2);
        QTRY_COMPARE(m_s->account(), Account::Connected);
        QTRY_COMPARE(m_s->queued(), 0);
    }

    void aRefusedTokenOnPlayingNowExpiresIt()
    {
        connect();
        m_playingNow = [](const TestHttpServer::Request &) { return json(401, "{}"); };
        m_s->playStarted(track(QStringLiteral("Now")), true);
        QTRY_COMPARE(m_s->account(), Account::Expired);
        // Anything else going wrong with "playing now" is shrugged off.
        m_playingNow = [](const TestHttpServer::Request &) { return json(500, "oops"); };
        connect();
        m_s->playStarted(track(QStringLiteral("Next")), true);
        QTRY_COMPARE(playingNows().size(), 2);
        QTest::qWait(50);
        QCOMPARE(m_s->account(), Account::Connected);
        QVERIFY(m_s->lastError().isEmpty());
    }

    void oneRequestASecond()
    {
        auto o = options();
        o.minRequestGapMs = 300;
        m_s = std::make_unique<Scrobbler>(o);
        connect();
        m_s->playStarted(track(QStringLiteral("Now")), true); // playing now: ≥ 300 ms after the check
        QTRY_COMPARE(playingNows().size(), 1);
        m_mono += 200'000;
        m_s->playEnded(); // its listen: ≥ 300 ms after that
        QTRY_COMPARE(listens().size(), 1);
        const QList<TestHttpServer::Request> all = m_server->requests();
        QCOMPARE(all.size(), 3);
        QVERIFY2(all[1].atMs - all[0].atMs >= 280, qPrintable(QString::number(all[1].atMs - all[0].atMs)));
        QVERIFY2(all[2].atMs - all[1].atMs >= 280, qPrintable(QString::number(all[2].atMs - all[1].atMs)));

        // The window used up: nothing until it resets.
        m_listens = [](const TestHttpServer::Request &) {
            return json(200, "{\"status\":\"ok\"}", {{"X-RateLimit-Remaining", "0"}, {"X-RateLimit-Reset-In", "1"}});
        };
        play(QStringLiteral("Uses it up"));
        QTRY_COMPARE(listens().size(), 2);
        m_listens = [](const TestHttpServer::Request &) { return ok(); };
        play(QStringLiteral("Waits"));
        QTRY_COMPARE_WITH_TIMEOUT(listens().size(), 3, 3000);
        QVERIFY2(listens()[2].atMs - listens()[1].atMs >= 950,
                 qPrintable(QString::number(listens()[2].atMs - listens()[1].atMs)));
    }

    void switchedOffNothingIsSentOrCounted()
    {
        connect();
        m_s->setEnabled(false);
        m_s->playStarted(track(QStringLiteral("Private")), true);
        QTest::qWait(100);
        QCOMPARE(playingNows().size(), 0);
        m_mono += 200'000;
        m_s->playEnded();
        QCOMPARE(m_s->queued(), 0);
        m_s->setEnabled(true);
        play(QStringLiteral("Public"));
        QTRY_COMPARE(listens().size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestScrobbler)
#include "tst_scrobbler.moc"

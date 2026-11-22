// SPDX-License-Identifier: GPL-3.0-or-later
#include <QElapsedTimer>
#include <QFile>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "httpfetcher.h"
#include "testhttpserver.h"

using mmm::HttpFetcher;
using mmm::HttpResult;
using mmm::test::TestHttpServer;

class TestHttpFetcher : public QObject
{
    Q_OBJECT

private:
    QNetworkAccessManager m_nam;

private Q_SLOTS:
    void getsAndSendsUserAgent()
    {
        TestHttpServer server;
        QVERIFY(server.listen());
        server.route(QStringLiteral("/a"), 200, "hello");
        HttpFetcher f(&m_nam, {});
        f.setUserAgent(QStringLiteral("test-agent/1.0 ( example )"));
        std::optional<HttpResult> got;
        f.get(server.url(QStringLiteral("/a")), this, [&](const HttpResult &r) { got = r; });
        QTRY_VERIFY(got);
        QVERIFY(got->ok());
        QCOMPARE(got->body, QByteArray("hello"));
        QCOMPARE(server.requests().front().headers.value("user-agent"), QByteArray("test-agent/1.0 ( example )"));

        got.reset();
        f.get(server.url(QStringLiteral("/missing")), this, [&](const HttpResult &r) { got = r; });
        QTRY_VERIFY(got);
        QVERIFY(!got->ok());
        QCOMPARE(got->status, 404);
        QVERIFY(got->error.isEmpty());
        QCOMPARE(got->describe(), QStringLiteral("HTTP 404"));
    }

    void requestsAreSpaced()
    {
        TestHttpServer server;
        QVERIFY(server.listen());
        server.route(QStringLiteral("/r"), 200, "ok");
        HttpFetcher f(&m_nam, HttpFetcher::Options{1, 80, 2, 5000, 50});
        int done = 0;
        for (int i = 0; i < 4; ++i)
            f.get(server.url(QStringLiteral("/r")), this, [&](const HttpResult &) { ++done; });
        QTRY_COMPARE(done, 4);
        const auto reqs = server.requests();
        for (qsizetype i = 1; i < reqs.size(); ++i)
            QVERIFY2(reqs[i].atMs - reqs[i - 1].atMs >= 70, qPrintable(QString::number(reqs[i].atMs - reqs[i - 1].atMs)));
    }

    // The interval also counts from the previous answer: a slow request (e.g. one that did the
    // TLS handshake) mustn't let the next one reach the server early.
    void intervalCountsFromTheAnswer()
    {
        TestHttpServer server;
        QVERIFY(server.listen());
        int calls = 0;
        server.route(QStringLiteral("/r"), [&](const TestHttpServer::Request &) {
            return TestHttpServer::Response{200, "ok", {}, ++calls == 1 ? 150 : 0, -1}; // the first answer is slow
        });
        HttpFetcher f(&m_nam, HttpFetcher::Options{1, 100, 2, 5000, 50});
        QList<qint64> answeredAt;
        QElapsedTimer clock;
        clock.start();
        for (int i = 0; i < 2; ++i)
            f.get(server.url(QStringLiteral("/r")), this, [&](const HttpResult &) { answeredAt << clock.elapsed(); });
        QTRY_COMPARE(answeredAt.size(), 2);
        const auto reqs = server.requests();
        // Start-to-start would allow 100 ms; the second waited 100 ms after the slow answer.
        QVERIFY2(reqs[1].atMs - reqs[0].atMs >= 150 + 90, qPrintable(QString::number(reqs[1].atMs - reqs[0].atMs)));
    }

    void concurrencyIsLimited()
    {
        TestHttpServer server;
        QVERIFY(server.listen());
        server.route(QStringLiteral("/slow"), [](const TestHttpServer::Request &) {
            return TestHttpServer::Response{200, "slow", {}, 150};
        });
        HttpFetcher f(&m_nam, HttpFetcher::Options{2, 0, 2, 5000, 50});
        int done = 0;
        for (int i = 0; i < 3; ++i)
            f.get(server.url(QStringLiteral("/slow")), this, [&](const HttpResult &) { ++done; });
        QTRY_COMPARE(f.runningCount(), 2);
        QCOMPARE(f.queuedCount(), 1);
        QTRY_COMPARE(done, 3);
        const auto reqs = server.requests();
        QVERIFY(reqs[2].atMs - reqs[0].atMs >= 120); // the third waited for a free slot
    }

    void retriesBusyAnswers()
    {
        TestHttpServer server;
        QVERIFY(server.listen());
        int calls = 0;
        server.route(QStringLiteral("/busy"), [&](const TestHttpServer::Request &) {
            return ++calls < 3 ? TestHttpServer::Response{503, "busy", {{"Retry-After", "0"}}, 0}
                               : TestHttpServer::Response{200, "finally", {}, 0};
        });
        HttpFetcher::Options o{1, 0, 2, 5000, 30};
        o.backoffStepMs = 10; // keep the test quick
        HttpFetcher f(&m_nam, o);
        std::optional<HttpResult> got;
        f.get(server.url(QStringLiteral("/busy")), this, [&](const HttpResult &r) { got = r; });
        QTRY_VERIFY(got);
        QCOMPARE(calls, 3);
        QVERIFY(got->ok());
        QCOMPARE(got->body, QByteArray("finally"));

        // Gives up after maxRetries.
        server.route(QStringLiteral("/always"), 429, "slow down");
        got.reset();
        f.get(server.url(QStringLiteral("/always")), this, [&](const HttpResult &r) { got = r; });
        QTRY_VERIFY(got);
        QCOMPARE(got->status, 429);
        QCOMPARE(server.requestsTo(QStringLiteral("/always")).size(), 3);
        QVERIFY(got->describe().contains(QStringLiteral("busy")));
    }

    // "Slow down" answers widen the spacing; successes narrow it again.
    void backsOffAdaptively()
    {
        TestHttpServer server;
        QVERIFY(server.listen());
        int calls = 0;
        server.route(QStringLiteral("/r"), [&](const TestHttpServer::Request &) {
            return ++calls == 1 ? TestHttpServer::Response{503, "busy", {{"Retry-After", "0"}}, 0, -1}
                                : TestHttpServer::Response{200, "ok", {}, 0, -1};
        });
        HttpFetcher::Options o{1, 20, 2, 5000, 10};
        o.backoffStepMs = 200;
        o.maxBackoffMs = 400;
        HttpFetcher f(&m_nam, o);
        int done = 0;
        for (int i = 0; i < 3; ++i)
            f.get(server.url(QStringLiteral("/r")), this, [&](const HttpResult &r) { done += r.ok() ? 1 : 0; });
        QTRY_COMPARE_WITH_TIMEOUT(done, 3, 5000);
        const auto reqs = server.requests();
        QCOMPARE(reqs.size(), 4); // one retry
        // After the 503, requests were spaced by 20 + up to 200 ms instead of 20 ms.
        for (qsizetype i = 2; i < reqs.size(); ++i)
            QVERIFY2(reqs[i].atMs - reqs[i - 1].atMs >= 140, qPrintable(QString::number(reqs[i].atMs - reqs[i - 1].atMs)));
        QVERIFY(f.backoffMs() > 0 && f.backoffMs() < 200); // three successes took some off
    }

    void cancelledRequestsNeverCallBack()
    {
        TestHttpServer server;
        QVERIFY(server.listen());
        server.route(QStringLiteral("/slow"), [](const TestHttpServer::Request &) {
            return TestHttpServer::Response{200, "slow", {}, 100};
        });
        HttpFetcher f(&m_nam, HttpFetcher::Options{1, 50, 2, 5000, 50});
        int called = 0;
        for (int i = 0; i < 3; ++i)
            f.get(server.url(QStringLiteral("/slow")), this, [&](const HttpResult &) { ++called; });
        QTRY_COMPARE(server.requests().size(), 1);
        f.cancelAll();
        QCOMPARE(f.queuedCount(), 0);
        QTest::qWait(300);
        QCOMPARE(called, 0);
        QCOMPARE(server.requests().size(), 1);

        // A destroyed context drops its callback too.
        auto context = std::make_unique<QObject>();
        f.get(server.url(QStringLiteral("/slow")), context.get(), [&](const HttpResult &) { ++called; });
        context.reset();
        QTest::qWait(300);
        QCOMPARE(called, 0);

        // ... and the fetcher still works afterwards.
        f.get(server.url(QStringLiteral("/slow")), this, [&](const HttpResult &) { ++called; });
        QTRY_COMPARE(called, 1);
    }

    // 200 + Content-Length, then the connection drops: an error, never a short body.
    void truncatedBodyIsAnError()
    {
        TestHttpServer server;
        QVERIFY(server.listen());
        server.route(QStringLiteral("/cut"), [](const TestHttpServer::Request &) {
            TestHttpServer::Response r;
            r.body = QByteArray(4000, 'x');
            r.sendBytes = 1000;
            return r;
        });
        HttpFetcher f(&m_nam, {});
        std::optional<HttpResult> got;
        f.get(server.url(QStringLiteral("/cut")), this, [&](const HttpResult &r) { got = r; });
        QTRY_VERIFY(got);
        QVERIFY(!got->ok());
        QCOMPARE(got->status, 0);
        QVERIFY(!got->error.isEmpty());
        QVERIFY(got->body.isEmpty());
    }

    void fileUrlsAndTransportErrors()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("x.bin"));
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("local bytes");
        }
        HttpFetcher f(&m_nam, {});
        std::optional<HttpResult> got;
        f.get(QUrl::fromLocalFile(path), this, [&](const HttpResult &r) { got = r; });
        QTRY_VERIFY(got);
        QVERIFY(got->ok());
        QCOMPARE(got->body, QByteArray("local bytes"));

        // Nothing listens on a port we just closed.
        quint16 port = 0;
        {
            TestHttpServer server;
            QVERIFY(server.listen());
            port = static_cast<quint16>(server.url().port());
        }
        got.reset();
        f.get(QUrl(QStringLiteral("http://127.0.0.1:%1/x").arg(port)), this, [&](const HttpResult &r) { got = r; });
        QTRY_VERIFY(got);
        QVERIFY(!got->ok());
        QCOMPARE(got->status, 0);
        QVERIFY(!got->error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestHttpFetcher)
#include "tst_httpfetcher.moc"

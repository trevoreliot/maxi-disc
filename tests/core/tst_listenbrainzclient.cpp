// SPDX-License-Identifier: GPL-3.0-or-later
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTest>

#include "appinfo.h"
#include "listenbrainzclient.h"
#include "testhttpserver.h"

using namespace mmm;
using mmm::test::TestHttpServer;

namespace {

ScrobbleTrack song()
{
    return ScrobbleTrack{QStringLiteral("Sigur Rós"), QStringLiteral("Svefn-g-englar"), QStringLiteral("Ágætis byrjun"),
                         QStringLiteral("Sigur Rós"), 2, 601'000};
}

PendingScrobble listen(const QString &title, qint64 at)
{
    ScrobbleTrack t = song();
    t.title = title;
    return PendingScrobble{1, t, at, 0};
}

QJsonObject bodyOf(const TestHttpServer::Request &r)
{
    return QJsonDocument::fromJson(r.body).object();
}

TestHttpServer::Response answer(int status, const QByteArray &body, QList<std::pair<QByteArray, QByteArray>> headers = {})
{
    return TestHttpServer::Response{status, body, std::move(headers), 0, -1};
}

} // namespace

// ListenBrainz calls against a loopback server (never the internet).
class TestListenBrainzClient : public QObject
{
    Q_OBJECT

    QNetworkAccessManager m_nam;
    std::unique_ptr<TestHttpServer> m_server;
    std::unique_ptr<ListenBrainzClient> m_client;
    std::function<TestHttpServer::Response(const TestHttpServer::Request &)> m_answer;

    // Runs `call` and waits for its answer.
    ListenBrainzResult submitAndWait(const QList<PendingScrobble> &batch)
    {
        bool called = false;
        ListenBrainzResult got;
        m_client->submit(batch, this, [&](const ListenBrainzResult &r) {
            called = true;
            got = r;
        });
        [&] { QTRY_VERIFY(called); }();
        return got;
    }
    std::pair<QString, ListenBrainzResult> validateAndWait(const QString &token)
    {
        bool called = false;
        std::pair<QString, ListenBrainzResult> got;
        m_client->validateToken(token, this, [&](const QString &user, const ListenBrainzResult &r) {
            called = true;
            got = {user, r};
        });
        [&] { QTRY_VERIFY(called); }();
        return got;
    }

private Q_SLOTS:
    void init()
    {
        m_server = std::make_unique<TestHttpServer>();
        QVERIFY(m_server->listen());
        m_answer = [](const TestHttpServer::Request &) { return answer(200, "{\"status\":\"ok\"}"); };
        m_server->route(QStringLiteral("/1/submit-listens"), [this](const TestHttpServer::Request &r) { return m_answer(r); });
        m_server->route(QStringLiteral("/1/validate-token"), [](const TestHttpServer::Request &r) {
            if (r.headers.value("authorization") == "Token abc-123")
                return answer(200, "{\"code\":200,\"message\":\"Token valid.\",\"valid\":true,\"user_name\":\"alice\"}");
            return answer(200, "{\"code\":200,\"message\":\"Token invalid.\",\"valid\":false}");
        });
        m_client = std::make_unique<ListenBrainzClient>(&m_nam);
        m_client->setBaseUrl(m_server->url(QStringLiteral("/")));
        m_client->setToken(QStringLiteral("abc-123"));
    }
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void baseUrls()
    {
        // ctest points it at a closed port; without that it's ListenBrainz's.
        const QByteArray saved = qgetenv("MMM_LISTENBRAINZ_URL");
        qputenv("MMM_LISTENBRAINZ_URL", "http://127.0.0.1:9/");
        QCOMPARE(ListenBrainzClient::defaultBaseUrl(), QUrl(QStringLiteral("http://127.0.0.1:9/")));
        qunsetenv("MMM_LISTENBRAINZ_URL");
        QCOMPARE(ListenBrainzClient::defaultBaseUrl(), QUrl(QStringLiteral("https://api.listenbrainz.org/")));
        if (!saved.isEmpty())
            qputenv("MMM_LISTENBRAINZ_URL", saved);
        QCOMPARE(ListenBrainzClient::tokenPageUrl(), QUrl(QStringLiteral("https://listenbrainz.org/settings/")));

        // Endpoints go under the base's path, with or without its last "/".
        ListenBrainzClient c(&m_nam);
        c.setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:9")));
        QCOMPARE(c.baseUrl(), QUrl(QStringLiteral("http://127.0.0.1:9/")));
        c.setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:9/lb")));
        QCOMPARE(c.baseUrl(), QUrl(QStringLiteral("http://127.0.0.1:9/lb/")));
    }

    void validatesAToken()
    {
        const auto [user, r] = validateAndWait(QStringLiteral("  abc-123\n"));
        QVERIFY(r.ok());
        QCOMPARE(user, QStringLiteral("alice"));
        QCOMPARE(m_server->requests().size(), 1);
        const TestHttpServer::Request req = m_server->requests().first();
        QCOMPARE(req.method, QByteArray("GET"));
        QCOMPARE(req.path, QStringLiteral("/1/validate-token"));
        QVERIFY(req.query.isEmpty()); // never in the URL
        QCOMPARE(req.headers.value("authorization"), QByteArray("Token abc-123"));
        QCOMPARE(req.headers.value("user-agent"), AppInfo::userAgent().toUtf8());
    }

    void anUnknownTokenIsUnauthorized()
    {
        // ListenBrainz says so with HTTP 200 and "valid": false.
        const auto [user, r] = validateAndWait(QStringLiteral("nope"));
        QCOMPARE(r.kind, ListenBrainzResult::Unauthorized);
        QVERIFY(user.isEmpty());
        QVERIFY(!r.retryable());
        QVERIFY(!r.describe().isEmpty());
    }

    void tokensThatCantBeSentArentSent()
    {
        QVERIFY(ListenBrainzClient::plausibleToken(QStringLiteral("0123abcd-ef45-6789-abcd-ef0123456789")));
        QVERIFY(!ListenBrainzClient::plausibleToken(QString()));
        QVERIFY(!ListenBrainzClient::plausibleToken(QStringLiteral("two words")));
        QVERIFY(!ListenBrainzClient::plausibleToken(QStringLiteral("a\r\nX-Evil: 1")));
        QVERIFY(!ListenBrainzClient::plausibleToken(QStringLiteral("tökén")));
        QCOMPARE(validateAndWait(QStringLiteral("a b")).second.kind, ListenBrainzResult::Unauthorized);
        m_client->setToken({});
        QCOMPARE(submitAndWait({listen(QStringLiteral("One"), 1'700'000'000)}).kind, ListenBrainzResult::Unauthorized);
        QVERIFY(m_server->requests().isEmpty());
    }

    void playingNow()
    {
        bool called = false;
        m_client->playingNow(song(), this, [&](const ListenBrainzResult &r) {
            QVERIFY(r.ok());
            called = true;
        });
        QTRY_VERIFY(called);
        const TestHttpServer::Request req = m_server->requests().first();
        QCOMPARE(req.method, QByteArray("POST"));
        QCOMPARE(req.path, QStringLiteral("/1/submit-listens"));
        QCOMPARE(req.headers.value("authorization"), QByteArray("Token abc-123"));
        QVERIFY(req.headers.value("content-type").startsWith("application/json"));
        const QJsonObject body = bodyOf(req);
        QCOMPARE(body.value(QStringLiteral("listen_type")).toString(), QStringLiteral("playing_now"));
        const QJsonArray payload = body.value(QStringLiteral("payload")).toArray();
        QCOMPARE(payload.size(), 1);
        const QJsonObject listen = payload.first().toObject();
        QVERIFY(!listen.contains(QStringLiteral("listened_at"))); // never for playing_now
        const QJsonObject meta = listen.value(QStringLiteral("track_metadata")).toObject();
        QCOMPARE(meta.value(QStringLiteral("artist_name")).toString(), QStringLiteral("Sigur Rós"));
        QCOMPARE(meta.value(QStringLiteral("track_name")).toString(), QStringLiteral("Svefn-g-englar"));
        QCOMPARE(meta.value(QStringLiteral("release_name")).toString(), QStringLiteral("Ágætis byrjun"));
        const QJsonObject info = meta.value(QStringLiteral("additional_info")).toObject();
        QCOMPARE(info.value(QStringLiteral("duration_ms")).toInteger(), 601'000);
        QCOMPARE(info.value(QStringLiteral("tracknumber")).toInt(), 2);
        QCOMPARE(info.value(QStringLiteral("submission_client")).toString(), AppInfo::displayName());
        QCOMPARE(info.value(QStringLiteral("submission_client_version")).toString(), AppInfo::version());
        QCOMPARE(info.value(QStringLiteral("media_player")).toString(), AppInfo::displayName());
    }

    void unknownTagsAreLeftOut()
    {
        ScrobbleTrack t{QStringLiteral("Artist"), QStringLiteral("Title"), QString(), QString(), 0, 0};
        const QJsonObject body = QJsonDocument::fromJson(ListenBrainzClient::playingNowBody(t)).object();
        const QJsonObject meta = body.value(QStringLiteral("payload")).toArray().first().toObject()
                                     .value(QStringLiteral("track_metadata")).toObject();
        QVERIFY(!meta.contains(QStringLiteral("release_name")));
        const QJsonObject info = meta.value(QStringLiteral("additional_info")).toObject();
        QVERIFY(!info.contains(QStringLiteral("duration_ms")));
        QVERIFY(!info.contains(QStringLiteral("tracknumber")));
        // The album artist isn't part of ListenBrainz's format.
        const QByteArray withAlbumArtist = ListenBrainzClient::playingNowBody(song());
        QVERIFY(!withAlbumArtist.contains("album_artist"));
        QVERIFY(!withAlbumArtist.contains("albumArtist"));
    }

    void oneListenIsSingleSeveralAreAnImport()
    {
        QVERIFY(submitAndWait({listen(QStringLiteral("One"), 1'700'000'000)}).ok());
        QJsonObject body = bodyOf(m_server->requests().last());
        QCOMPARE(body.value(QStringLiteral("listen_type")).toString(), QStringLiteral("single"));
        QCOMPARE(body.value(QStringLiteral("payload")).toArray().first().toObject().value(QStringLiteral("listened_at")).toInteger(),
                 1'700'000'000);

        QVERIFY(submitAndWait({listen(QStringLiteral("A"), 1'700'000'100), listen(QStringLiteral("B"), 1'700'000'400),
                               listen(QStringLiteral("C"), 1'700'000'900)})
                    .ok());
        body = bodyOf(m_server->requests().last());
        QCOMPARE(body.value(QStringLiteral("listen_type")).toString(), QStringLiteral("import"));
        const QJsonArray payload = body.value(QStringLiteral("payload")).toArray();
        QCOMPARE(payload.size(), 3);
        QStringList titles;
        for (const QJsonValue &v : payload)
            titles << v.toObject().value(QStringLiteral("track_metadata")).toObject().value(QStringLiteral("track_name")).toString();
        QCOMPARE(titles, QStringList({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}));
        QCOMPARE(payload[2].toObject().value(QStringLiteral("listened_at")).toInteger(), 1'700'000'900);
    }

    void readsTheRateLimit()
    {
        m_answer = [](const TestHttpServer::Request &) {
            return answer(200, "{\"status\":\"ok\"}", {{"X-RateLimit-Limit", "30"}, {"X-RateLimit-Remaining", "0"}, {"X-RateLimit-Reset-In", "7"}});
        };
        const ListenBrainzResult r = submitAndWait({listen(QStringLiteral("One"), 1'700'000'000)});
        QVERIFY(r.ok());
        QCOMPARE(r.remaining, 0);
        QCOMPARE(r.resetInS, 7);
        m_answer = [](const TestHttpServer::Request &) { return answer(200, "{\"status\":\"ok\"}"); };
        const ListenBrainzResult none = submitAndWait({listen(QStringLiteral("One"), 1'700'000'000)});
        QCOMPARE(none.remaining, -1); // not said
        QCOMPARE(none.resetInS, 0);
    }

    void errors_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<QByteArray>("header");
        QTest::addColumn<int>("kind");
        QTest::addColumn<QString>("message");
        QTest::addColumn<int>("resetInS");
        QTest::newRow("401") << 401 << QByteArray("{\"code\":401,\"error\":\"Invalid authorization token.\"}") << QByteArray()
                             << int(ListenBrainzResult::Unauthorized) << QStringLiteral("Invalid authorization token.") << 0;
        QTest::newRow("400") << 400 << QByteArray("{\"code\":400,\"error\":\"Value for key listened_at is too low.\"}")
                             << QByteArray() << int(ListenBrainzResult::Invalid)
                             << QStringLiteral("Value for key listened_at is too low.") << 0;
        QTest::newRow("429, reset in") << 429 << QByteArray("{\"code\":429,\"error\":\"Too many requests\"}")
                                       << QByteArray("X-RateLimit-Reset-In") << int(ListenBrainzResult::Temporary)
                                       << QStringLiteral("Too many requests") << 12;
        QTest::newRow("429, retry after") << 429 << QByteArray() << QByteArray("Retry-After")
                                          << int(ListenBrainzResult::Temporary) << QString() << 12;
        QTest::newRow("503, an HTML page") << 503 << QByteArray("<html>down</html>") << QByteArray()
                                           << int(ListenBrainzResult::Temporary) << QString() << 0;
        QTest::newRow("413: too big, like a refused one")
            << 413 << QByteArray() << QByteArray() << int(ListenBrainzResult::Invalid) << QString() << 0;
        QTest::newRow("418") << 418 << QByteArray("{\"error\":\"teapot\"}") << QByteArray() << int(ListenBrainzResult::Other)
                             << QStringLiteral("teapot") << 0;
        QTest::newRow("200, not JSON") << 200 << QByteArray("ok!") << QByteArray() << int(ListenBrainzResult::Other)
                                       << QStringLiteral("its answer couldn't be read") << 0;
    }
    void errors()
    {
        QFETCH(int, status);
        QFETCH(QByteArray, body);
        QFETCH(QByteArray, header);
        QFETCH(int, kind);
        QFETCH(QString, message);
        QFETCH(int, resetInS);
        m_answer = [&](const TestHttpServer::Request &) {
            QList<std::pair<QByteArray, QByteArray>> headers;
            if (!header.isEmpty())
                headers << std::pair{header, QByteArray("12")};
            return answer(status, body, headers);
        };
        const ListenBrainzResult r = submitAndWait({listen(QStringLiteral("One"), 1'700'000'000)});
        QCOMPARE(int(r.kind), kind);
        QCOMPARE(r.httpStatus, status);
        QCOMPARE(r.message, message);
        QCOMPARE(r.resetInS, resetInS);
        QVERIFY(!r.describe().isEmpty());
        // Only the token and the listen itself are worth not retrying.
        QCOMPARE(r.retryable(), kind != ListenBrainzResult::Unauthorized && kind != ListenBrainzResult::Invalid);
    }

    void theTokenNeverFollowsARedirect()
    {
        m_server->route(QStringLiteral("/elsewhere"), 200, "{\"status\":\"ok\"}");
        m_answer = [this](const TestHttpServer::Request &) {
            return answer(302, {}, {{"Location", m_server->url(QStringLiteral("/elsewhere")).toEncoded()}});
        };
        const ListenBrainzResult r = submitAndWait({listen(QStringLiteral("One"), 1'700'000'000)});
        QCOMPARE(r.kind, ListenBrainzResult::Other);
        QCOMPARE(r.httpStatus, 302);
        QTest::qWait(100);
        QVERIFY(m_server->requestsTo(QStringLiteral("/elsewhere")).isEmpty());
    }

    void offlineIsANetworkError()
    {
        m_client->setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:9/")));
        const ListenBrainzResult r = submitAndWait({listen(QStringLiteral("One"), 1'700'000'000)});
        QCOMPARE(r.kind, ListenBrainzResult::Network);
        QVERIFY(r.retryable());
    }

    void aGoneContextHearsNothing()
    {
        m_answer = [](const TestHttpServer::Request &) {
            return TestHttpServer::Response{200, "{\"status\":\"ok\"}", {}, 100, -1};
        };
        auto context = std::make_unique<QObject>();
        bool called = false;
        m_client->submit({listen(QStringLiteral("One"), 1'700'000'000)}, context.get(),
                         [&](const ListenBrainzResult &) { called = true; });
        QTRY_COMPARE(m_server->requests().size(), 1);
        context.reset();
        QTest::qWait(300);
        QVERIFY(!called);
    }
};

QTEST_GUILESS_MAIN(TestListenBrainzClient)
#include "tst_listenbrainzclient.moc"

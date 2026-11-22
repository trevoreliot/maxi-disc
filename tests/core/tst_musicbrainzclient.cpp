// SPDX-License-Identifier: GPL-3.0-or-later
#include <QNetworkAccessManager>
#include <QTest>

#include "musicbrainzclient.h"
#include "testhttpserver.h"

using mmm::MbRelease;
using mmm::MusicBrainzClient;
using mmm::test::TestHttpServer;

namespace {

// Trimmed from a real ws/2 search answer.
const QByteArray kSearchJson = R"({
  "created": "2026-11-21T00:00:00.000Z", "count": 2, "offset": 0,
  "releases": [
    {"id": "b84ee12a-09ef-421b-82de-0441a926375b", "score": 100, "title": "Kid A",
     "status": "Official", "date": "2000-10-02", "country": "GB", "track-count": 10,
     "artist-credit": [{"name": "Radiohead", "artist": {"id": "a74b1b7f", "name": "Radiohead"}}],
     "release-group": {"id": "b8048f24-c026-3398-b23a-b5e50716cbc7", "primary-type": "Album"},
     "media": [{"format": "CD", "disc-count": 1, "track-count": 10}]},
    {"id": "0f3a5d36-27c6-4a8b-a9e0-6b4c3d0c3c11", "score": 87, "title": "Kid A Mnesia",
     "status": "Official", "date": "2021", "track-count": 34,
     "artist-credit": [{"name": "Radiohead", "joinphrase": " & "}, {"name": "Friends"}],
     "release-group": {"id": "aaaaaaaa-c026-3398-b23a-b5e50716cbc7"},
     "media": [{"format": "CD", "track-count": 10}, {"format": "CD", "track-count": 11},
               {"format": "CD", "track-count": 13}]},
    {"title": "no id: skipped"}
  ]})";

const QByteArray kReleaseJson = R"({
  "id": "b84ee12a-09ef-421b-82de-0441a926375b", "title": "Kid A", "status": "Official",
  "date": "2000-10-02", "country": "XE",
  "artist-credit": [{"name": "Radiohead", "joinphrase": ""}],
  "release-group": {"id": "b8048f24-c026-3398-b23a-b5e50716cbc7"},
  "media": [{"format": "CD", "track-count": 10}, {"format": "DVD-Video", "track-count": 2}]})";

} // namespace

class TestMusicBrainzClient : public QObject
{
    Q_OBJECT

private:
    QNetworkAccessManager m_nam;

private Q_SLOTS:
    void searchQuery()
    {
        QCOMPARE(MusicBrainzClient::searchQuery(QStringLiteral("Kid A"), QStringLiteral("Radiohead"), false),
                 QStringLiteral("release:\"Kid A\" AND artist:\"Radiohead\""));
        // Quotes and backslashes are escaped inside the phrase.
        QCOMPARE(MusicBrainzClient::searchQuery(QStringLiteral("Say \"Hi\" \\o/"), QString(), false),
                 QStringLiteral("release:\"Say \\\"Hi\\\" \\\\o/\""));
        // Relaxed: edition dropped, operators and punctuation removed.
        QCOMPARE(MusicBrainzClient::searchQuery(QStringLiteral("OK Computer (Collector's Edition)"),
                                                QStringLiteral("Radiohead"), true),
                 QStringLiteral("release:(OK Computer) AND artist:(Radiohead)"));
        QCOMPARE(MusicBrainzClient::searchQuery(QStringLiteral("Rock AND Roll: Part 1!"), QStringLiteral("AC/DC"), true),
                 QStringLiteral("release:(Rock Roll Part 1) AND artist:(AC DC)"));
    }

    void parsing()
    {
        QString error;
        const QList<MbRelease> releases = MusicBrainzClient::parseSearch(kSearchJson, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(releases.size(), 2);
        const MbRelease &a = releases[0];
        QCOMPARE(a.id, QStringLiteral("b84ee12a-09ef-421b-82de-0441a926375b"));
        QCOMPARE(a.releaseGroupId, QStringLiteral("b8048f24-c026-3398-b23a-b5e50716cbc7"));
        QCOMPARE(a.title, QStringLiteral("Kid A"));
        QCOMPARE(a.artist, QStringLiteral("Radiohead"));
        QCOMPARE(a.year, 2000);
        QCOMPARE(a.country, QStringLiteral("GB"));
        QCOMPARE(a.status, QStringLiteral("Official"));
        QCOMPARE(a.format, QStringLiteral("CD"));
        QCOMPARE(a.trackCount, 10);
        QCOMPARE(a.searchScore, 100);
        QCOMPARE(releases[1].artist, QStringLiteral("Radiohead & Friends"));
        QCOMPARE(releases[1].format, QStringLiteral("3×CD"));
        QCOMPARE(releases[1].trackCount, 34);

        const auto r = MusicBrainzClient::parseRelease(kReleaseJson, &error);
        QVERIFY(r);
        QCOMPARE(r->trackCount, 12); // summed over media
        QCOMPARE(r->format, QStringLiteral("CD + DVD-Video"));
        QCOMPARE(r->searchScore, 0);

        QVERIFY(MusicBrainzClient::parseSearch("<html>", &error).isEmpty());
        QVERIFY(error.contains(QStringLiteral("unexpected answer")));
        QVERIFY(!MusicBrainzClient::parseRelease("{}", &error));
    }

    void isMbid()
    {
        QVERIFY(MusicBrainzClient::isMbid(QStringLiteral("b84ee12a-09ef-421b-82de-0441a926375b")));
        QVERIFY(MusicBrainzClient::isMbid(QStringLiteral(" B84EE12A-09EF-421B-82DE-0441A926375B ")));
        QVERIFY(!MusicBrainzClient::isMbid(QStringLiteral("b84ee12a")));
        QVERIFY(!MusicBrainzClient::isMbid(QString()));
        QVERIFY(!MusicBrainzClient::isMbid(QStringLiteral("../../etc/passwd")));
    }

    void searchOverHttp()
    {
        TestHttpServer server;
        QVERIFY(server.listen());
        server.route(QStringLiteral("/ws/2/release/"), 200, kSearchJson);
        MusicBrainzClient mb(&m_nam);
        mb.setBaseUrl(server.url());
        mb.fetcher().setOptions(HttpFetcherOptionsFast());

        QList<MbRelease> got;
        QString error = QStringLiteral("unset");
        mb.searchReleases(QStringLiteral("Kid A"), QStringLiteral("Radiohead"), false, this,
                          [&](const QList<MbRelease> &r, const QString &e) { got = r; error = e; });
        QTRY_VERIFY(error != QLatin1String("unset"));
        QVERIFY(error.isEmpty());
        QCOMPARE(got.size(), 2);
        const auto req = server.requests().front();
        QCOMPARE(req.query.queryItemValue(QStringLiteral("query"), QUrl::FullyDecoded),
                 QStringLiteral("release:\"Kid A\" AND artist:\"Radiohead\""));
        QCOMPARE(req.query.queryItemValue(QStringLiteral("fmt")), QStringLiteral("json"));
        QVERIFY(req.headers.value("user-agent").startsWith("music-md-manager/"));
    }

    void lookupOverHttp()
    {
        TestHttpServer server;
        QVERIFY(server.listen());
        const QString id = QStringLiteral("b84ee12a-09ef-421b-82de-0441a926375b");
        server.route(QStringLiteral("/ws/2/release/") + id, 200, kReleaseJson);
        MusicBrainzClient mb(&m_nam);
        mb.setBaseUrl(server.url());
        mb.fetcher().setOptions(HttpFetcherOptionsFast());

        QList<MbRelease> got;
        QString error = QStringLiteral("unset");
        auto done = [&](const QList<MbRelease> &r, const QString &e) { got = r; error = e; };
        mb.lookupRelease(id.toUpper(), this, done);
        QTRY_VERIFY(error != QLatin1String("unset"));
        QVERIFY(error.isEmpty());
        QCOMPARE(got.size(), 1);
        QCOMPARE(got.front().title, QStringLiteral("Kid A"));
        QVERIFY(server.requests().front().query.queryItemValue(QStringLiteral("inc"), QUrl::FullyDecoded)
                    .contains(QStringLiteral("release-groups")));

        // Unknown id: empty, no error. Server error: an error. Not an MBID: refused locally.
        for (const auto &[mbid, wantError] : {std::pair{QStringLiteral("00000000-0000-0000-0000-000000000000"), false},
                                              std::pair{QStringLiteral("11111111-1111-1111-1111-111111111111"), true},
                                              std::pair{QStringLiteral("not-an-id"), true}}) {
            server.route(QStringLiteral("/ws/2/release/11111111-1111-1111-1111-111111111111"), 500, "oops");
            error = QStringLiteral("unset");
            got = {MbRelease{}};
            mb.lookupRelease(mbid, this, done);
            QTRY_VERIFY(error != QLatin1String("unset"));
            QVERIFY(got.isEmpty());
            QCOMPARE(!error.isEmpty(), wantError);
        }
    }

private:
    static mmm::HttpFetcher::Options HttpFetcherOptionsFast() { return {1, 0, 0, 5000, 10}; }
};

QTEST_GUILESS_MAIN(TestMusicBrainzClient)
#include "tst_musicbrainzclient.moc"

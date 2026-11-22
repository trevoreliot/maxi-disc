// SPDX-License-Identifier: GPL-3.0-or-later
#include <QNetworkAccessManager>
#include <QTest>

#include "coverartarchiveclient.h"
#include "testhttpserver.h"

using mmm::CaaImage;
using mmm::CoverArtArchiveClient;
using mmm::test::TestHttpServer;

namespace {

// Trimmed from a real listing (they still use http:// URLs).
const QByteArray kListing = "{\n"
    "  \"images\": [\n"
    "    {\"approved\": true, \"back\": true, \"front\": false, \"id\": 111, \"types\": [\"Back\"],\n"
    "     \"image\": \"http://coverartarchive.org/release/r/111.jpg\",\n"
    "     \"thumbnails\": {\"250\": \"http://coverartarchive.org/release/r/111-250.jpg\"}},\n"
    "    {\"approved\": true, \"back\": false, \"front\": true, \"id\": 829521842, \"types\": [\"Front\"],\n"
    "     \"image\": \"http://coverartarchive.org/release/r/829521842.png\",\n"
    "     \"thumbnails\": {\"250\": \"http://coverartarchive.org/release/r/829521842-250.jpg\",\n"
    "                    \"500\": \"http://coverartarchive.org/release/r/829521842-500.jpg\",\n"
    "                    \"1200\": \"http://coverartarchive.org/release/r/829521842-1200.jpg\",\n"
    "                    \"large\": \"http://coverartarchive.org/release/r/829521842-500.jpg\",\n"
    "                    \"small\": \"http://coverartarchive.org/release/r/829521842-250.jpg\"}}\n"
    "  ],\n"
    "  \"release\": \"https://musicbrainz.org/release/r\"}";

// An old upload: legacy thumbnail names only, nothing at 1200 px.
const QByteArray kLegacyListing = "{\"images\": [\n"
    "    {\"front\": true, \"id\": \"42\", \"image\": \"https://example.org/42.jpg\",\n"
    "     \"thumbnails\": {\"large\": \"https://example.org/42-500.jpg\", \"small\": \"https://example.org/42-250.jpg\"}}]}";

} // namespace

class TestCoverArtArchiveClient : public QObject
{
    Q_OBJECT

private:
    QNetworkAccessManager m_nam;

private Q_SLOTS:
    void parseFront()
    {
        QString error;
        const auto front = CoverArtArchiveClient::parseFront(kListing, &error);
        QVERIFY(front);
        QCOMPARE(front->id, QStringLiteral("829521842"));
        // https, so Qt follows the redirects to archive.org.
        QCOMPARE(front->previewUrl(), QUrl(QStringLiteral("https://coverartarchive.org/release/r/829521842-500.jpg")));
        QCOMPARE(front->saveUrl(), QUrl(QStringLiteral("https://coverartarchive.org/release/r/829521842-1200.jpg")));

        const auto legacy = CoverArtArchiveClient::parseFront(kLegacyListing, &error);
        QVERIFY(legacy);
        QCOMPARE(legacy->id, QStringLiteral("42"));
        QCOMPARE(legacy->previewUrl(), QUrl(QStringLiteral("https://example.org/42-500.jpg")));
        QCOMPARE(legacy->saveUrl(), QUrl(QStringLiteral("https://example.org/42-500.jpg"))); // no 1200: 500

        const QByteArray noFront = "{\"images\": [{\"front\": false, \"id\": 1, \"image\": \"x\"}]}";
        QVERIFY(!CoverArtArchiveClient::parseFront(noFront));
        QVERIFY(!CoverArtArchiveClient::parseFront("nope", &error));
        QVERIFY(error.contains(QStringLiteral("unexpected answer")));
    }

    void frontCoverOverHttp()
    {
        TestHttpServer server;
        QVERIFY(server.listen());
        // The real service redirects listings to archive.org.
        server.route(QStringLiteral("/release/b84ee12a-09ef-421b-82de-0441a926375b"),
                     [&](const TestHttpServer::Request &) {
                         return TestHttpServer::Response{
                             307, {}, {{"Location", server.url(QStringLiteral("/download/index.json")).toEncoded()}}, 0};
                     });
        server.route(QStringLiteral("/download/index.json"), 200, kListing);
        server.route(QStringLiteral("/release-group/b8048f24-c026-3398-b23a-b5e50716cbc7"), 500, "oops");

        CoverArtArchiveClient caa(&m_nam);
        caa.setBaseUrl(server.url());
        std::optional<CaaImage> front;
        QString error = QStringLiteral("unset");
        auto done = [&](const std::optional<CaaImage> &f, const QString &e) { front = f; error = e; };

        caa.frontCover(CoverArtArchiveClient::Kind::Release, QStringLiteral("B84EE12A-09EF-421B-82DE-0441A926375B"), this, done);
        QTRY_VERIFY(error != QLatin1String("unset"));
        QVERIFY(error.isEmpty());
        QVERIFY(front);
        QCOMPARE(front->id, QStringLiteral("829521842"));
        QVERIFY(server.requests().front().headers.value("user-agent").startsWith("music-md-manager/"));

        // No art: 404 → nothing, no error.
        error = QStringLiteral("unset");
        caa.frontCover(CoverArtArchiveClient::Kind::Release, QStringLiteral("00000000-0000-0000-0000-000000000000"), this, done);
        QTRY_VERIFY(error != QLatin1String("unset"));
        QVERIFY(error.isEmpty());
        QVERIFY(!front);

        error = QStringLiteral("unset");
        caa.frontCover(CoverArtArchiveClient::Kind::ReleaseGroup, QStringLiteral("b8048f24-c026-3398-b23a-b5e50716cbc7"), this, done);
        QTRY_VERIFY(error != QLatin1String("unset"));
        QVERIFY(error.contains(QStringLiteral("HTTP 500")));
        QVERIFY(!front);
    }
};

QTEST_GUILESS_MAIN(TestCoverArtArchiveClient)
#include "tst_coverartarchiveclient.moc"

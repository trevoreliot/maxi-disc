// SPDX-License-Identifier: GPL-3.0-or-later
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "artworkfinder.h"
#include "testhttpserver.h"

using mmm::ArtworkFinder;
using mmm::ArtworkTarget;
using mmm::test::TestHttpServer;
using Status = mmm::ArtworkFinder::Status;

namespace {

const QString kMbidA = QStringLiteral("aaaaaaaa-0000-0000-0000-000000000001");
const QString kGroupA = QStringLiteral("aaaaaaaa-0000-0000-0000-0000000000a0");
const QString kRelB1 = QStringLiteral("bbbbbbbb-0000-0000-0000-000000000001");
const QString kRelB2 = QStringLiteral("bbbbbbbb-0000-0000-0000-000000000002");
const QString kRelB3 = QStringLiteral("bbbbbbbb-0000-0000-0000-000000000003");
const QString kGroupB = QStringLiteral("bbbbbbbb-0000-0000-0000-0000000000b0");

ArtworkTarget target(qint64 id, const char *title, const char *artist, int tracks, const QString &mbid = {})
{
    ArtworkTarget t;
    t.albumId = id;
    t.title = QString::fromUtf8(title);
    t.artist = QString::fromUtf8(artist);
    t.trackCount = tracks;
    t.mbAlbumId = mbid;
    return t;
}

QJsonObject releaseJson(const QString &id, const QString &group, const char *title, const char *artist, int tracks)
{
    return QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("title"), QString::fromUtf8(title)},
        {QStringLiteral("status"), QStringLiteral("Official")},
        {QStringLiteral("date"), QStringLiteral("2001")},
        {QStringLiteral("artist-credit"), QJsonArray{QJsonObject{{QStringLiteral("name"), QString::fromUtf8(artist)}}}},
        {QStringLiteral("release-group"), QJsonObject{{QStringLiteral("id"), group}}},
        {QStringLiteral("media"), QJsonArray{QJsonObject{{QStringLiteral("format"), QStringLiteral("CD")},
                                                         {QStringLiteral("track-count"), tracks}}}},
    };
}

QByteArray json(const QJsonObject &o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }

} // namespace

class TestArtworkFinder : public QObject
{
    Q_OBJECT

private:
    TestHttpServer m_server;

    QByteArray listing(const QString &imageId)
    {
        const QString base = m_server.url(QStringLiteral("/img/") + imageId).toString();
        return json(QJsonObject{{QStringLiteral("images"),
                                 QJsonArray{QJsonObject{
                                     {QStringLiteral("front"), true},
                                     {QStringLiteral("id"), imageId},
                                     {QStringLiteral("image"), base + QStringLiteral(".png")},
                                     {QStringLiteral("thumbnails"),
                                      QJsonObject{{QStringLiteral("500"), base + QStringLiteral("-500.jpg")},
                                                  {QStringLiteral("1200"), base + QStringLiteral("-1200.jpg")}}}}}}});
    }

    void configure(ArtworkFinder &finder)
    {
        finder.musicBrainz().setBaseUrl(m_server.url());
        finder.musicBrainz().fetcher().setOptions({1, 20, 0, 5000, 10}); // fast, still serialised
        finder.coverArt().setBaseUrl(m_server.url());
    }

    // Search queries the server saw for an album title, in order.
    QStringList searchesFor(const QString &title) const
    {
        QStringList out;
        for (const auto &r : m_server.requestsTo(QStringLiteral("/ws/2/release/"))) {
            const QString q = r.query.queryItemValue(QStringLiteral("query"), QUrl::FullyDecoded);
            if (q.contains(title))
                out << q;
        }
        return out;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_server.listen());
        // A: tagged with an MBID; the release has no art, its release group does.
        m_server.route(QStringLiteral("/ws/2/release/") + kMbidA, 200,
                       json(releaseJson(kMbidA, kGroupA, "Alpha", "Artist A", 9)));
        m_server.route(QStringLiteral("/release-group/") + kGroupA, 200, listing(QStringLiteral("100")));
        // B: found by search; B1 and B2 have art, B3 is another album, the group repeats B1's image.
        m_server.route(QStringLiteral("/release/") + kRelB1, 200, listing(QStringLiteral("201")));
        m_server.route(QStringLiteral("/release/") + kRelB2, 200, listing(QStringLiteral("202")));
        m_server.route(QStringLiteral("/release-group/") + kGroupB, 200, listing(QStringLiteral("201")));
        // Search answers by album title in the query.
        m_server.route(QStringLiteral("/ws/2/release/"), [](const TestHttpServer::Request &r) {
            const QString q = r.query.queryItemValue(QStringLiteral("query"), QUrl::FullyDecoded);
            QJsonArray releases;
            if (q.contains(QLatin1String("Bravo"))) {
                releases << releaseJson(kRelB2, kGroupB, "Bravo (Deluxe Edition)", "Artist B", 14)
                         << releaseJson(kRelB1, kGroupB, "Bravo", "Artist B", 11)
                         << releaseJson(kRelB3, kGroupB, "Something Else Entirely", "Artist B", 11);
            } else if (q.contains(QLatin1String("Delta"))) {
                return TestHttpServer::Response{500, "down", {}, 0};
            }
            return TestHttpServer::Response{200, json(QJsonObject{{QStringLiteral("releases"), releases}}), {}, 0};
        });
    }

    void findsCandidatesInBatch()
    {
        ArtworkFinder finder;
        configure(finder);
        QSignalSpy updates(&finder, &ArtworkFinder::albumUpdated);
        const QList<ArtworkTarget> albums = {
            target(1, "Alpha", "Artist A", 9, kMbidA),
            target(2, "Bravo", "Artist B", 11),
            target(3, "Charlie (Remastered)", "Artist C", 5),
            target(4, "Delta", "Artist D", 3),
        };
        finder.start(albums);
        QCOMPARE(finder.totalCount(), 4);
        QCOMPARE(finder.status(1), Status::Searching); // the first search starts right away
        QCOMPARE(finder.status(2), Status::Pending);
        QTRY_COMPARE_WITH_TIMEOUT(finder.searchedCount(), 4, 10000);

        // A: the release the tags name has no art; its release group's cover is offered.
        QCOMPARE(finder.status(1), Status::Found);
        const auto a = finder.candidates(1);
        QCOMPARE(a.size(), 1);
        QVERIFY(a[0].fromReleaseGroup);
        QCOMPARE(a[0].imageId, QStringLiteral("100"));
        QVERIFY(a[0].score >= 100);
        QCOMPARE(a[0].previewUrl, m_server.url(QStringLiteral("/img/100-500.jpg")));
        QCOMPARE(a[0].saveUrl, m_server.url(QStringLiteral("/img/100-1200.jpg")));
        QVERIFY(searchesFor(QStringLiteral("Alpha")).isEmpty()); // no search needed

        // B: the exact release first, the deluxe one second, the group's copy of B1 deduplicated,
        // the unrelated release never probed.
        QCOMPARE(finder.status(2), Status::Found);
        const auto b = finder.candidates(2);
        QCOMPARE(b.size(), 2);
        QCOMPARE(b[0].releaseId, kRelB1);
        QCOMPARE(b[0].title, QStringLiteral("Bravo"));
        QCOMPARE(b[0].format, QStringLiteral("CD"));
        QCOMPARE(b[0].trackCount, 11);
        QCOMPARE(b[1].releaseId, kRelB2);
        QVERIFY(b[0].score > b[1].score);
        QVERIFY(m_server.requestsTo(QStringLiteral("/release/") + kRelB3).isEmpty());
        QCOMPARE(m_server.requestsTo(QStringLiteral("/release-group/") + kGroupB).size(), 1);

        // C: nothing found, after a looser second search without the edition note.
        QCOMPARE(finder.status(3), Status::NotFound);
        QCOMPARE(searchesFor(QStringLiteral("Charlie")),
                 (QStringList{QStringLiteral("release:\"Charlie (Remastered)\" AND artist:\"Artist C\""),
                              QStringLiteral("release:(Charlie) AND artist:(Artist C)")}));

        // D: the search failed: an error, not "not found".
        QCOMPARE(finder.status(4), Status::Failed);
        const auto failed = std::find_if(updates.cbegin(), updates.cend(), [](const QList<QVariant> &args) {
            return args[0].toLongLong() == 4 && args[1].value<Status>() == Status::Failed;
        });
        QVERIFY(failed != updates.cend());
        QVERIFY(failed->at(2).toString().contains(QStringLiteral("HTTP 500")));
    }

    void prioritizeAndRetry()
    {
        ArtworkFinder finder;
        configure(finder);
        const qsizetype before = m_server.requestsTo(QStringLiteral("/ws/2/release/")).size();
        finder.start({target(11, "Charlie One", "X", 1), target(12, "Charlie Two", "X", 1),
                      target(13, "Charlie Three", "X", 1)});
        finder.prioritize(13); // jumps ahead of 12 (11 is already searching)
        QTRY_COMPARE_WITH_TIMEOUT(finder.searchedCount(), 3, 10000);
        QStringList order;
        const auto reqs = m_server.requestsTo(QStringLiteral("/ws/2/release/"));
        for (qsizetype i = before; i < reqs.size(); ++i) {
            const QString q = reqs[i].query.queryItemValue(QStringLiteral("query"), QUrl::FullyDecoded);
            for (const char *t : {"One", "Two", "Three"}) {
                if (q.contains(QLatin1String(t)) && !order.contains(QLatin1String(t)))
                    order << QString::fromLatin1(t);
            }
        }
        QCOMPARE(order, (QStringList{QStringLiteral("One"), QStringLiteral("Three"), QStringLiteral("Two")}));

        QSignalSpy updates(&finder, &ArtworkFinder::albumUpdated);
        finder.retry(12);
        QCOMPARE(finder.status(12), Status::Searching);
        QCOMPARE(finder.searchedCount(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(finder.status(12), Status::NotFound, 10000);
        QCOMPARE(finder.searchedCount(), 3);
        QCOMPARE(updates.front().at(1).value<Status>(), Status::Pending);
        finder.retry(999); // unknown: ignored
    }

    void stopDropsResults()
    {
        ArtworkFinder finder;
        configure(finder);
        QSignalSpy updates(&finder, &ArtworkFinder::albumUpdated);
        finder.start({target(2, "Bravo", "Artist B", 11)});
        finder.stop();
        QCOMPARE(finder.totalCount(), 0);
        updates.clear();
        QTest::qWait(300);
        QCOMPARE(updates.size(), 0);
        QCOMPARE(finder.status(2), Status::Pending);
        QVERIFY(finder.candidates(2).isEmpty());
    }

    void downloads()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("c.jpg"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("\xFF\xD8\xFF jpeg");
        }
        m_server.route(QStringLiteral("/img/201-1200.jpg"), 200, "\xFF\xD8\xFF from http");
        ArtworkFinder finder;
        configure(finder);
        QByteArray got;
        QString error = QStringLiteral("unset");
        finder.download(QUrl::fromLocalFile(path), this, [&](const QByteArray &d, const QString &e) { got = d; error = e; });
        QTRY_VERIFY(error != QLatin1String("unset"));
        QVERIFY(error.isEmpty());
        QCOMPARE(got, QByteArray("\xFF\xD8\xFF jpeg"));

        // stop() doesn't cancel downloads (an accepted cover still gets saved).
        error = QStringLiteral("unset");
        finder.download(m_server.url(QStringLiteral("/img/201-1200.jpg")), this,
                        [&](const QByteArray &d, const QString &e) { got = d; error = e; });
        finder.stop();
        QTRY_VERIFY(error != QLatin1String("unset"));
        QCOMPARE(got, QByteArray("\xFF\xD8\xFF from http"));

        error = QStringLiteral("unset");
        finder.download(m_server.url(QStringLiteral("/img/missing.jpg")), this,
                        [&](const QByteArray &d, const QString &e) { got = d; error = e; });
        QTRY_VERIFY(error != QLatin1String("unset"));
        QVERIFY(error.contains(QStringLiteral("404")));
        QVERIFY(got.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestArtworkFinder)
#include "tst_artworkfinder.moc"

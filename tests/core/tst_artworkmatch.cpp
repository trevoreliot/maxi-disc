// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include "artworkmatch.h"
#include "libraryquery.h"
#include "musicbrainzclient.h"

using namespace mmm;

namespace {

ArtworkTarget album(const char *title, const char *artist, int tracks = 10, int year = 2000)
{
    ArtworkTarget a;
    a.title = QString::fromUtf8(title);
    a.artist = QString::fromUtf8(artist);
    a.trackCount = tracks;
    a.year = year;
    return a;
}

MbRelease release(const char *title, const char *artist, int tracks = 10, int year = 2000,
                  const char *status = "Official")
{
    MbRelease r;
    r.title = QString::fromUtf8(title);
    r.artist = QString::fromUtf8(artist);
    r.trackCount = tracks;
    r.year = year;
    r.status = QString::fromLatin1(status);
    return r;
}

} // namespace

class TestArtworkMatch : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void normalize_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        QTest::newRow("case+punct") << QStringLiteral("OK Computer!") << QStringLiteral("ok computer");
        QTest::newRow("the") << QStringLiteral("The Beatles") << QStringLiteral("beatles");
        QTest::newRow("accents") << QStringLiteral("Björk – Homogénic") << QStringLiteral("bjork homogenic");
        QTest::newRow("ampersand") << QStringLiteral("Simon & Garfunkel") << QStringLiteral("simon and garfunkel");
        QTest::newRow("apostrophe") << QStringLiteral("Don’t Stop") << QStringLiteral("dont stop");
        QTest::newRow("cjk") << QStringLiteral("東京事変") << QStringLiteral("東京事変");
        QTest::newRow("empty") << QString() << QString();
    }
    void normalize()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(ArtworkMatch::normalize(in), out);
    }

    void stripEdition_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        QTest::newRow("paren") << QStringLiteral("OK Computer (Collector's Edition)") << QStringLiteral("OK Computer");
        QTest::newRow("bracket") << QStringLiteral("Abbey Road [2019 Remaster]") << QStringLiteral("Abbey Road");
        QTest::newRow("dash") << QStringLiteral("Rumours - Super Deluxe Edition") << QStringLiteral("Rumours");
        QTest::newRow("disc") << QStringLiteral("The Wall (Disc 2)") << QStringLiteral("The Wall");
        QTest::newRow("two") << QStringLiteral("Album (Disc 1) [Remastered]") << QStringLiteral("Album");
        QTest::newRow("keeps title parens") << QStringLiteral("(What's the Story) Morning Glory?")
                                            << QStringLiteral("(What's the Story) Morning Glory?");
        QTest::newRow("keeps non-edition") << QStringLiteral("Songs (For the Deaf)") << QStringLiteral("Songs (For the Deaf)");
        QTest::newRow("only a note") << QStringLiteral("(Deluxe)") << QStringLiteral("(Deluxe)");
    }
    void stripEdition()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(ArtworkMatch::stripEdition(in), out);
    }

    void similarity()
    {
        QCOMPARE(ArtworkMatch::similarity(QStringLiteral("Kid A"), QStringLiteral("kid a")), 1.0);
        QCOMPARE(ArtworkMatch::similarity(QStringLiteral("The Beatles"), QStringLiteral("Beatles")), 1.0);
        QVERIFY(ArtworkMatch::similarity(QStringLiteral("Homogenic"), QStringLiteral("Homogenik")) > 0.7);
        QVERIFY(ArtworkMatch::similarity(QStringLiteral("Abbey Road"), QStringLiteral("Let It Be")) < 0.3);
        QCOMPARE(ArtworkMatch::similarity(QStringLiteral("A"), QStringLiteral("B")), 0.0);
    }

    void scoreRanksTheRightRelease()
    {
        const ArtworkTarget a = album("OK Computer", "Radiohead", 12, 1997);
        const int exact = ArtworkMatch::score(a, release("OK Computer", "Radiohead", 12, 1997));
        const int reissue = ArtworkMatch::score(a, release("OK Computer", "Radiohead", 12, 2009));
        const int deluxe = ArtworkMatch::score(a, release("OK Computer (Collector's Edition)", "Radiohead", 32, 2009));
        const int bootleg = ArtworkMatch::score(a, release("OK Computer", "Radiohead", 12, 1997, "Bootleg"));
        const int cover = ArtworkMatch::score(a, release("OK Computer", "Various Artists", 12, 2006));
        QCOMPARE(exact, 40 + 25 + 15 + 10 + 5);
        QVERIFY(exact > reissue);
        QVERIFY(reissue > deluxe);
        QVERIFY(exact > bootleg);
        QVERIFY(reissue > cover);
        QVERIFY(deluxe >= 0);
        // A different album is rejected outright.
        QCOMPARE(ArtworkMatch::score(a, release("Pablo Honey", "Radiohead", 12, 1993)), -1);
    }

    void compilationsMatchVariousArtists()
    {
        ArtworkTarget a = album("Now 42", "Various Artists", 40, 1999);
        a.compilation = true;
        const int va = ArtworkMatch::score(a, release("Now 42", "Various Artists", 40, 1999));
        QCOMPARE(va, 40 + 25 + 15 + 10 + 5);
        // Compilation flag with an album artist that isn't "Various Artists" still matches VA.
        a.artist = QStringLiteral("Now Music");
        QCOMPARE(ArtworkMatch::score(a, release("Now 42", "Various Artists", 40, 1999)), va);
    }

    void unknownFieldsDontCount()
    {
        const ArtworkTarget a = album("Kid A", "Radiohead", 0, 0);
        QCOMPARE(ArtworkMatch::score(a, release("Kid A", "Radiohead", 10, 2000)), 40 + 25 + 5);
    }
};

QTEST_GUILESS_MAIN(TestArtworkMatch)
#include "tst_artworkmatch.moc"

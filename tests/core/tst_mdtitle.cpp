// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include "mdtitle.h"

using namespace mmm;
using namespace mmm::MdTitle;

class TestMdTitle : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void clean_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        QTest::addColumn<bool>("changed");
        QTest::newRow("ascii") << QStringLiteral("Hells Bells") << QStringLiteral("Hells Bells") << false;
        QTest::newRow("accents") << QStringLiteral("Björk – Jóga") << QStringLiteral("Bjork - Joga") << true;
        QTest::newRow("quotes") << QStringLiteral("“Hi” it’s…") << QStringLiteral("\"Hi\" it's...") << true;
        QTest::newRow("spelled") << QStringLiteral("Straße Æon Øresund") << QStringLiteral("Strasse AEon Oresund") << true;
        QTest::newRow("fullwidth") << QStringLiteral("ＡＢＣ１") << QStringLiteral("ABC1") << true;
        QTest::newRow("japanese decks") << QStringLiteral("a~b\\c") << QStringLiteral("a-b/c") << true;
        QTest::newRow("cjk and emoji dropped") << QStringLiteral("Song 歌 🎵") << QStringLiteral("Song") << true;
        QTest::newRow("spaces") << QStringLiteral("  a \t b  ") << QStringLiteral("a b") << true;
        QTest::newRow("empty") << QString() << QString() << false;
    }
    void clean()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QFETCH(bool, changed);
        const Cleaned c = MdTitle::clean(in);
        QCOMPARE(c.text, out);
        QCOMPARE(c.changed, changed);
        QVERIFY(!c.truncated);
    }

    void cleanCutsLongTitles()
    {
        const Cleaned c = MdTitle::clean(QString(300, QLatin1Char('a')));
        QCOMPARE(c.text.size(), kMaxTrackTitleBytes);
        QVERIFY(c.truncated);
    }

    void parseHeader_data()
    {
        QTest::addColumn<QString>("raw");
        QTest::addColumn<QString>("title");
        QTest::addColumn<int>("groups");
        QTest::newRow("empty") << QString() << QString() << 0;
        QTest::newRow("plain") << QStringLiteral("Summer Mix") << QStringLiteral("Summer Mix") << 0;
        QTest::newRow("grouped") << QStringLiteral("0;Road Trip//1-3;Side A//4;Bonus//") << QStringLiteral("Road Trip") << 2;
        QTest::newRow("title//") << QStringLiteral("Road Trip//1-3;Side A//") << QStringLiteral("Road Trip") << 1;
        QTest::newRow("untitled grouped") << QStringLiteral("0;//2-5;Group//") << QString() << 1;
        QTest::newRow("no disc token") << QStringLiteral("1-3;Side A//") << QString() << 1;
        QTest::newRow("empty group") << QStringLiteral("0;Disc//;Empty//") << QStringLiteral("Disc") << 1;
        QTest::newRow("0-n range") << QStringLiteral("0-0;Disc//1;One//") << QStringLiteral("Disc") << 1;
        QTest::newRow("garbage token skipped") << QStringLiteral("0;Disc//x-y;What//2;Two//") << QStringLiteral("Disc") << 1;
        QTest::newRow("stray piece doesn't replace the title") << QStringLiteral("0;Disc///1;One//") << QStringLiteral("Disc") << 0;
    }
    void parseHeader()
    {
        QFETCH(QString, raw);
        QFETCH(QString, title);
        QFETCH(int, groups);
        const Header h = MdTitle::parseHeader(raw);
        QCOMPARE(h.title, title);
        QCOMPARE(h.groups.size(), groups);
    }

    void groupsAreZeroBased()
    {
        const Header h = MdTitle::parseHeader(QStringLiteral("0;Road Trip//1-3;Side A//4;Bonus//;Empty//"));
        QCOMPARE(h.groups.size(), 3);
        QCOMPARE(h.groups[0], (MdGroup{QStringLiteral("Side A"), 0, 2}));
        QCOMPARE(h.groups[1], (MdGroup{QStringLiteral("Bonus"), 3, 3}));
        QCOMPARE(h.groups[2], (MdGroup{QStringLiteral("Empty"), -1, -1}));
        QVERIFY(h.groups[0].contains(2));
        QVERIFY(!h.groups[0].contains(3));
        QVERIFY(!h.groups[2].contains(0));
    }

    void formatRoundTrips()
    {
        QCOMPARE(formatHeader(Header{QStringLiteral("Summer Mix"), {}}), QStringLiteral("Summer Mix"));
        QCOMPARE(formatHeader(Header{}), QString());
        // A ';' would read as a group: the 0; form keeps it a title.
        QCOMPARE(formatHeader(Header{QStringLiteral("Live; Part 1"), {}}), QStringLiteral("0;Live; Part 1//"));
        QCOMPARE(MdTitle::parseHeader(QStringLiteral("0;Live; Part 1//")).title, QStringLiteral("Live; Part 1"));
        const QString grouped = QStringLiteral("0;Road Trip//1-3;Side A//4;Bonus//;Empty//");
        QCOMPARE(formatHeader(MdTitle::parseHeader(grouped)), grouped);
    }

    void withTitleKeepsGroups()
    {
        QCOMPARE(withTitle(QStringLiteral("0;Road Trip//1-3;Side A//4;Bonus//"), QStringLiteral("Trip 2")),
                 QStringLiteral("0;Trip 2//1-3;Side A//4;Bonus//"));
        QCOMPARE(withTitle(QStringLiteral("1-3;Side A//"), QStringLiteral("New")), QStringLiteral("0;New//1-3;Side A//"));
        QCOMPARE(withTitle(QStringLiteral("0;Old//1-3;Side A//"), QString()), QStringLiteral("0;//1-3;Side A//"));
        QCOMPARE(withTitle(QStringLiteral("Old"), QStringLiteral("New")), QStringLiteral("New"));
        QCOMPARE(withTitle(QString(), QString()), QString());
        // Slashes can't split the header: runs collapse, none at the end (also after shortening).
        QCOMPARE(withTitle(QString(), QStringLiteral("AC//DC")), QStringLiteral("AC/DC"));
        QCOMPARE(withTitle(QStringLiteral("0;x//1-3;B//"), QStringLiteral("A///1-3;C")), QStringLiteral("0;A/1-3;C//1-3;B//"));
        QCOMPARE(withTitle(QStringLiteral("0;x//1-3;B//"), QStringLiteral("Side/")), QStringLiteral("0;Side//1-3;B//"));
        QCOMPARE(MdTitle::parseHeader(withTitle(QStringLiteral("0;x//1-3;B//"), QStringLiteral("Side/"))).groups.size(), 1);
        bool shortened = false;
        const QString slashy = withTitle(QStringLiteral("0;x//1-3;B//"), QString(250, QLatin1Char('a')) + QStringLiteral("/////b"), &shortened);
        QVERIFY(shortened);
        QVERIFY(!MdTitle::parseHeader(slashy).title.endsWith(QLatin1Char('/')));
        QCOMPARE(MdTitle::parseHeader(slashy).groups.size(), 1);
        // Groups alone too long: the result doesn't fit (the caller refuses to write it).
        QString groups;
        for (int i = 1; i <= 30; ++i)
            groups += QStringLiteral("%1;Group number %1//").arg(i);
        QVERIFY(withTitle(groups, QStringLiteral("T")).size() > kMaxHeaderBytes);
        // Too long for the header (one length byte): the title is shortened, groups kept.
        bool cut = false;
        const QString longHeader = withTitle(QStringLiteral("0;x//1-3;Side A//"), QString(300, QLatin1Char('t')), &cut);
        QVERIFY(cut);
        QVERIFY(longHeader.size() <= kMaxHeaderBytes);
        QVERIFY(longHeader.endsWith(QStringLiteral("//1-3;Side A//")));
    }

    void errorsAreDescribed()
    {
        QVERIFY(describe(NetMdError::None).isEmpty());
        QVERIFY(describe(NetMdError::PermissionDenied).contains(QStringLiteral("udev")));
        QVERIFY(describe(NetMdError::Busy).contains(QStringLiteral("Web MiniDisc")));
    }
};

QTEST_GUILESS_MAIN(TestMdTitle)
#include "tst_mdtitle.moc"

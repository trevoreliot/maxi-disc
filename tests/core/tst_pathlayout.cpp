// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include "deviceprofile.h"
#include "libraryquery.h"
#include "pathlayout.h"

using namespace mmm;

namespace {

DeviceProfile profile(const QString &layout = DeviceProfiles::defaultLayout(), const QString &musicDir = QStringLiteral("Music"))
{
    DeviceProfile p = *DeviceProfiles::builtIn(QStringLiteral("usb-flash"));
    p.pathLayout = layout;
    p.musicDir = musicDir;
    return p;
}

TrackRow track(const char *albumArtist, const char *artist, const char *album, const char *title, int track = 1,
               int disc = 0, const char *codec = "FLAC")
{
    TrackRow t;
    t.path = QStringLiteral("/m/x/file.flac");
    t.albumArtist = QString::fromUtf8(albumArtist);
    t.artist = QString::fromUtf8(artist);
    t.album = QString::fromUtf8(album);
    t.title = QString::fromUtf8(title);
    t.trackNumber = track;
    t.discNumber = disc;
    t.year = 1997;
    t.codec = QString::fromLatin1(codec);
    return t;
}

} // namespace

class TestPathLayout : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void renders()
    {
        QCOMPARE(PathLayout::render(track("", "Radiohead", "OK Computer", "Airbag", 1), profile()),
                 QStringLiteral("Music/Radiohead/OK Computer/01 Airbag.flac"));
        // Optional disc prefix: only when there's a disc number.
        QCOMPARE(PathLayout::render(track("Pink Floyd", "Pink Floyd", "The Wall", "Hey You", 1, 2), profile()),
                 QStringLiteral("Music/Pink Floyd/The Wall/2-01 Hey You.flac"));
        // Album artist wins over the artist; padding.
        QCOMPARE(PathLayout::render(track("Various Artists", "Artist A", "Now 42", "Song", 7),
                                    profile(QStringLiteral("{albumartist}/{year} - {album}/{track:03}. {artist} - {title}"),
                                            QString())),
                 QStringLiteral("Various Artists/1997 - Now 42/007. Artist A - Song.flac"));
        // Missing tags: fallbacks; an empty folder (no year) is skipped.
        TrackRow bare = track("", "", "", "", 0);
        bare.year = 0;
        bare.path = QStringLiteral("/m/x/demo take.wav");
        bare.codec = QStringLiteral("WAV");
        QCOMPARE(PathLayout::render(bare, profile(QStringLiteral("{year}/{albumartist}/{album}/[{track:02} ]{title}"))),
                 QStringLiteral("Music/Unknown Artist/Unknown Album/demo take.wav"));
    }

    void extensionFollowsTranscodeDecision()
    {
        DeviceProfile dap = *DeviceProfiles::builtIn(QStringLiteral("generic-dap"));
        QCOMPARE(PathLayout::render(track("", "A", "B", "C", 1, 0, "FLAC"), dap), QStringLiteral("Music/A/B/01 C.mp3"));
        QCOMPARE(PathLayout::render(track("", "A", "B", "C", 1, 0, "MP3"), dap), QStringLiteral("Music/A/B/01 C.mp3"));
        QCOMPARE(PathLayout::render(track("", "A", "B", "C", 1, 0, "AAC"), profile()), QStringLiteral("Music/A/B/01 C.m4a"));
        QCOMPARE(PathLayout::render(track("", "A", "B", "C", 1, 0, "Opus"), profile()), QStringLiteral("Music/A/B/01 C.opus"));
    }

    void tagValuesNeverMakeFolders()
    {
        QCOMPARE(PathLayout::render(track("AC/DC", "AC/DC", "Back in Black", "Hells Bells", 1), profile()),
                 QStringLiteral("Music/AC_DC/Back in Black/01 Hells Bells.flac"));
        QCOMPARE(PathLayout::render(track("", "A\\B", "What?: <Live> \"1\" |*", "x", 1), profile()),
                 QStringLiteral("Music/A_B/What__ _Live_ _1_ __/01 x.flac"));
        QCOMPARE(PathLayout::sampleTrack().artist, QStringLiteral("AC/DC"));
        QCOMPARE(PathLayout::render(PathLayout::sampleTrack(), profile()),
                 QStringLiteral("Music/AC_DC/Back in Black/2-04 Back in Black.flac"));
    }

    void sanitizeName_data()
    {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("out");
        QTest::newRow("forbidden") << QStringLiteral("a<b>c:d\"e/f\\g|h?i*j") << QStringLiteral("a_b_c_d_e_f_g_h_i_j");
        QTest::newRow("control") << QStringLiteral("a\tb\nc\u0001d") << QStringLiteral("abcd");
        QTest::newRow("trailing dots/spaces") << QStringLiteral("Hello World... ") << QStringLiteral("Hello World");
        QTest::newRow("leading dots") << QStringLiteral("...And Justice for All") << QStringLiteral("And Justice for All");
        QTest::newRow("spaces") << QStringLiteral("  a   b  ") << QStringLiteral("a b");
        QTest::newRow("CON") << QStringLiteral("CON") << QStringLiteral("CON_");
        QTest::newRow("nul.mp3") << QStringLiteral("nul.mp3") << QStringLiteral("nul_.mp3");
        QTest::newRow("Com1") << QStringLiteral("Com1") << QStringLiteral("Com1_");
        QTest::newRow("CONTROL is fine") << QStringLiteral("CONTROL") << QStringLiteral("CONTROL");
        QTest::newRow("empty") << QString() << QStringLiteral("_");
        QTest::newRow("only dots") << QStringLiteral("...") << QStringLiteral("_");
        QTest::newRow("dot space dot") << QStringLiteral(". .foo") << QStringLiteral("foo");
        QTest::newRow("unicode kept") << QStringLiteral("Sigur Rós – ( )") << QStringLiteral("Sigur Rós – ( )");
    }
    void sanitizeName()
    {
        QFETCH(QString, in);
        QFETCH(QString, out);
        QCOMPARE(PathLayout::sanitizeName(in), out);
    }

    void longNamesAreCutSafely()
    {
        // Emoji are surrogate pairs: never cut in half.
        QString emoji;
        for (int i = 0; i < 100; ++i)
            emoji += QStringLiteral("🎵");
        const QString cut = PathLayout::sanitizeName(emoji, 121);
        QCOMPARE(cut.size(), 120);
        QVERIFY(!cut.back().isHighSurrogate());

        // Long classical titles: the whole path stays within the limit, extension kept.
        TrackRow t = track("Berliner Philharmoniker, Herbert von Karajan", "Berliner Philharmoniker",
                           "Beethoven: The 9 Symphonies (Complete Recordings, Remastered Edition, Deluxe Box)",
                           "Symphony No. 9 in D minor, Op. 125 \"Choral\": IV. Presto – Allegro assai – Presto (\"O Freunde, nicht diese Töne!\") – "
                           "Allegro molto assai (Alla marcia) – Andante maestoso – Allegro energico, sempre ben marcato",
                           4, 5);
        const QString path = PathLayout::render(t, profile(), 160);
        QVERIFY2(path.size() <= 160, qPrintable(path));
        QVERIFY(path.startsWith(QStringLiteral("Music/")));
        QVERIFY(path.endsWith(QStringLiteral(".flac")));
        QVERIFY(path.contains(QStringLiteral("/5-04 Symphony No. 9")));
    }

    void edgeCases()
    {
        // A title that sanitises to nothing is still the file name, not the album folder.
        QCOMPARE(PathLayout::render(track("", "A", "B", "...", 3), profile(QStringLiteral("{album}/{title}"))),
                 QStringLiteral("Music/B/Untitled.flac"));
        // A flat layout at the device root has no leading "/".
        QCOMPARE(PathLayout::render(track("", "A", "B", "C", 1), profile(QStringLiteral("{artist} - {title}"), QString())),
                 QStringLiteral("A - C.flac"));
        // The DOS-name "_" counts towards the length.
        const QString con = QStringLiteral("con.") + QString(116, QLatin1Char('x'));
        QVERIFY(PathLayout::sanitizeName(con, 120).size() <= 120);
        QVERIFY(PathLayout::sanitizeName(con, 120).startsWith(QStringLiteral("con_.")));
        // ext4 / btrfs limit names to 255 bytes: CJK is 3 bytes a character.
        const QString cjk = PathLayout::sanitizeName(QString(120, QChar(0x97F3)), 120);
        QVERIFY(cjk.toUtf8().size() <= 240);
    }

    void cleanFolder()
    {
        QCOMPARE(PathLayout::cleanFolder(QStringLiteral("Music")), QStringLiteral("Music"));
        QCOMPARE(PathLayout::cleanFolder(QStringLiteral("/Music//Car/")), QStringLiteral("Music/Car"));
        QCOMPARE(PathLayout::cleanFolder(QStringLiteral("..\\..\\etc")), QStringLiteral("etc"));
        QCOMPARE(PathLayout::cleanFolder(QStringLiteral(" . ")), QString());
        QCOMPARE(PathLayout::cleanFolder(QString()), QString());
    }

    void validate_data()
    {
        QTest::addColumn<QString>("pattern");
        QTest::addColumn<QString>("errorContains");
        QTest::newRow("default ok") << DeviceProfiles::defaultLayout() << QString();
        QTest::newRow("padded year ok") << QStringLiteral("{year:04} {title}") << QString();
        QTest::newRow("track only ok") << QStringLiteral("{album}/{track:02}") << QString();
        QTest::newRow("empty") << QStringLiteral(" ") << QStringLiteral("empty");
        QTest::newRow("unknown") << QStringLiteral("{genre}/{title}") << QStringLiteral("Unknown field {genre}");
        QTest::newRow("unclosed {") << QStringLiteral("{album/{title}") << QStringLiteral("Unknown field");
        QTest::newRow("no closing }") << QStringLiteral("{title") << QStringLiteral("no closing");
        QTest::newRow("stray }") << QStringLiteral("title}") << QStringLiteral("no opening");
        QTest::newRow("nested []") << QStringLiteral("[[{disc}]]{title}") << QStringLiteral("nested");
        QTest::newRow("unclosed [") << QStringLiteral("[{disc}-{title}") << QStringLiteral("no closing \"]\"");
        QTest::newRow("slash in []") << QStringLiteral("[{disc}/]{title}") << QStringLiteral("can't contain \"/\"");
        QTest::newRow("dotdot") << QStringLiteral("../{title}") << QStringLiteral("..");
        QTest::newRow("ends with /") << QStringLiteral("{album}/") << QStringLiteral("end with a file name");
        QTest::newRow("no title") << QStringLiteral("{albumartist}/{album}") << QStringLiteral("{title} or {track}");
        QTest::newRow("padded album") << QStringLiteral("{album:02}/{title}") << QStringLiteral("Only numbers");
    }
    void validate()
    {
        QFETCH(QString, pattern);
        QFETCH(QString, errorContains);
        const QString error = PathLayout::validate(pattern);
        if (errorContains.isEmpty())
            QVERIFY2(error.isEmpty(), qPrintable(error));
        else
            QVERIFY2(error.contains(errorContains), qPrintable(error));
    }

    void invalidLayoutRendersWithDefault()
    {
        QCOMPARE(PathLayout::render(track("", "A", "B", "C", 1), profile(QStringLiteral("{nope}"))),
                 QStringLiteral("Music/A/B/01 C.flac"));
    }
};

QTEST_GUILESS_MAIN(TestPathLayout)
#include "tst_pathlayout.moc"

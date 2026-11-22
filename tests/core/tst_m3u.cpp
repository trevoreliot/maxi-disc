// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include "m3u.h"

namespace {

bool writeRaw(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

QStringList paths(const QList<mmm::M3u::Entry> &entries)
{
    QStringList out;
    for (const auto &e : entries)
        out << e.path;
    return out;
}

} // namespace

class TestM3u : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    void readsExtendedPlaylist()
    {
        const QString pl = m_dir.filePath(QStringLiteral("lists/mix.m3u8"));
        QDir().mkpath(QFileInfo(pl).absolutePath());
        // BOM, CRLF, comments, relative + ../ + absolute + file:// + http (skipped), blank lines.
        const QByteArray body = "\xEF\xBB\xBF#EXTM3U\r\n"
                                "#EXTINF:187,Radiohead - Airbag\r\n"
                                "../music/Radiohead/01 Airbag.flac\r\n"
                                "\r\n"
                                "# a comment\r\n"
                                "#EXTINF:-1,Unknown length\r\n"
                                "sub/track.mp3\r\n"
                                "/abs/path/Björk/Jóga.flac\r\n"
                                "file:///abs/url%20encoded/song.ogg\r\n"
                                "http://radio.example/stream\r\n";
        QVERIFY(writeRaw(pl, body));

        QList<mmm::M3u::Entry> e;
        QString error;
        QVERIFY2(mmm::M3u::read(pl, &e, &error), qPrintable(error));
        const QString base = QFileInfo(pl).absolutePath();
        QCOMPARE(paths(e), (QStringList{
                               QDir::cleanPath(base + QStringLiteral("/../music/Radiohead/01 Airbag.flac")),
                               base + QStringLiteral("/sub/track.mp3"),
                               QStringLiteral("/abs/path/Björk/Jóga.flac"),
                               QStringLiteral("/abs/url encoded/song.ogg"),
                           }));
        QCOMPARE(e[0].title, QStringLiteral("Radiohead - Airbag"));
        QCOMPARE(e[0].durationMs, 187000);
        QCOMPARE(e[1].durationMs, -1);
        QVERIFY(e[2].title.isEmpty()); // no #EXTINF before it
    }

    void readsLegacyLatin1()
    {
        const QString pl = m_dir.filePath(QStringLiteral("old.m3u"));
        QVERIFY(writeRaw(pl, QByteArray("/music/Bj\xF6rk.mp3\n"))); // Latin-1 ö, not valid UTF-8
        QList<mmm::M3u::Entry> e;
        QVERIFY(mmm::M3u::read(pl, &e));
        QCOMPARE(paths(e), QStringList{QStringLiteral("/music/Björk.mp3")});
    }

    // Made on Windows or by a DAP: backslashes, paths rooted on another drive.
    void readsWindowsAndDevicePaths()
    {
        const QString root = m_dir.filePath(QStringLiteral("win/Media"));
        const QString who = root + QStringLiteral("/Music/The Who/Who’s Next");
        QVERIFY(QDir().mkpath(who));
        QVERIFY(QDir().mkpath(root + QStringLiteral("/Music/Playlists")));
        for (const char *name : {"Baba O’Riley.mp3", "Bargain.mp3", "Behind Blue Eyes.mp3"})
            QVERIFY(writeRaw(who + QLatin1Char('/') + QString::fromUtf8(name), "x"));
        const QString pl = root + QStringLiteral("/Music/Playlists/Classic Rock I.m3u");
        QVERIFY(writeRaw(pl, QStringLiteral("#EXTM3U\r\n"
                                            "\\Music\\The Who\\Who’s Next\\Baba O’Riley.mp3\r\n"
                                            "E:\\Music\\The Who\\Who’s Next\\Bargain.mp3\r\n"
                                            "..\\The Who\\Who’s Next\\Behind Blue Eyes.mp3\r\n"
                                            "\\Music\\Nobody\\Gone.mp3\r\n")
                                 .toUtf8()));
        QList<mmm::M3u::Entry> entries;
        QVERIFY(mmm::M3u::read(pl, &entries));
        QCOMPARE(paths(entries), QStringList({who + QStringLiteral("/Baba O’Riley.mp3"), who + QStringLiteral("/Bargain.mp3"),
                                              who + QStringLiteral("/Behind Blue Eyes.mp3"),
                                              QStringLiteral("/Music/Nobody/Gone.mp3")})); // not found: as written
    }

    // MMM_TEST_M3U=/path/to/list.m3u ./tst_m3u userPlaylist: how many entries resolve (read-only).
    void userPlaylist()
    {
        const QString pl = qEnvironmentVariable("MMM_TEST_M3U");
        if (pl.isEmpty())
            QSKIP("set MMM_TEST_M3U to a playlist to check");
        QList<mmm::M3u::Entry> entries;
        QVERIFY(mmm::M3u::read(pl, &entries));
        int found = 0;
        for (const auto &e : std::as_const(entries)) {
            if (QFileInfo::exists(e.path))
                ++found;
            else
                qInfo().noquote() << "not found:" << e.path;
        }
        qInfo().noquote() << found << "of" << entries.size() << "entries found";
        QCOMPARE(found, entries.size());
    }

    void missingFile()
    {
        QList<mmm::M3u::Entry> e;
        QString error;
        QVERIFY(!mmm::M3u::read(m_dir.filePath(QStringLiteral("nope.m3u")), &e, &error));
        QVERIFY(!error.isEmpty());
    }

    void writeRoundTrip_data()
    {
        QTest::addColumn<bool>("relative");
        QTest::newRow("absolute") << false;
        QTest::newRow("relative") << true;
    }
    void writeRoundTrip()
    {
        QFETCH(bool, relative);
        const QString pl = m_dir.filePath(QStringLiteral("out/list.m3u8"));
        QDir().mkpath(QFileInfo(pl).absolutePath());
        const QList<mmm::M3u::Entry> in = {
            {m_dir.filePath(QStringLiteral("music/Sigur Rós/01 Svefn-g-englar.flac")), QStringLiteral("Sigur Rós - Svefn-g-englar"), 600400},
            {m_dir.filePath(QStringLiteral("out/same-dir.mp3")), QStringLiteral("Here"), 1000},
            {QStringLiteral("/elsewhere/x.wav"), QString(), -1},
        };
        QString error;
        QVERIFY2(mmm::M3u::write(pl, in, relative, &error), qPrintable(error));

        QFile f(pl);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(f.readAll());
        QVERIFY(text.startsWith(QStringLiteral("#EXTM3U\n#EXTINF:600,Sigur Rós - Svefn-g-englar\n")));
        QCOMPARE(text.contains(QStringLiteral("\n../music/Sigur Rós/")), relative);
        QCOMPARE(text.contains(QLatin1String("\nsame-dir.mp3\n")), relative);
        QVERIFY(!text.contains(QLatin1Char('\r')));

        QList<mmm::M3u::Entry> out;
        QVERIFY(mmm::M3u::read(pl, &out));
        QCOMPARE(paths(out), paths(in)); // relative paths resolve back to the same files
        QCOMPARE(out[0].durationMs, 600000);
        QCOMPARE(out[0].title, in[0].title);
    }
};

QTEST_GUILESS_MAIN(TestM3u)
#include "tst_m3u.moc"

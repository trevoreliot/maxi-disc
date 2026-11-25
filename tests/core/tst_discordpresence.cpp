// SPDX-License-Identifier: GPL-3.0-or-later
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

#include "discordpresence.h"

using namespace mmm;

namespace {

DiscordTrack song(const QString &title = QStringLiteral("Idioteque"))
{
    return DiscordTrack{title, QStringLiteral("Radiohead"), QStringLiteral("Kid A"), 309'000};
}

QJsonObject activityOf(const QSignalSpy &spy, qsizetype i)
{
    return spy.at(i).first().toJsonObject().value(QStringLiteral("activity")).toObject();
}

} // namespace

// What Discord is told, and when (M15). No sockets.
class TestDiscordPresence : public QObject
{
    Q_OBJECT

    qint64 m_now = 1'700'000'000'000; // ms

    std::unique_ptr<DiscordPresence> presence(int windowMs = 20'000, bool fakeClock = true)
    {
        DiscordPresence::Options o;
        if (fakeClock)
            o.nowMs = [this] { return m_now; };
        o.coalesceMs = 20;
        o.windowMs = windowMs;
        o.pid = 4242;
        return std::make_unique<DiscordPresence>(o);
    }

private Q_SLOTS:
    void init() { m_now = 1'700'000'000'000; }

    void theActivity()
    {
        const QJsonObject a = DiscordPresence::activityFor(song(), 60'000, m_now);
        QCOMPARE(a.value(QStringLiteral("type")).toInt(), 2); // Listening
        // "Song - Artist" first (and in the member list), the album under it.
        QCOMPARE(a.value(QStringLiteral("details")).toString(), QStringLiteral("Idioteque - Radiohead"));
        QCOMPARE(a.value(QStringLiteral("status_display_type")).toInt(), 2);
        QCOMPARE(a.value(QStringLiteral("state")).toString(), QStringLiteral("Kid A"));
        QVERIFY(!a.contains(QStringLiteral("assets")));
        // Unix seconds: started a minute ago, ends 309 s after that.
        const QJsonObject ts = a.value(QStringLiteral("timestamps")).toObject();
        QCOMPARE(ts.value(QStringLiteral("start")).toInteger(), 1'700'000'000 - 60);
        QCOMPARE(ts.value(QStringLiteral("end")).toInteger(), 1'700'000'000 - 60 + 309);
    }

    void whatsUnknownIsLeftOut()
    {
        DiscordTrack t{QStringLiteral("Untitled"), QString(), QStringLiteral("  "), 0};
        const QJsonObject a = DiscordPresence::activityFor(t, -5, m_now);
        QCOMPARE(a.value(QStringLiteral("details")).toString(), QStringLiteral("Untitled")); // no " - "
        QVERIFY(!a.contains(QStringLiteral("state")));
        // A long "Song - Artist" is cut at 128 like any text.
        DiscordTrack longOne{QString(100, QLatin1Char('s')), QString(100, QLatin1Char('a')), {}, 0};
        QCOMPARE(DiscordPresence::activityFor(longOne, 0, m_now).value(QStringLiteral("details")).toString().size(), 128);
        const QJsonObject ts = a.value(QStringLiteral("timestamps")).toObject();
        QCOMPARE(ts.value(QStringLiteral("start")).toInteger(), 1'700'000'000); // position < 0 → 0
        QVERIFY(!ts.contains(QStringLiteral("end")));
        // Past the end (a late tick): the end, not beyond.
        const QJsonObject late = DiscordPresence::activityFor(song(), 400'000, m_now).value(QStringLiteral("timestamps")).toObject();
        QCOMPARE(late.value(QStringLiteral("start")).toInteger(), 1'700'000'000 - 309);
        // No title: nothing to show.
        QVERIFY(DiscordPresence::activityFor(DiscordTrack{QStringLiteral(" "), QStringLiteral("A"), {}, 0}, 0, m_now).isEmpty());
    }

    void textFitsDiscordsRules()
    {
        QCOMPARE(DiscordPresence::fit(QString()), QString());
        QCOMPARE(DiscordPresence::fit(QStringLiteral("  Kid A  ")), QStringLiteral("Kid A"));
        QCOMPARE(DiscordPresence::fit(QStringLiteral("X")), QStringLiteral("X") + QChar(0x200B)); // at least 2
        QCOMPARE(DiscordPresence::fit(QStringLiteral("😀")), QStringLiteral("😀") + QChar(0x200B)); // one character too
        const QString longTitle(300, QLatin1Char('a'));
        const QString cut = DiscordPresence::fit(longTitle);
        QCOMPARE(cut.size(), 128);
        QVERIFY(cut.endsWith(QChar(0x2026)));
        // Never half an emoji: a surrogate pair across the cut goes whole.
        const QString emoji = QString(126, QLatin1Char('a')) + QStringLiteral("😀😀😀");
        const QString cutEmoji = DiscordPresence::fit(emoji);
        QVERIFY(cutEmoji.size() <= 128);
        QVERIFY(!cutEmoji.at(cutEmoji.size() - 2).isHighSurrogate());
        QVERIFY(cutEmoji.endsWith(QChar(0x2026)));
    }

    void playingPausingAndTicks()
    {
        auto p = presence();
        QSignalSpy sent(p.get(), &DiscordPresence::activityChanged);
        // Not connected: nothing goes.
        p->update(song(), true, 0);
        QTest::qWait(60);
        QCOMPARE(sent.size(), 0);
        // Connected: what's playing.
        p->setConnected(true);
        QCOMPARE(sent.size(), 1);
        QCOMPARE(sent.first().first().toJsonObject().value(QStringLiteral("pid")).toInteger(), 4242);
        QCOMPARE(activityOf(sent, 0).value(QStringLiteral("details")).toString(), QStringLiteral("Idioteque - Radiohead"));

        // A track start's burst of signals and the position ticks: nothing new to say.
        for (int i = 1; i <= 20; ++i) {
            m_now += 100;
            p->update(song(), true, i * 100);
        }
        QTest::qWait(60);
        QCOMPARE(sent.size(), 1);

        // A seek moves the time bar.
        p->update(song(), true, 200'000);
        QTRY_COMPARE(sent.size(), 2);
        QCOMPARE(activityOf(sent, 1).value(QStringLiteral("timestamps")).toObject().value(QStringLiteral("start")).toInteger(),
                 m_now / 1000 - 200);

        // Paused: cleared (no activity), once.
        p->update(song(), false, 200'000);
        QTRY_COMPARE(sent.size(), 3);
        QVERIFY(!sent.at(2).first().toJsonObject().contains(QStringLiteral("activity")));
        p->update(std::nullopt, false, 0); // stopped too
        QTest::qWait(60);
        QCOMPARE(sent.size(), 3);

        // The next track.
        p->update(song(QStringLiteral("Morning Bell")), true, 0);
        QTRY_COMPARE(sent.size(), 4);
        QCOMPARE(activityOf(sent, 3).value(QStringLiteral("details")).toString(), QStringLiteral("Morning Bell - Radiohead"));
        QCOMPARE(p->sentCount(), 4);
    }

    void changesCloseTogetherGoAsOne()
    {
        auto p = presence();
        QSignalSpy sent(p.get(), &DiscordPresence::activityChanged);
        p->setConnected(true);
        QCOMPARE(sent.size(), 0); // nothing playing: nothing to clear on a new connection
        p->update(song(QStringLiteral("One")), true, 0);
        p->update(song(QStringLiteral("Two")), true, 0);
        p->update(song(QStringLiteral("Three")), true, 0);
        QTRY_COMPARE(sent.size(), 1);
        QTest::qWait(60);
        QCOMPARE(sent.size(), 1);
        QCOMPARE(activityOf(sent, 0).value(QStringLiteral("details")).toString(), QStringLiteral("Three - Radiohead"));
    }

    void fivePerWindowThenTheLatest()
    {
        auto p = presence(400, false); // the real clock: the window has to pass
        QSignalSpy sent(p.get(), &DiscordPresence::activityChanged);
        p->setConnected(true);
        QElapsedTimer clock;
        clock.start();
        for (int i = 1; i <= 8; ++i) {
            p->update(song(QStringLiteral("Track %1").arg(i)), true, 0);
            QTest::qWait(30);
        }
        QCOMPARE(sent.size(), 5);
        QTRY_COMPARE(sent.size(), 6);
        QVERIFY(clock.elapsed() >= 380);
        QCOMPARE(activityOf(sent, 5).value(QStringLiteral("details")).toString(), QStringLiteral("Track 8 - Radiohead")); // 6 and 7 skipped
        QTest::qWait(450);
        QCOMPARE(sent.size(), 6);
    }

    void aReconnectShowsItAgain()
    {
        auto p = presence();
        QSignalSpy sent(p.get(), &DiscordPresence::activityChanged);
        p->setConnected(true);
        p->update(song(), true, 0);
        QTRY_COMPARE(sent.size(), 1);
        p->setConnected(false); // Discord restarted
        p->setConnected(true);
        QCOMPARE(sent.size(), 2);
        QCOMPARE(activityOf(sent, 1).value(QStringLiteral("details")).toString(), QStringLiteral("Idioteque - Radiohead"));
        // Paused: a new connection has nothing to clear.
        p->update(song(), false, 0);
        QTRY_COMPARE(sent.size(), 3);
        p->setConnected(false);
        p->setConnected(true);
        QTest::qWait(60);
        QCOMPARE(sent.size(), 3);
        QVERIFY(!p->clearArgs().contains(QStringLiteral("activity")));
    }
};

QTEST_GUILESS_MAIN(TestDiscordPresence)
#include "tst_discordpresence.moc"

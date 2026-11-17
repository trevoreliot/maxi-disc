// SPDX-License-Identifier: GPL-3.0-or-later
#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QTest>

#include "playqueue.h"

using mmm::PlayQueue;
using mmm::TrackRow;
using Repeat = mmm::PlayQueue::RepeatMode;

namespace {

QList<TrackRow> tracks(std::initializer_list<const char *> titles, qint64 firstId = 1)
{
    QList<TrackRow> out;
    qint64 id = firstId;
    for (const char *t : titles) {
        TrackRow r;
        r.id = id++;
        r.title = QString::fromLatin1(t);
        r.path = QStringLiteral("/m/%1.flac").arg(r.title);
        r.durationMs = 1000;
        out << r;
    }
    return out;
}

QStringList titles(const PlayQueue &q)
{
    QStringList out;
    for (const TrackRow &t : q.items())
        out << t.title;
    return out;
}

} // namespace

class TestPlayQueue : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void emptyQueue()
    {
        PlayQueue q;
        QAbstractItemModelTester tester(&q, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(q.count(), 0);
        QCOMPARE(q.currentIndex(), -1);
        QVERIFY(!q.current());
        QCOMPARE(q.nextIndex(), -1);
        QCOMPARE(q.previousIndex(), -1);
        q.setCurrentIndex(0); // out of range: ignored
        QCOMPARE(q.currentIndex(), -1);
    }

    void setItemsAndNavigation()
    {
        PlayQueue q;
        QAbstractItemModelTester tester(&q, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy upcoming(&q, &PlayQueue::upcomingChanged);
        q.setItems(tracks({"a", "b", "c"}), 1);
        QCOMPARE(q.currentIndex(), 1);
        QCOMPARE(q.current()->title, QStringLiteral("b"));
        QCOMPARE(q.nextIndex(), 2);
        QCOMPARE(q.previousIndex(), 0);
        QCOMPARE(q.totalDurationMs(), 3000);
        QVERIFY(upcoming.count() >= 1);
        QCOMPARE(q.data(q.index(1), PlayQueue::IsCurrentRole).toBool(), true);
        QCOMPARE(q.data(q.index(0), PlayQueue::IsCurrentRole).toBool(), false);

        q.setCurrentIndex(2);
        QCOMPARE(q.nextIndex(), -1); // end, repeat off
        q.setItems(tracks({"x"}), 99);
        QCOMPARE(q.currentIndex(), 0); // clamped
        q.setItems({}, 0);
        QCOMPARE(q.currentIndex(), -1);
    }

    void repeatModes()
    {
        PlayQueue q;
        q.setItems(tracks({"a", "b", "c"}), 2);
        QSignalSpy upcoming(&q, &PlayQueue::upcomingChanged);
        q.setRepeatMode(Repeat::All);
        QCOMPARE(upcoming.count(), 1);
        QCOMPARE(q.nextIndex(), 0);      // wraps
        q.setCurrentIndex(0);
        QCOMPARE(q.previousIndex(), 2);  // wraps back too
        q.setRepeatMode(Repeat::One);
        QCOMPARE(q.nextIndex(), 0);      // same track
        q.setRepeatMode(Repeat::Off);
        QCOMPARE(q.previousIndex(), -1);
    }

    void insertNextAndAppend()
    {
        PlayQueue q;
        QAbstractItemModelTester tester(&q, QAbstractItemModelTester::FailureReportingMode::QtTest);
        q.setItems(tracks({"a", "b", "c"}), 0);
        q.insertNext(tracks({"x", "y"}, 10));
        QCOMPARE(titles(q), (QStringList{QStringLiteral("a"), QStringLiteral("x"), QStringLiteral("y"),
                                         QStringLiteral("b"), QStringLiteral("c")}));
        QCOMPARE(q.currentIndex(), 0);
        QCOMPARE(q.nextIndex(), 1);
        q.append(tracks({"z"}, 20));
        QCOMPARE(titles(q).last(), QStringLiteral("z"));

        // Inserting before the current track shifts the current index, not the current track.
        q.setCurrentIndex(3); // "b"
        PlayQueue q2;
        q2.setItems(tracks({"a", "b"}), 1);
        QSignalSpy cur(&q2, &PlayQueue::currentIndexChanged);
        q2.setCurrentIndex(-1);
        q2.insertNext(tracks({"first"}, 30)); // nothing current → at the start
        QCOMPARE(q2.items().first().title, QStringLiteral("first"));
    }

    void removeAt()
    {
        PlayQueue q;
        QAbstractItemModelTester tester(&q, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy removedCurrent(&q, &PlayQueue::currentRemoved);
        q.setItems(tracks({"a", "b", "c", "d"}), 2); // current "c"

        q.removeAt(0); // before current
        QCOMPARE(q.current()->title, QStringLiteral("c"));
        QCOMPARE(q.currentIndex(), 1);
        QCOMPARE(removedCurrent.count(), 0);

        q.removeAt(2); // after current ("d")
        QCOMPARE(q.current()->title, QStringLiteral("c"));
        QCOMPARE(removedCurrent.count(), 0);

        q.removeAt(1); // the current one ("c"); it was last, so "b" becomes current
        QCOMPARE(removedCurrent.count(), 1);
        QCOMPARE(q.current()->title, QStringLiteral("b"));

        q.setItems(tracks({"a", "b", "c"}), 1);
        q.removeAt(1); // current in the middle: the following track takes its place
        QCOMPARE(q.current()->title, QStringLiteral("c"));

        q.setItems(tracks({"only"}), 0);
        q.removeAt(0);
        QCOMPARE(q.currentIndex(), -1);
        q.removeAt(5); // out of range: ignored
    }

    void moveKeepsCurrentTrack()
    {
        PlayQueue q;
        QAbstractItemModelTester tester(&q, QAbstractItemModelTester::FailureReportingMode::QtTest);
        q.setItems(tracks({"a", "b", "c", "d"}), 1); // current "b"
        q.move(3, 0); // d to the front
        QCOMPARE(titles(q), (QStringList{QStringLiteral("d"), QStringLiteral("a"), QStringLiteral("b"),
                                         QStringLiteral("c")}));
        QCOMPARE(q.current()->title, QStringLiteral("b"));
        q.move(2, 3); // move the current track itself
        QCOMPARE(q.current()->title, QStringLiteral("b"));
        QCOMPARE(q.currentIndex(), 3);
        q.move(0, 3); // from before current to its position
        QCOMPARE(q.current()->title, QStringLiteral("b"));
        q.move(0, 0);
        q.move(-1, 2); // invalid: ignored
    }

    void clear()
    {
        PlayQueue q;
        q.setItems(tracks({"a"}), 0);
        q.clear();
        QCOMPARE(q.count(), 0);
        QCOMPARE(q.currentIndex(), -1);
    }
};

QTEST_GUILESS_MAIN(TestPlayQueue)
#include "tst_playqueue.moc"

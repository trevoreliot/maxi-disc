// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <taglib/fileref.h>
#include <taglib/tpropertymap.h>

#include <unistd.h>

#include "database.h"
#include "libraryscanner.h"
#include "testmedia.h"

using mmm::LibraryScanner;
using mmm::ScanStats;

namespace {

constexpr int kTimeoutMs = 30000;

TagLib::String ts(const QString &s) { return TagLib::String(s.toUtf8().constData(), TagLib::String::UTF8); }

bool setTags(const QString &path, const QString &title, const QString &artist, const QString &album)
{
    const QByteArray name = QFile::encodeName(path);
    TagLib::FileRef ref(name.constData());
    if (ref.isNull())
        return false;
    TagLib::PropertyMap props;
    props.replace("TITLE", ts(title));
    props.replace("ARTIST", ts(artist));
    props.replace("ALBUM", ts(album));
    ref.setProperties(props);
    return ref.save();
}

QVariant scalar(const QString &dbPath, const QString &sql)
{
    mmm::Database db;
    if (!db.open(dbPath))
        return {};
    QSqlQuery q(db.connection());
    if (!q.exec(sql) || !q.next())
        return {};
    return q.value(0);
}

int trackCount(const QString &dbPath) { return scalar(dbPath, QStringLiteral("SELECT count(*) FROM tracks")).toInt(); }

ScanStats lastStats(const QSignalSpy &spy) { return spy.last().at(0).value<ScanStats>(); }

} // namespace

class TestLibraryScanner : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_tmp;
    QString m_templates; // untagged source files, one per format
    int m_counter = 0;

    QString templateFor(const QString &ext) const { return m_templates + QStringLiteral("/t.") + ext; }

    // A fresh library dir + DB path per test.
    struct Fixture
    {
        QString lib;
        QString db;
    };
    Fixture fixture()
    {
        const QString base = m_tmp.filePath(QStringLiteral("case%1").arg(++m_counter));
        QDir().mkpath(base + QStringLiteral("/lib"));
        return {base + QStringLiteral("/lib"), base + QStringLiteral("/library.db")};
    }

    // Copies a template into the library and tags it.
    void addTrack(const QString &path, const QString &title, const QString &artist, const QString &album)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QVERIFY(QFile::copy(templateFor(QFileInfo(path).suffix()), path));
        QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner));
        QVERIFY(setTags(path, title, artist, album));
    }

    // n hard links to one tagged mp3: big libraries without disk space or encode time.
    void addManyTracks(const QString &dir, int n)
    {
        QDir().mkpath(dir);
        const QString src = dir + QStringLiteral("/000.mp3");
        addTrack(src, QStringLiteral("Bulk"), QStringLiteral("Bulk Artist"), QStringLiteral("Bulk Album"));
        const QByteArray from = QFile::encodeName(src);
        for (int i = 1; i < n; ++i) {
            const QByteArray to = QFile::encodeName(dir + QStringLiteral("/%1.mp3").arg(i, 3, 10, QLatin1Char('0')));
            QVERIFY(::link(from.constData(), to.constData()) == 0);
        }
    }

    // The standard small library used by several tests.
    void buildBasicLibrary(const QString &lib)
    {
        addTrack(lib + QStringLiteral("/Radiohead/OK Computer/01.flac"), QStringLiteral("Airbag"),
                 QStringLiteral("Radiohead"), QStringLiteral("OK Computer"));
        addTrack(lib + QStringLiteral("/Radiohead/OK Computer/02.mp3"), QStringLiteral("Paranoid Android"),
                 QStringLiteral("Radiohead"), QStringLiteral("OK Computer"));
        addTrack(lib + QStringLiteral("/Björk/Homogenic/01.ogg"), QStringLiteral("Hunter"), QStringLiteral("Björk"),
                 QStringLiteral("Homogenic"));
        // Folder art: cover.* beats folder.*; extension case doesn't matter.
        QFile c(lib + QStringLiteral("/Björk/Homogenic/cover.JPG"));
        QVERIFY(c.open(QIODevice::WriteOnly));
        c.write("jpeg");
        c.close();
        QFile f(lib + QStringLiteral("/Björk/Homogenic/folder.png"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("png");
        f.close();
        // Not audio / hidden / broken.
        QFile notes(lib + QStringLiteral("/Radiohead/notes.txt"));
        QVERIFY(notes.open(QIODevice::WriteOnly));
        notes.write("hello");
        notes.close();
        addTrack(lib + QStringLiteral("/.hidden/secret.flac"), QStringLiteral("Hidden"), QStringLiteral("X"),
                 QStringLiteral("X"));
        QFile broken(lib + QStringLiteral("/broken.flac"));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write(QByteArray(4096, 'x'));
        broken.close();
    }

    // Adds the library folder and waits for the scan that triggers.
    ScanStats addAndScan(LibraryScanner &s, const QString &lib)
    {
        QSignalSpy finished(&s, &LibraryScanner::scanFinished);
        QString error;
        if (!s.addFolder(lib, &error))
            qWarning("addFolder: %s", qPrintable(error));
        if (!QTest::qWaitFor([&] { return !finished.isEmpty() && !s.isScanning(); }, kTimeoutMs))
            qWarning("scan timed out");
        return finished.isEmpty() ? ScanStats{} : lastStats(finished);
    }

    ScanStats scanAndWait(LibraryScanner &s)
    {
        QSignalSpy finished(&s, &LibraryScanner::scanFinished);
        s.scan();
        if (!QTest::qWaitFor([&] { return !finished.isEmpty() && !s.isScanning(); }, kTimeoutMs))
            qWarning("scan timed out");
        return finished.isEmpty() ? ScanStats{} : lastStats(finished);
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        m_templates = m_tmp.filePath(QStringLiteral("templates"));
        QVERIFY(QDir().mkpath(m_templates));
        QCOMPARE(mmm::test::generateTone(templateFor(QStringLiteral("flac")), QStringLiteral("flacenc"), 300), QString());
        QCOMPARE(mmm::test::generateTone(templateFor(QStringLiteral("mp3")),
                                         QStringLiteral("lamemp3enc target=bitrate bitrate=64 cbr=true"), 300),
                 QString());
        QCOMPARE(mmm::test::generateTone(templateFor(QStringLiteral("ogg")), QStringLiteral("vorbisenc ! oggmux"), 300),
                 QString());
    }

    void isAudioFile_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("audio");
        QTest::newRow("flac") << "a.flac" << true;
        QTest::newRow("upper") << "A.FLAC" << true;
        QTest::newRow("mp3") << "x.y.mp3" << true;
        QTest::newRow("m4a") << "a.m4a" << true;
        QTest::newRow("opus") << "a.opus" << true;
        QTest::newRow("aiff") << "a.aiff" << true;
        QTest::newRow("txt") << "a.txt" << false;
        QTest::newRow("jpg") << "cover.jpg" << false;
        QTest::newRow("no ext") << "flac" << false;
        QTest::newRow("dotfile") << ".flac" << false;
        QTest::newRow("cue") << "a.cue" << false;
    }
    void isAudioFile()
    {
        QFETCH(QString, name);
        QFETCH(bool, audio);
        QCOMPARE(LibraryScanner::isAudioFile(name), audio);
    }

    void initialScan()
    {
        const Fixture fx = fixture();
        buildBasicLibrary(fx.lib);
        LibraryScanner s(fx.db);
        QVERIFY(s.open());
        const ScanStats st = addAndScan(s, fx.lib);

        QCOMPARE(st.filesSeen, 4); // 3 tracks + broken.flac; hidden dir and notes.txt skipped
        QCOMPARE(st.added, 3);
        QCOMPARE(st.failed, 1);
        QCOMPARE(st.failedPaths.size(), 1);
        QVERIFY(st.failedPaths.first().contains(QLatin1String("broken.flac")));
        QVERIFY(!st.cancelled);

        QCOMPARE(trackCount(fx.db), 3);
        QCOMPARE(scalar(fx.db, QStringLiteral("SELECT count(*) FROM albums")).toInt(), 2);
        QCOMPARE(scalar(fx.db, QStringLiteral("SELECT count(*) FROM tracks WHERE folder_id IS NULL")).toInt(), 0);
        QCOMPARE(scalar(fx.db, QStringLiteral("SELECT cover_path FROM albums WHERE title = 'Homogenic'")).toString(),
                 fx.lib + QStringLiteral("/Björk/Homogenic/cover.JPG"));
        QVERIFY(scalar(fx.db, QStringLiteral("SELECT cover_path FROM albums WHERE title = 'OK Computer'")).isNull());
        QCOMPARE(scalar(fx.db, QStringLiteral("SELECT count(*) FROM tracks WHERE path LIKE '%secret%'")).toInt(), 0);

        mmm::Database db;
        QVERIFY(db.open(fx.db));
        QCOMPARE(db.search(QStringLiteral("bjork hunt")).size(), 1);
        QCOMPARE(s.folders(), QStringList{QFileInfo(fx.lib).canonicalFilePath()});
    }

    void rescanSkipsUnchangedFiles()
    {
        const Fixture fx = fixture();
        buildBasicLibrary(fx.lib);
        LibraryScanner s(fx.db);
        QVERIFY(s.open());
        addAndScan(s, fx.lib);

        const ScanStats st = scanAndWait(s);
        QVERIFY(st.fullScan);
        QCOMPARE(st.added, 0);
        QCOMPARE(st.updated, 0);
        QCOMPARE(st.removed, 0);
        QCOMPARE(st.unchanged, 3);
        QCOMPARE(st.failed, 1); // unreadable files are retried each scan
        QCOMPARE(trackCount(fx.db), 3);
    }

    void changedFileIsReread()
    {
        const Fixture fx = fixture();
        buildBasicLibrary(fx.lib);
        LibraryScanner s(fx.db);
        QVERIFY(s.open());
        addAndScan(s, fx.lib);

        const QString path = fx.lib + QStringLiteral("/Radiohead/OK Computer/01.flac");
        QVERIFY(setTags(path, QStringLiteral("Airbag (Remastered)"), QStringLiteral("Radiohead"),
                        QStringLiteral("OK Computer")));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadWrite)); // make sure mtime moves even on coarse filesystems
        QVERIFY(f.setFileTime(QDateTime::currentDateTime().addSecs(10), QFileDevice::FileModificationTime));
        f.close();

        const ScanStats st = scanAndWait(s);
        QCOMPARE(st.updated, 1);
        QCOMPARE(st.unchanged, 2);
        QCOMPARE(scalar(fx.db, QStringLiteral("SELECT title FROM tracks WHERE path LIKE '%/01.flac'")).toString(),
                 QStringLiteral("Airbag (Remastered)"));
    }

    void deletedFileIsRemovedAndOrphansPruned()
    {
        const Fixture fx = fixture();
        buildBasicLibrary(fx.lib);
        LibraryScanner s(fx.db);
        QVERIFY(s.open());
        addAndScan(s, fx.lib);

        QVERIFY(QFile::remove(fx.lib + QStringLiteral("/Björk/Homogenic/01.ogg")));
        const ScanStats st = scanAndWait(s);
        QCOMPARE(st.removed, 1);
        QCOMPARE(trackCount(fx.db), 2);
        QCOMPARE(scalar(fx.db, QStringLiteral("SELECT count(*) FROM albums WHERE title = 'Homogenic'")).toInt(), 0);
        QCOMPARE(scalar(fx.db, QStringLiteral("SELECT count(*) FROM artists WHERE name = 'Björk'")).toInt(), 0);
        QCOMPARE(scalar(fx.db, QStringLiteral("SELECT count(*) FROM tracks_fts")).toInt(), 2);
    }

    void newFilesAreAdded()
    {
        const Fixture fx = fixture();
        buildBasicLibrary(fx.lib);
        LibraryScanner s(fx.db);
        QVERIFY(s.open());
        addAndScan(s, fx.lib);

        addTrack(fx.lib + QStringLiteral("/New Artist/New Album/01.mp3"), QStringLiteral("Fresh"),
                 QStringLiteral("New Artist"), QStringLiteral("New Album"));
        const ScanStats st = scanAndWait(s);
        QCOMPARE(st.added, 1);
        QCOMPARE(trackCount(fx.db), 4);
    }

    void unavailableFolderKeepsItsTracks()
    {
        // An unmounted drive must not look like "everything was deleted".
        const Fixture fx = fixture();
        buildBasicLibrary(fx.lib);
        LibraryScanner s(fx.db);
        QVERIFY(s.open());
        addAndScan(s, fx.lib);
        const QString canonical = QFileInfo(fx.lib).canonicalFilePath();

        const QString away = fx.lib + QStringLiteral("-unmounted");
        QVERIFY(QDir().rename(fx.lib, away));
        QSignalSpy errors(&s, &LibraryScanner::errorOccurred);
        ScanStats st = scanAndWait(s);
        QCOMPARE(st.removed, 0);
        QCOMPARE(st.unavailableFolders, QStringList{canonical});
        QCOMPARE(trackCount(fx.db), 3);
        QCOMPARE(errors.count(), 1);

        QVERIFY(QDir().rename(away, fx.lib));
        st = scanAndWait(s);
        QCOMPARE(st.unchanged, 3);
        QVERIFY(st.unavailableFolders.isEmpty());
    }

    void scanPathsRemovesVanishedSubdirectory()
    {
        const Fixture fx = fixture();
        buildBasicLibrary(fx.lib);
        LibraryScanner s(fx.db);
        QVERIFY(s.open());
        addAndScan(s, fx.lib);
        const QString lib = QFileInfo(fx.lib).canonicalFilePath();

        QVERIFY(QDir(lib + QStringLiteral("/Radiohead")).removeRecursively());
        QSignalSpy finished(&s, &LibraryScanner::scanFinished);
        s.scanPaths({lib + QStringLiteral("/Radiohead")});
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty() && !s.isScanning(), kTimeoutMs);
        const ScanStats st = lastStats(finished);
        QVERIFY(!st.fullScan);
        QCOMPARE(st.removed, 2);
        QCOMPARE(st.filesSeen, 0); // only the requested subtree was walked
        QCOMPARE(trackCount(fx.db), 1);
    }

    void scanPathsOutsideLibraryIsIgnored()
    {
        const Fixture fx = fixture();
        buildBasicLibrary(fx.lib);
        LibraryScanner s(fx.db);
        QVERIFY(s.open());
        addAndScan(s, fx.lib);
        QSignalSpy finished(&s, &LibraryScanner::scanFinished);
        s.scanPaths({m_templates});
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty() && !s.isScanning(), kTimeoutMs);
        QCOMPARE(lastStats(finished).filesSeen, 0);
        QCOMPARE(trackCount(fx.db), 3);
    }

    void removeFolder()
    {
        const Fixture fx = fixture();
        buildBasicLibrary(fx.lib);
        LibraryScanner s(fx.db);
        QVERIFY(s.open());
        addAndScan(s, fx.lib);

        QString error;
        QVERIFY2(s.removeFolder(fx.lib, &error), qPrintable(error));
        QVERIFY(s.folders().isEmpty());
        QCOMPARE(trackCount(fx.db), 0);
        QCOMPARE(scalar(fx.db, QStringLiteral("SELECT count(*) FROM tracks_fts")).toInt(), 0);
        QCOMPARE(scalar(fx.db, QStringLiteral("SELECT count(*) FROM artists")).toInt(), 0);
        QCOMPARE(scalar(fx.db, QStringLiteral("SELECT count(*) FROM albums")).toInt(), 0);
        QVERIFY(!s.removeFolder(fx.lib, &error));
    }

    void nestedFolders()
    {
        const Fixture fx = fixture();
        buildBasicLibrary(fx.lib);
        const QString lib = QFileInfo(fx.lib).canonicalFilePath();
        const QString sub = lib + QStringLiteral("/Radiohead");
        LibraryScanner s(fx.db);
        QVERIFY(s.open());

        // Child first, then parent: the parent absorbs the child.
        addAndScan(s, sub);
        QCOMPARE(trackCount(fx.db), 2);
        addAndScan(s, lib);
        QCOMPARE(s.folders(), QStringList{lib});
        QCOMPARE(trackCount(fx.db), 3);
        QCOMPARE(scalar(fx.db, QStringLiteral("SELECT count(DISTINCT folder_id) FROM tracks")).toInt(), 1);

        // Adding a folder inside an existing one is refused.
        QString error;
        QVERIFY(!s.addFolder(sub, &error));
        QVERIFY2(error.contains(QLatin1String("already in the library")), qPrintable(error));
        QVERIFY(!s.addFolder(lib, &error)); // the same folder again
    }

    void addFolderRejectsNonFolders()
    {
        const Fixture fx = fixture();
        LibraryScanner s(fx.db);
        QVERIFY(s.open());
        QString error;
        QVERIFY(!s.addFolder(fx.lib + QStringLiteral("/missing"), &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!s.addFolder(templateFor(QStringLiteral("mp3")), &error)); // a file
    }

    void requestsDuringScanAreCoalesced()
    {
        const Fixture fx = fixture();
        addManyTracks(fx.lib + QStringLiteral("/bulk"), 300);
        LibraryScanner s(fx.db);
        QVERIFY(s.open());
        QSignalSpy finished(&s, &LibraryScanner::scanFinished);
        QSignalSpy scanning(&s, &LibraryScanner::scanningChanged);
        QVERIFY(s.addFolder(fx.lib)); // starts a scan
        s.scan();
        s.scan();
        s.scan();
        QTRY_VERIFY_WITH_TIMEOUT(!s.isScanning(), kTimeoutMs);
        QVERIFY2(finished.count() <= 2, qPrintable(QString::number(finished.count()))); // running + one coalesced
        QCOMPARE(scanning.count(), 2); // true once, false once: back-to-back scans don't flap
        QCOMPARE(trackCount(fx.db), 300);
    }

    void cancelThenRescanCompletes()
    {
        const Fixture fx = fixture();
        addManyTracks(fx.lib + QStringLiteral("/bulk"), 600);
        LibraryScanner s(fx.db);
        QVERIFY(s.open());
        QSignalSpy finished(&s, &LibraryScanner::scanFinished);
        QVERIFY(s.addFolder(fx.lib));
        s.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty() && !s.isScanning(), kTimeoutMs);
        QVERIFY(trackCount(fx.db) <= 600); // whatever batches committed before the cancel

        const ScanStats st = scanAndWait(s);
        QVERIFY(!st.cancelled);
        QCOMPARE(trackCount(fx.db), 600);
        QCOMPARE(st.added + st.unchanged, 600);
    }

    void destroyWhileScanning()
    {
        const Fixture fx = fixture();
        addManyTracks(fx.lib + QStringLiteral("/bulk"), 600);
        {
            LibraryScanner s(fx.db);
            QVERIFY(s.open());
            QVERIFY(s.addFolder(fx.lib));
            QTest::qWait(20);
        } // must cancel, wait for the worker, and not deliver signals to a dead object
        QTest::qWait(50);

        LibraryScanner s(fx.db);
        QVERIFY(s.open());
        scanAndWait(s);
        QCOMPARE(trackCount(fx.db), 600);
    }

    void removeFolderWhileScanning()
    {
        const Fixture fx = fixture();
        addManyTracks(fx.lib + QStringLiteral("/bulk"), 600);
        LibraryScanner s(fx.db);
        QVERIFY(s.open());
        QVERIFY(s.addFolder(fx.lib));
        QVERIFY(s.isScanning());
        QVERIFY(s.removeFolder(fx.lib)); // cancels the scan first, so no tracks get re-added
        QTRY_VERIFY_WITH_TIMEOUT(!s.isScanning(), kTimeoutMs);
        QCOMPARE(trackCount(fx.db), 0);
    }

    void watcherPicksUpChanges()
    {
        const Fixture fx = fixture();
        buildBasicLibrary(fx.lib);
        const QString lib = QFileInfo(fx.lib).canonicalFilePath();
        LibraryScanner s(fx.db);
        QVERIFY(s.open());
        s.setWatchDebounceMs(200);
        s.setWatching(true);
        addAndScan(s, lib);
        QVERIFY(s.watchedDirectoryCount() >= 5); // lib, Radiohead, OK Computer, Björk, Homogenic (+ .hidden? no)

        QSignalSpy finished(&s, &LibraryScanner::scanFinished);

        // New file in a watched folder.
        addTrack(lib + QStringLiteral("/Radiohead/OK Computer/03.flac"), QStringLiteral("Subterranean"),
                 QStringLiteral("Radiohead"), QStringLiteral("OK Computer"));
        QTRY_VERIFY_WITH_TIMEOUT(trackCount(fx.db) == 4, kTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!s.isScanning(), kTimeoutMs);
        QVERIFY(!lastStats(finished).fullScan); // only the changed directory was rescanned

        // Deleted file.
        QVERIFY(QFile::remove(lib + QStringLiteral("/Radiohead/OK Computer/03.flac")));
        QTRY_VERIFY_WITH_TIMEOUT(trackCount(fx.db) == 3, kTimeoutMs);

        // New folder containing a file: the new folder gets watched too.
        const int watchedBefore = s.watchedDirectoryCount();
        addTrack(lib + QStringLiteral("/Portishead/Dummy/01.mp3"), QStringLiteral("Mysterons"),
                 QStringLiteral("Portishead"), QStringLiteral("Dummy"));
        QTRY_VERIFY_WITH_TIMEOUT(trackCount(fx.db) == 4, kTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(s.watchedDirectoryCount() == watchedBefore + 2, kTimeoutMs);

        // ...and a file added to that new folder later is seen.
        addTrack(lib + QStringLiteral("/Portishead/Dummy/02.mp3"), QStringLiteral("Sour Times"),
                 QStringLiteral("Portishead"), QStringLiteral("Dummy"));
        QTRY_VERIFY_WITH_TIMEOUT(trackCount(fx.db) == 5, kTimeoutMs);

        s.setWatching(false);
        QCOMPARE(s.watchedDirectoryCount(), 0);
    }

    void throughput()
    {
        // Informational: the plan targets 100k tracks in < 10 min (≈170 files/s).
        const Fixture fx = fixture();
        constexpr int kFiles = 2000;
        for (int i = 0; i < 4; ++i)
            addManyTracks(fx.lib + QStringLiteral("/bulk%1").arg(i), kFiles / 4);
        LibraryScanner s(fx.db);
        QVERIFY(s.open());
        const ScanStats first = addAndScan(s, fx.lib);
        QCOMPARE(first.added, kFiles);
        const ScanStats second = scanAndWait(s);
        QCOMPARE(second.unchanged, kFiles);
        qInfo("initial scan: %d files in %lld ms (%.0f files/s); rescan: %lld ms", kFiles, first.elapsedMs,
              kFiles * 1000.0 / static_cast<double>(std::max<qint64>(first.elapsedMs, 1)), second.elapsedMs);
        QVERIFY(first.elapsedMs < 60000);
    }
};

QTEST_GUILESS_MAIN(TestLibraryScanner)
#include "tst_libraryscanner.moc"

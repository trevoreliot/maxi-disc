// SPDX-License-Identifier: GPL-3.0-or-later
//
// mmm-scan: manage library folders and scan them into the library database.
//   mmm-scan --add ~/Music            add a folder (and scan it)
//   mmm-scan --scan                   incremental scan of every library folder
//   mmm-scan --scan --watch           scan, then keep watching for changes (Ctrl+C to stop)
//   mmm-scan --list | --remove DIR
// Uses the app's database unless --db is given.

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSocketNotifier>
#include <QSqlQuery>
#include <QTextStream>

#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

#include "appinfo.h"
#include "apppaths.h"
#include "database.h"
#include "libraryscanner.h"

namespace {

int s_signalFds[2] = {-1, -1};

void onSignal(int)
{
    const char c = 1;
    [[maybe_unused]] auto n = ::write(s_signalFds[0], &c, 1); // async-signal-safe
}

// Turns SIGINT/SIGTERM into a normal Qt event so the scanner can cancel and clean up.
void installSignalHandlers(QCoreApplication &app)
{
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, s_signalFds) != 0)
        return;
    auto *notifier = new QSocketNotifier(s_signalFds[1], QSocketNotifier::Read, &app);
    QObject::connect(notifier, &QSocketNotifier::activated, &app, [notifier] {
        char c;
        [[maybe_unused]] auto n = ::read(s_signalFds[1], &c, 1);
        notifier->setEnabled(false);
        QCoreApplication::quit();
    });
    struct sigaction sa {};
    sa.sa_handler = &onSignal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

qint64 count(const QString &dbPath, const char *table)
{
    mmm::Database db;
    if (!db.open(dbPath))
        return -1;
    QSqlQuery q(db.connection());
    if (q.exec(QStringLiteral("SELECT count(*) FROM %1").arg(QLatin1String(table))) && q.next())
        return q.value(0).toLongLong();
    return -1;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("mmm-scan"));
    QCoreApplication::setApplicationVersion(mmm::AppInfo::version());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Manage and scan the music-md-manager library."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption dbOpt(QStringLiteral("db"),
                                   QStringLiteral("Library database (default: %1).").arg(mmm::AppPaths::defaultDatabasePath()),
                                   QStringLiteral("FILE"), mmm::AppPaths::defaultDatabasePath());
    const QCommandLineOption addOpt(QStringLiteral("add"), QStringLiteral("Add a library folder and scan it."),
                                    QStringLiteral("DIR"));
    const QCommandLineOption removeOpt(QStringLiteral("remove"),
                                       QStringLiteral("Remove a library folder and its tracks."), QStringLiteral("DIR"));
    const QCommandLineOption listOpt(QStringLiteral("list"), QStringLiteral("List library folders."));
    const QCommandLineOption scanOpt(QStringLiteral("scan"), QStringLiteral("Incremental scan of all folders."));
    const QCommandLineOption watchOpt(QStringLiteral("watch"),
                                      QStringLiteral("Keep running and rescan folders when files change."));
    parser.addOptions({dbOpt, addOpt, removeOpt, listOpt, scanOpt, watchOpt});
    parser.process(app);

    QTextStream out(stdout);
    QTextStream err(stderr);
    const QString dbPath = parser.value(dbOpt);

    mmm::LibraryScanner scanner(dbPath);
    QString error;
    if (!scanner.open(&error)) {
        err << "cannot open " << dbPath << ": " << error << Qt::endl;
        return 2;
    }

    bool failed = false;
    const bool watch = parser.isSet(watchOpt);

    // Connect before adding folders: --add starts a scan straight away.
    QElapsedTimer readTimer;
    QObject::connect(&scanner, &mmm::LibraryScanner::scanStarted, &app, [&] {
        out << "scanning…" << Qt::endl;
        readTimer.invalidate();
    });
    QObject::connect(&scanner, &mmm::LibraryScanner::progress, &app, [&](int done, int total) {
        if (total == 0)
            return;
        if (!readTimer.isValid())
            readTimer.start();
        const double secs = static_cast<double>(readTimer.elapsed()) / 1000.0;
        out << "\r  reading tags " << done << " / " << total;
        if (secs > 1.0)
            out << "  (" << qRound(done / secs) << " files/s)";
        out << "\x1b[K" << Qt::flush;
    });
    QObject::connect(&scanner, &mmm::LibraryScanner::errorOccurred, &app,
                     [&](const QString &msg) { err << "\n" << msg << Qt::endl; });
    QObject::connect(&scanner, &mmm::LibraryScanner::scanFinished, &app, [&](const mmm::ScanStats &s) {
        out << "\r\x1b[K";
        out << (s.cancelled ? "scan cancelled" : (s.fullScan ? "full scan done" : "scan done")) << " in "
            << QString::number(static_cast<double>(s.elapsedMs) / 1000.0, 'f', 1) << " s: " << s.filesSeen
            << " audio files, " << s.added << " added, " << s.updated << " updated, " << s.unchanged
            << " unchanged, " << s.removed << " removed, " << s.failed << " failed\n";
        for (const QString &f : s.failedPaths)
            out << "  failed: " << f << "\n";
        out << "library: " << count(dbPath, "tracks") << " tracks, " << count(dbPath, "albums") << " albums, "
            << count(dbPath, "artists") << " artists" << Qt::endl;
        if (watch && !scanner.isScanning())
            out << "watching " << scanner.watchedDirectoryCount() << " folders for changes (Ctrl+C to stop)" << Qt::endl;
    });
    QObject::connect(&scanner, &mmm::LibraryScanner::scanningChanged, &app, [&](bool scanning) {
        if (!scanning && !watch)
            QCoreApplication::exit(failed ? 1 : 0);
    });

    for (const QString &dir : parser.values(removeOpt)) {
        if (scanner.removeFolder(dir, &error)) {
            out << "removed " << dir << Qt::endl;
        } else {
            err << "remove " << dir << ": " << error << Qt::endl;
            failed = true;
        }
    }
    for (const QString &dir : parser.values(addOpt)) {
        if (scanner.addFolder(dir, &error)) {
            out << "added " << dir << Qt::endl;
        } else {
            err << "add " << dir << ": " << error << Qt::endl;
            failed = true;
        }
    }
    if (parser.isSet(listOpt)) {
        out << "database: " << dbPath << "\n";
        const QStringList folders = scanner.folders();
        if (folders.isEmpty())
            out << "no library folders (add one with --add DIR)\n";
        for (const QString &f : folders)
            out << "  " << f << "\n";
        out << Qt::flush;
    }

    if (parser.isSet(scanOpt) || watch) // watching needs a scan to discover the folders
        scanner.scan();
    if (!scanner.isScanning() && !watch)
        return failed ? 1 : 0;

    if (watch)
        scanner.setWatching(true);
    installSignalHandlers(app);
    return QCoreApplication::exec();
}

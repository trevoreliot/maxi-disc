// SPDX-License-Identifier: GPL-3.0-or-later
//
// mmm-tags: show what music-md-manager reads from audio files.
//   mmm-tags [--cover DIR] [--db FILE] PATH...
// PATH may be a file or a directory (scanned recursively for audio files).

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include "appinfo.h"
#include "database.h"
#include "tagreader.h"

namespace {

const QStringList kAudioGlobs = {
    QStringLiteral("*.flac"), QStringLiteral("*.mp3"), QStringLiteral("*.wav"), QStringLiteral("*.m4a"),
    QStringLiteral("*.mp4"), QStringLiteral("*.aac"), QStringLiteral("*.ogg"), QStringLiteral("*.oga"),
    QStringLiteral("*.opus"), QStringLiteral("*.aif"), QStringLiteral("*.aiff"),
};

QStringList expand(const QStringList &paths)
{
    QStringList files;
    for (const QString &p : paths) {
        const QFileInfo fi(p);
        if (fi.isDir()) {
            QDirIterator it(p, kAudioGlobs, QDir::Files, QDirIterator::Subdirectories);
            QStringList found;
            while (it.hasNext())
                found << it.next();
            found.sort();
            files << found;
        } else {
            files << p;
        }
    }
    return files;
}

QString opt(const std::optional<double> &v, const char *unit)
{
    return v ? QString::number(*v, 'f', 2) + QLatin1String(unit) : QStringLiteral("-");
}

QString orDash(const QString &s) { return s.isEmpty() ? QStringLiteral("-") : s; }
QString orDash(int v) { return v ? QString::number(v) : QStringLiteral("-"); }

void print(QTextStream &out, const QString &path, const mmm::TrackTags &t)
{
    out << path << "\n";
    out << "  title         " << orDash(t.title) << "\n";
    out << "  artist        " << orDash(t.artist) << "\n";
    out << "  album         " << orDash(t.album) << "\n";
    out << "  album artist  " << orDash(t.albumArtist) << (t.compilation ? "  (compilation)" : "") << "\n";
    out << "  genre         " << orDash(t.genre) << "\n";
    out << "  year          " << orDash(t.year) << "\n";
    out << "  track         " << orDash(t.trackNumber) << " / " << orDash(t.trackTotal)
        << "    disc " << orDash(t.discNumber) << " / " << orDash(t.discTotal) << "\n";
    if (!t.composer.isEmpty())
        out << "  composer      " << t.composer << "\n";
    out << "  replaygain    track " << opt(t.replayGainTrackGain, " dB") << " peak "
        << opt(t.replayGainTrackPeak, "") << " | album " << opt(t.replayGainAlbumGain, " dB")
        << " peak " << opt(t.replayGainAlbumPeak, "") << "\n";
    if (!t.musicBrainzAlbumId.isEmpty())
        out << "  mb album id   " << t.musicBrainzAlbumId << "\n";
    const qint64 s = t.durationMs / 1000;
    out << "  audio         " << t.codec << ", " << QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'))
        << ", " << t.bitrateKbps << " kbps, " << t.sampleRate << " Hz, " << t.channels << " ch";
    if (t.bitsPerSample)
        out << ", " << t.bitsPerSample << "-bit";
    out << "\n  embedded art  " << (t.hasEmbeddedArt ? "yes" : "no") << "\n";
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("mmm-tags"));
    QCoreApplication::setApplicationVersion(mmm::AppInfo::version());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Show tags as music-md-manager reads them."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption coverOpt(QStringLiteral("cover"),
                                      QStringLiteral("Extract each file's cover art into DIR."),
                                      QStringLiteral("DIR"));
    const QCommandLineOption dbOpt(QStringLiteral("db"),
                                   QStringLiteral("Also insert the tracks into this SQLite database."),
                                   QStringLiteral("FILE"));
    const QCommandLineOption quietOpt({QStringLiteral("q"), QStringLiteral("quiet")},
                                      QStringLiteral("Only print errors and the summary."));
    parser.addOptions({coverOpt, dbOpt, quietOpt});
    parser.addPositionalArgument(QStringLiteral("paths"), QStringLiteral("Audio files or folders."),
                                 QStringLiteral("PATH..."));
    parser.process(app);

    if (parser.positionalArguments().isEmpty())
        parser.showHelp(2);

    QTextStream out(stdout);
    QTextStream err(stderr);

    const QString coverDir = parser.value(coverOpt);
    if (!coverDir.isEmpty() && !QDir().mkpath(coverDir)) {
        err << "cannot create " << coverDir << Qt::endl;
        return 2;
    }

    mmm::Database db;
    const bool useDb = parser.isSet(dbOpt);
    if (useDb) {
        QString error;
        if (!db.open(parser.value(dbOpt), &error)) {
            err << "database: " << error << Qt::endl;
            return 2;
        }
    }

    const QStringList files = expand(parser.positionalArguments());
    int ok = 0;
    int failed = 0;
    QElapsedTimer timer;
    timer.start();

    if (useDb)
        db.connection().transaction(); // one commit for the whole run (upserts use savepoints)

    for (const QString &path : files) {
        QString error;
        const auto tags = mmm::TagReader::read(path, &error);
        if (!tags) {
            err << "FAILED " << path << ": " << error << Qt::endl;
            ++failed;
            continue;
        }
        ++ok;
        if (!parser.isSet(quietOpt))
            print(out, path, *tags);

        if (!coverDir.isEmpty() && tags->hasEmbeddedArt) {
            if (const auto cover = mmm::TagReader::readCover(path)) {
                const QString ext = cover->mimeType.endsWith(QLatin1String("png")) ? QStringLiteral("png")
                                                                                    : QStringLiteral("jpg");
                QFile f(QDir(coverDir).filePath(QFileInfo(path).completeBaseName() + QLatin1Char('.') + ext));
                if (f.open(QIODevice::WriteOnly) && f.write(cover->data) == cover->data.size()) {
                    if (!parser.isSet(quietOpt))
                        out << "  cover         " << f.fileName() << " (" << cover->pictureType << ", "
                            << cover->data.size() / 1024 << " KiB)\n";
                } else {
                    err << "cannot write " << f.fileName() << Qt::endl;
                }
            }
        }

        if (useDb) {
            const QFileInfo fi(path);
            if (!db.upsertTrack(fi.absoluteFilePath(), fi.lastModified().toSecsSinceEpoch(), fi.size(), *tags, &error)) {
                err << "DB FAILED " << path << ": " << error << Qt::endl;
                ++failed;
            }
        }
        if (!parser.isSet(quietOpt))
            out << "\n";
    }

    if (useDb && !db.connection().commit())
        err << "database commit failed" << Qt::endl;

    out << ok << " read, " << failed << " failed, " << timer.elapsed() << " ms" << Qt::endl;
    return failed ? 1 : 0;
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "mduploadplanner.h"

#include "mdtitle.h"

#include <QFileInfo>

namespace mmm::MdUploadPlanner {

namespace {

QString minutes(qint64 ms)
{
    const qint64 s = (ms + 500) / 1000;
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

qint64 neededMs(qint64 durationMs, MdRecordMode mode)
{
    switch (mode) {
    case MdRecordMode::Sp:
        return durationMs;
    case MdRecordMode::Lp2:
        return (durationMs + 1) / 2;
    case MdRecordMode::Lp4:
        return (durationMs + 3) / 4;
    }
    return durationMs;
}

QString titleFor(const TrackRow &track, int number)
{
    QString title = MdTitle::clean(track.title).text;
    if (title.isEmpty())
        title = MdTitle::clean(QFileInfo(track.path).completeBaseName()).text;
    return title.isEmpty() ? QStringLiteral("Track %1").arg(number) : title;
}

MdUploadPlan plan(const MdDisc &disc, bool hasDisc, const QList<TrackRow> &tracks, MdRecordMode mode)
{
    MdUploadPlan p;
    p.mode = mode;
    bool unknownLength = false;
    QString missing;
    for (const TrackRow &t : tracks) {
        p.durationMs += std::max<qint64>(0, t.durationMs);
        p.neededMs += neededMs(std::max<qint64>(0, t.durationMs), mode);
        unknownLength = unknownLength || t.durationMs <= 0;
        if (missing.isEmpty() && !QFileInfo::exists(t.path))
            missing = t.path;
    }
    p.availableMs = hasDisc && disc.capacity.valid ? disc.capacity.availableMs : 0;

    if (tracks.isEmpty())
        p.error = QStringLiteral("Nothing to record yet: add an album, artist or playlist (i).");
    else if (!hasDisc || !disc.capacity.valid)
        p.error = QStringLiteral("No disc to record to.");
    else if (disc.tracks.size() + tracks.size() > kMaxTracks)
        p.error = QStringLiteral("A MiniDisc holds at most %1 tracks (%2 on it, %3 to add).")
                      .arg(kMaxTracks)
                      .arg(disc.tracks.size())
                      .arg(tracks.size());
    else if (!missing.isEmpty())
        p.error = QStringLiteral("File not found: %1").arg(missing);
    else if (p.neededMs > p.availableMs)
        // In the mode's own time (an 80-minute disc holds 160 minutes of LP2).
        p.error = QStringLiteral("Doesn't fit in %1: %2 needed, %3 free.")
                      .arg(mdModeName(mode), minutes(p.neededMs * mdModeFactor(mode)), minutes(p.availableMs * mdModeFactor(mode)));
    p.canRecord = p.error.isEmpty();

    if (p.canRecord) {
        QStringList warnings;
        p.tight = p.neededMs + kRoundingPerTrackMs * tracks.size() > p.availableMs;
        if (p.tight)
            warnings << QStringLiteral("A tight fit: the last track may not fit.");
        if (unknownLength)
            warnings << QStringLiteral("Some tracks have no known length.");
        p.warning = warnings.join(QLatin1Char(' '));
    }

    // A blank untitled disc filled from one album: offer its title.
    if (hasDisc && disc.tracks.isEmpty() && disc.title.trimmed().isEmpty() && !tracks.isEmpty()) {
        const QString album = tracks.first().album;
        const bool oneAlbum = std::all_of(tracks.cbegin(), tracks.cend(),
                                          [&](const TrackRow &t) { return t.album == album; });
        if (oneAlbum && !album.trimmed().isEmpty())
            p.suggestedDiscTitle = MdTitle::clean(album, MdTitle::kMaxHeaderBytes).text;
    }
    return p;
}

} // namespace mmm::MdUploadPlanner

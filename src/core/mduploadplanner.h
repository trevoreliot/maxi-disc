// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>

#include "mdupload.h"

namespace mmm::MdUploadPlanner {

constexpr int kMaxTracks = 255;          // the disc's TOC
constexpr qint64 kRoundingPerTrackMs = 2000; // tracks start on whole clusters: a little slack each

// Disc time a track of `durationMs` takes in `mode`.
qint64 neededMs(qint64 durationMs, MdRecordMode mode);

// Can `tracks` go on `disc` (appended)? Refuses: nothing to record, no readable disc, too long,
// over the track limit, a missing file. Warns: a tight fit, tracks of unknown length.
MdUploadPlan plan(const MdDisc &disc, bool hasDisc, const QList<TrackRow> &tracks, MdRecordMode mode = MdRecordMode::Sp);

// The title a track gets on the disc: its title, else its file name, else "Track N".
QString titleFor(const TrackRow &track, int number);

} // namespace mmm::MdUploadPlanner

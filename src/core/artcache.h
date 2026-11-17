// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace mmm {

struct TrackRow;

namespace ArtCache {

// A local image file for the track's album art, for clients that need a file (MPRIS
// mpris:artUrl): the album's folder art if it exists, else the track's embedded cover
// written (undecoded) into `cacheDir`, keyed on the track file's path/mtime/size.
// Returns "" when there's no art.
QString fileFor(const TrackRow &track, const QString &cacheDir);

} // namespace ArtCache
} // namespace mmm

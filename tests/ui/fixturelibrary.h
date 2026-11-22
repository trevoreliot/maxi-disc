// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace mmm::test {

// Builds a small library database in `dir` for UI tests and returns its path:
//   - Radiohead / OK Computer (1997): 3 tracks, folder art (real 300×300 PNG, cover.png)
//   - Radiohead / Kid A (2000): 2 tracks, embedded art. Only Kid A/01.flac exists on disk: a
//     real audio file with a 200×100 PNG and the same tags as its library entry.
//   - The Beatles / Abbey Road (1969): 2 tracks, no art (sorts under B)
//   - Pink Floyd / The Wall (1979): 2 discs, 3 tracks, inserted out of order
//   - Various Artists / Now 42 (1999): compilation, 2 tracks by different artists
// Genres: Rock (all but Now 42), Pop (Now 42). `dir` is registered as the library folder.
// Returns "" on failure (see qWarning).
QString buildFixtureLibrary(const QString &dir);

} // namespace mmm::test

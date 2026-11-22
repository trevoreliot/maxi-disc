// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace mmm {

struct ArtworkTarget;
struct MbRelease;

// How well a MusicBrainz release matches a library album (artwork finder). Pure functions.
namespace ArtworkMatch {

// Lower case, accents and punctuation removed, "&" → "and", a leading "the " dropped,
// whitespace collapsed: "The Beatles – Abbey Road!" → "beatles abbey road".
QString normalize(const QString &s);
// Drops a trailing edition note: "OK Computer (Collector's Edition)" → "OK Computer".
// Also "[Remastered]", "- Deluxe Edition", "(Disc 2)".
QString stripEdition(const QString &title);
// 0–1: Dice coefficient of the normalised strings' letter pairs (1 = same).
double similarity(const QString &a, const QString &b);

// Higher is better; < 0 = not this album. Title (0–40), artist (0–25), track count
// (0–15), year (0–10), official release (+5), bootlegs (−10).
int score(const ArtworkTarget &album, const MbRelease &release);

} // namespace ArtworkMatch
} // namespace mmm

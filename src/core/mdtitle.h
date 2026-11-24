// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>

#include "mdtypes.h"

// MiniDisc titles (M10): what the recorder stores and how the disc title holds groups.
namespace mmm::MdTitle {

// The disc header (disc title + groups) is written with a one-byte length.
constexpr int kMaxHeaderBytes = 255;
constexpr int kMaxTrackTitleBytes = 254; // netmd_set_title reads the old title into 255 bytes

struct Cleaned
{
    QString text;
    bool changed = false;   // characters were replaced or dropped
    bool truncated = false; // cut to the maximum length
};

// What a title becomes on the disc: printable ASCII only (NetMD's half-width titles).
// Accents are dropped ("Björk" → "Bjork"), typographic punctuation is made plain (“” → "",
// – → -, … → ...), ß/æ/ø and friends are spelled out, anything else non-ASCII goes. Tilde and
// backslash show as other characters on Japanese decks, so they become - and /. Spaces are collapsed
// and trimmed. Cut to `maxBytes`.
Cleaned clean(const QString &title, int maxBytes = kMaxTrackTitleBytes);

// --- The disc header: the disc title and the groups, in one string ---
struct Header
{
    QString title;
    QList<MdGroup> groups;
    friend bool operator==(const Header &, const Header &) = default;
};

// Reads the forms recorders and other software write: "Title", "0;Title//1-3;Group//4;Other//",
// "Title//1;Group//", "0;//2-5;Group//" (untitled), ";Empty group//". Track numbers in the header
// are 1-based; MdGroup's are 0-based.
Header parseHeader(const QString &raw);
// The header to write: just the title without groups (unless it has a ';', which would read
// as a group), else "0;Title//" followed by the groups.
QString formatHeader(const Header &header);
// The header with a new (cleaned) disc title, the groups kept; fits kMaxHeaderBytes by
// shortening the title. `truncated` says if it had to. If the groups alone are too long the
// result is still over kMaxHeaderBytes: the caller must not write it.
QString withTitle(const QString &raw, const QString &cleanTitle, bool *truncated = nullptr);

// --- Groups after editing the disc (M12b) ---
// The recorder keeps groups in the header, so it doesn't change them when tracks are deleted
// or moved: the caller writes the header these return (when it differs).

// After deleting the tracks `deleted` (0-based, as numbered before the delete; any order): ranges
// shift up and shrink; a group whose tracks are all gone is dropped. Groups without tracks stay.
Header headerAfterDelete(const Header &header, QList<int> deleted);
// After moving track `from` to position `to` (the track ends up at `to`, the others close up:
// netmd_move_track). Within its group the groups stay the same; past a group's edge the track
// leaves it; into the middle of a group it joins it. A group of just this track goes with it
// unless it lands inside another group (then the group is dropped).
Header headerAfterMove(const Header &header, int from, int to);

} // namespace mmm::MdTitle

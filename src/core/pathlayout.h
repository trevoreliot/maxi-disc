// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace mmm {

struct DeviceProfile;
struct TrackRow;

// Where a track goes on a sync target: a profile's layout pattern rendered for one track,
// safe for FAT32 / exFAT (the PLAN's USB targets). Pure functions.
//
// Pattern tokens: {albumartist} (falls back to the artist), {artist}, {album}, {title},
// {year}, {track}, {disc}; numbers can be zero-padded: {track:02}. "/" separates folders.
// [...] is optional: left out when a token inside it is empty ("[{disc}-]" disappears for
// single-disc albums). Tag values never create folders: "AC/DC" becomes "AC_DC".
namespace PathLayout {

// "" if the pattern is usable, else what's wrong with it.
QString validate(const QString &pattern);

// One file or folder name made safe: <>:"/\|?* and control characters replaced, leading
// dots and trailing dots/spaces removed, DOS device names (CON, NUL, COM1...) suffixed
// with "_", at most `maxLength` UTF-16 units (never splitting a character). "_" if empty.
QString sanitizeName(const QString &name, int maxLength = 120);

// A folder setting like "Music/Car" cleaned up: separators normalised, ".." and empty
// parts dropped, each part sanitised. "" = the device root.
QString cleanFolder(const QString &folder);

// The track's path relative to the device root: profile.musicDir + the rendered layout +
// the extension the device gets (copied or transcoded, DeviceProfiles::decide). Paths
// longer than `maxLength` UTF-16 units are shortened (file name first, extension kept).
// An invalid pattern renders with DeviceProfiles::defaultLayout().
QString render(const TrackRow &track, const DeviceProfile &profile, int maxLength = 240);

// A sample track for previews (multi-disc FLAC, "AC/DC"), so a preview shows folder
// sanitising, the disc prefix and whether FLAC is copied or transcoded.
TrackRow sampleTrack();

} // namespace PathLayout
} // namespace mmm

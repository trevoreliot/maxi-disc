// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace mmm::test {

// Writes a stereo 44.1 kHz sine tone of `durationMs` to `path`, encoded by the GStreamer
// description `encoder` (e.g. "flacenc", "wavenc", "lamemp3enc target=bitrate bitrate=320 cbr=true").
// `tags` (optional) is a GstStructure-style field list for `taginject`, e.g.
//   title="My Song",artist="Someone",track-number=(uint)3
// Returns an empty string on success, otherwise an error message.
QString generateTone(const QString &path, const QString &encoder, int durationMs,
                     const QString &tags = {});

} // namespace mmm::test

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

#include <atomic>

#include "deviceprofile.h"

namespace mmm {

// Makes the file a device gets when it isn't a plain copy (M9): an MP3 transcode and/or a
// copy with its cover art resized or stripped. GStreamer for audio, TagLib for tags.
// Stateless and thread-safe; runs on sync worker threads.
class Transcoder
{
public:
    struct Request
    {
        QString sourcePath;
        QString destPath;     // written directly (a private temp file in the cache)
        bool transcode = false;
        TranscodeTarget target = TranscodeTarget::Mp3V0;
        ArtworkPolicy art = ArtworkPolicy::Keep;
        QString artPath;      // folder art (beats embedded art, decisions/0003); "" = embedded
    };

    // Prepares `destPath`. Polls `cancel` (may be null); a canceled or failed run leaves no
    // file behind. Returns false with `error` on failure.
    static bool prepare(const Request &request, const std::atomic_bool *cancel, QString *error = nullptr);

    // Decodes `source` (any format GStreamer reads) and encodes it as an MP3 without tags.
    static bool transcodeToMp3(const QString &source, const QString &dest, TranscodeTarget target,
                               const std::atomic_bool *cancel, QString *error = nullptr);
};

} // namespace mmm

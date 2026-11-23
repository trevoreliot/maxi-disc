// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "mdupload.h"

namespace mmm {

// SP recording's audio (M11): decodes any file GStreamer reads to raw PCM, 44.1 kHz, 16-bit
// big-endian stereo (mono is doubled, other rates resampled), into `destination`. The recorder
// encodes SP itself. Thread-safe; polls `cancel` every 100 ms; leaves no file on failure.
class GstPcmEncoder final : public MdAudioEncoder
{
public:
    static constexpr qint64 kBytesPerSecond = 44100 * 2 * 2;

    bool encode(const QString &source, MdRecordMode mode, const QString &destination,
                const std::atomic_bool &cancel, MdEncodedAudio *out, QString *error) override;
};

} // namespace mmm

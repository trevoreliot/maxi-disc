// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "mdupload.h"

#include <memory>

namespace mmm {

// LP2 / LP4 recording's audio (M12): decodes to a 44.1 kHz WAV (GStreamer), runs atracdenc
// (vendored, a separate program: third_party/atracdenc) to make ATRAC3, and keeps the bare
// frames (the RIFF "data" chunk) in `destination`. LP2 = 384-byte frames (132 kbps, atracdenc's
// atrac3 default), LP4 = 192-byte joint-stereo frames (66 kbps, its atrac3_lp4). Polls `cancel`
// (kills atracdenc); leaves no file on failure.
class AtracEncoder final : public MdAudioEncoder
{
public:
    // `binary` empty: findBinary().
    explicit AtracEncoder(QString binary = {});

    bool encode(const QString &source, MdRecordMode mode, const QString &destination,
                const std::atomic_bool &cancel, MdEncodedAudio *out, QString *error) override;

    // Where atracdenc is: $MMM_ATRACDENC, next to the app, the build tree's, then PATH. "" if none.
    static QString findBinary();
    // Can LP be recorded? If not, `reason` says why.
    static bool isAvailable(QString *reason = nullptr, const QString &binary = {});

    // An ATRAC3 RIFF file (what atracdenc writes for .at3): checks the format (0x270, stereo,
    // 44.1 kHz, `frameBytes` per block) and finds the frames. False with `error` if it isn't one.
    static bool readAt3(const QString &path, int frameBytes, qint64 *dataOffset, qint64 *dataBytes, QString *error);

private:
    QString m_binary;
};

// The default encoder: SP through GstPcmEncoder, LP2 / LP4 through AtracEncoder with
// `atracdenc` (empty: findBinary()).
std::unique_ptr<MdAudioEncoder> makeDefaultMdEncoder(const QString &atracdenc = {});

} // namespace mmm

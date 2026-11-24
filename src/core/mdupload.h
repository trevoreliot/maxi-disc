// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QMetaType>
#include <QString>

#include <atomic>
#include <functional>

#include "libraryquery.h"
#include "mdtypes.h"

// Recording to a MiniDisc (M11): what gets recorded, the plan, progress. Plain values.
namespace mmm {

// SP: the recorder encodes PCM itself. LP2 / LP4 (M12): ATRAC3 made by atracdenc.
enum class MdRecordMode { Sp, Lp2, Lp4 };

// "SP", "LP2", "LP4".
inline QString mdModeName(MdRecordMode mode)
{
    switch (mode) {
    case MdRecordMode::Sp:
        return QStringLiteral("SP");
    case MdRecordMode::Lp2:
        return QStringLiteral("LP2");
    case MdRecordMode::Lp4:
        return QStringLiteral("LP4");
    }
    return {};
}

// How much music fits per unit of disc time (capacity is reported in SP time).
inline qint64 mdModeFactor(MdRecordMode mode)
{
    return mode == MdRecordMode::Lp4 ? 4 : mode == MdRecordMode::Lp2 ? 2 : 1;
}

// How a mode goes over NetMD (libnetmd / netmd-js values; LibNetMdBackend static_asserts them).
struct MdWireSpec
{
    int wireFormat = 0;  // PCM 0x00, LP2 0x94, LP4 0xa8
    int discFormat = 6;  // SP stereo 6, LP2 2, LP4 0
    int frameBytes = 4;  // the encoded audio's frame: SP a stereo sample, LP2 384, LP4 192 (ATRAC3)
};
inline MdWireSpec mdWireSpec(MdRecordMode mode)
{
    switch (mode) {
    case MdRecordMode::Sp:
        return {0x00, 6, 4};
    case MdRecordMode::Lp2:
        return {0x94, 2, 384};
    case MdRecordMode::Lp4:
        return {0xa8, 0, 192};
    }
    return {};
}

// Ready (M12c): prepared, waiting for the track before it to finish on the disc.
enum class MdItemState { Pending, Decoding, Ready, Uploading, Done, Failed, Skipped };

// One track to record.
struct MdUploadItem
{
    TrackRow track;
    QString title;               // as it goes on the disc (MdTitle::clean'ed, never empty)
    MdItemState state = MdItemState::Pending;
    QString error;
};

// Whether a list of tracks can be recorded to the disc in the recorder.
struct MdUploadPlan
{
    MdRecordMode mode = MdRecordMode::Sp;
    qint64 durationMs = 0;       // the tracks' total length
    qint64 neededMs = 0;         // disc time they take in this mode, in SP time (LP2 half, LP4 a quarter)
    qint64 availableMs = 0;      // SP time (× mdModeFactor(mode) = music that fits in this mode)
    bool canRecord = false;
    bool tight = false;          // fits, but only just (each track may round up a little)
    QString error;               // why not (empty if canRecord)
    QString warning;             // tight fit, unknown lengths
    QString suggestedDiscTitle;  // a blank untitled disc filled from one album: its title
};

// Audio prepared for the recorder: SP = raw PCM, 44.1 kHz, 16-bit big-endian stereo; LP2 / LP4
// = raw ATRAC3 frames (384 / 192 bytes, 1024 samples each).
struct MdEncodedAudio
{
    MdRecordMode mode = MdRecordMode::Sp;
    QString path;
    qint64 bytes = 0;
    qint64 durationMs = 0;
};

// Makes MdEncodedAudio from a library file (M11: GstPcmEncoder; M12 adds ATRAC for LP2/LP4).
// Runs on the NetMD encode thread (M12c: MdEncodeWorker, while a track uploads); polls `cancel`. Stopped by `cancel`: returns false with
// `error` == kCanceled (a real failure has another message).
inline const QString kEncodeCanceled = QStringLiteral("canceled");
class MdAudioEncoder
{
public:
    virtual ~MdAudioEncoder() = default;
    virtual bool encode(const QString &source, MdRecordMode mode, const QString &destination,
                        const std::atomic_bool &cancel, MdEncodedAudio *out, QString *error) = 0;
};

using MdAudioEncoderFactory = std::function<std::unique_ptr<MdAudioEncoder>()>;

// Upload progress of the current track.
struct MdTransferProgress
{
    qint64 bytesSent = 0;
    qint64 bytesTotal = 0;
};

} // namespace mmm

Q_DECLARE_METATYPE(mmm::MdTransferProgress)
Q_DECLARE_METATYPE(mmm::MdEncodedAudio)

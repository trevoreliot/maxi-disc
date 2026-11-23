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

// SP now; LP2 / LP4 arrive with M12 (atracdenc).
enum class MdRecordMode { Sp, Lp2, Lp4 };

enum class MdItemState { Pending, Decoding, Uploading, Done, Failed, Skipped };

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
    qint64 neededMs = 0;         // disc time they take in this mode (LP2 half, LP4 a quarter)
    qint64 availableMs = 0;
    bool canRecord = false;
    bool tight = false;          // fits, but only just (each track may round up a little)
    QString error;               // why not (empty if canRecord)
    QString warning;             // tight fit, unknown lengths
    QString suggestedDiscTitle;  // a blank untitled disc filled from one album: its title
};

// Audio prepared for the recorder: SP = raw PCM, 44.1 kHz, 16-bit big-endian stereo.
struct MdEncodedAudio
{
    MdRecordMode mode = MdRecordMode::Sp;
    QString path;
    qint64 bytes = 0;
    qint64 durationMs = 0;
};

// Makes MdEncodedAudio from a library file (M11: GstPcmEncoder; M12 adds ATRAC for LP2/LP4).
// Runs on the NetMD worker thread; polls `cancel`. Stopped by `cancel`: returns false with
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

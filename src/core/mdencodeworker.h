// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

#include "mdupload.h"

class QTemporaryDir;

namespace mmm {

// Prepares tracks for the recorder on its own thread (M12c), so the next track is decoded /
// encoded while the NetMD worker uploads the one before it. One track at a time; NetMdManager
// decides which and when. Lives on the "netmd-encode" thread; reports through `encoded` (queued).
class MdEncodeWorker : public QObject
{
    Q_OBJECT

public:
    enum Outcome { Ok, Canceled, Failed };

    explicit MdEncodeWorker(MdAudioEncoderFactory factory) : m_factory(std::move(factory)) {}

    // Prepares `source` as `destination` in `mode`. `run` and `index` go back with the result.
    // `cancel` is the recording's (stops it at once); `dir`, the recording's temporary folder,
    // is kept until this returns. A file that isn't handed over (failed, canceled) is removed.
    void encodeOne(quint64 run, int index, const QString &source, const QString &destination, MdRecordMode mode,
                   const std::shared_ptr<std::atomic_bool> &cancel, const std::shared_ptr<QTemporaryDir> &dir);

Q_SIGNALS:
    void encoded(quint64 run, int index, int outcome, const mmm::MdEncodedAudio &audio, const QString &error);

private:
    MdAudioEncoderFactory m_factory;
};

} // namespace mmm

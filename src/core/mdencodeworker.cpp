// SPDX-License-Identifier: GPL-3.0-or-later
#include "mdencodeworker.h"

#include <QFile>
#include <QTemporaryDir>

namespace mmm {

void MdEncodeWorker::encodeOne(quint64 run, int index, const QString &source, const QString &destination,
                               MdRecordMode mode, const std::shared_ptr<std::atomic_bool> &cancel,
                               const std::shared_ptr<QTemporaryDir> &dir)
{
    Q_ASSERT(cancel && dir);
    if (cancel->load()) {
        Q_EMIT encoded(run, index, Canceled, {}, {});
        return;
    }
    MdEncodedAudio audio;
    QString error;
    const bool ok = m_factory()->encode(source, mode, destination, *cancel, &audio, &error);
    if (!ok || cancel->load()) {
        QFile::remove(destination);
        const bool canceled = cancel->load() || error == kEncodeCanceled;
        Q_EMIT encoded(run, index, canceled ? Canceled : Failed, {}, canceled ? QString() : error);
        return;
    }
    Q_EMIT encoded(run, index, Ok, audio, {});
}

} // namespace mmm

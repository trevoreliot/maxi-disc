// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QThreadPool>

#include <atomic>
#include <memory>

#include "device.h"
#include "synctypes.h"

namespace mmm {

// Plans and runs device syncs off the calling thread (M9): the job abstraction (plan, run,
// progress, cancel) that MiniDisc uploads (M10–M12) will share. One thing at a time; each
// run opens its own Database on the worker. Results arrive as queued signals.
class SyncJob : public QObject
{
    Q_OBJECT

public:
    struct Options
    {
        QString databasePath;
        QString cacheDir;          // transcode cache ("" = AppPaths::transcodeCacheDir())
        qint64 cacheMaxBytes = 0;  // 0 = TranscodeCache::kDefaultMaxBytes
        int prepareThreads = 0;    // 0 = automatic
    };

    explicit SyncJob(Options options, QObject *parent = nullptr);
    ~SyncJob() override; // cancels and waits for the worker

    bool isBusy() const { return m_busy; }

    // Starts planning (planReady follows). Ignored while busy.
    bool plan(const Device &device, const DeviceProfile &profile);
    // Starts running the plan (progressChanged…, then finished). Ignored while busy.
    bool run(const SyncPlan &plan);
    void cancel();

Q_SIGNALS:
    void busyChanged();
    void planReady(const mmm::SyncPlan &plan);
    void progressChanged(const mmm::SyncProgress &progress);
    void finished(const mmm::SyncResult &result);

private:
    void setBusy(bool busy);

    Options m_options;
    QThreadPool m_pool;
    std::shared_ptr<std::atomic_bool> m_cancel;
    bool m_busy = false;
};

} // namespace mmm

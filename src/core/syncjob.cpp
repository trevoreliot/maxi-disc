// SPDX-License-Identifier: GPL-3.0-or-later
#include "syncjob.h"

#include "apppaths.h"
#include "database.h"
#include "syncplanner.h"
#include "transcodecache.h"
#include "usbsync.h"

#include <QLoggingCategory>
#include <QMetaObject>

Q_DECLARE_LOGGING_CATEGORY(lcSync)

namespace mmm {

SyncJob::SyncJob(Options options, QObject *parent)
    : QObject(parent), m_options(std::move(options)), m_cancel(std::make_shared<std::atomic_bool>(false))
{
    if (m_options.cacheDir.isEmpty())
        m_options.cacheDir = AppPaths::transcodeCacheDir();
    if (m_options.cacheMaxBytes <= 0)
        m_options.cacheMaxBytes = TranscodeCache::kDefaultMaxBytes;
    m_pool.setMaxThreadCount(1);
    qRegisterMetaType<mmm::SyncPlan>();
    qRegisterMetaType<mmm::SyncProgress>();
    qRegisterMetaType<mmm::SyncResult>();
}

SyncJob::~SyncJob()
{
    *m_cancel = true;
    m_pool.waitForDone();
}

void SyncJob::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    Q_EMIT busyChanged();
}

void SyncJob::cancel()
{
    *m_cancel = true;
}

bool SyncJob::plan(const Device &device, const DeviceProfile &profile)
{
    if (m_busy)
        return false;
    *m_cancel = false;
    setBusy(true);
    m_pool.start([this, options = m_options, device, profile, cancel = m_cancel] {
        SyncPlan plan;
        Database db;
        QString error;
        if (!db.open(options.databasePath, &error)) {
            plan.deviceId = profile.deviceId;
            plan.error = QStringLiteral("Cannot open the library: %1").arg(error);
        } else {
            const TranscodeCache cache(options.cacheDir, options.cacheMaxBytes);
            plan = SyncPlanner::plan(db, device, profile, &cache, cancel.get());
        }
        QMetaObject::invokeMethod(this, [this, plan] {
            setBusy(false);
            Q_EMIT planReady(plan);
        }, Qt::QueuedConnection);
    });
    return true;
}

bool SyncJob::run(const SyncPlan &plan)
{
    if (m_busy)
        return false;
    *m_cancel = false;
    setBusy(true);
    m_pool.start([this, options = m_options, plan, cancel = m_cancel] {
        SyncResult result;
        Database db;
        QString error;
        if (!db.open(options.databasePath, &error)) {
            result.error = QStringLiteral("Cannot open the library: %1").arg(error);
        } else {
            TranscodeCache cache(options.cacheDir, options.cacheMaxBytes);
            cache.evict(); // nothing else uses the cache while this job runs
            result = UsbSync::run(plan, db, cache, *cancel, [this](const SyncProgress &p) {
                QMetaObject::invokeMethod(this, [this, p] { Q_EMIT progressChanged(p); }, Qt::QueuedConnection);
            }, options.prepareThreads);
            cache.evict();
        }
        qCInfo(lcSync).noquote() << "sync of" << plan.deviceId << (result.ok ? "done" : result.canceled ? "canceled" : "failed")
                                 << "- added" << result.added << "updated" << result.updated << "moved" << result.moved
                                 << "removed" << result.removed << "failed" << result.failed << result.error;
        QMetaObject::invokeMethod(this, [this, result] {
            setBusy(false);
            Q_EMIT finished(result);
        }, Qt::QueuedConnection);
    });
    return true;
}

} // namespace mmm

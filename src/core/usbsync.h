// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <functional>

#include "synctypes.h"

namespace mmm {

class Database;
class TranscodeCache;

// Runs a SyncPlan on a mounted USB drive or DAP (M9). Runs on a worker thread with that
// thread's Database; files needing a transcode or new art are prepared on a few extra
// threads (through the TranscodeCache) while this thread copies, in plan order.
//
// Every file is written as a hidden temp file, flushed, then renamed into place, and the
// manifest row is committed right after: a cancel, a full device or an unplugged cable
// loses at most the file in progress, and the next plan picks up from there.
class UsbSync
{
public:
    using ProgressFn = std::function<void(const SyncProgress &)>;

    static SyncResult run(const SyncPlan &plan, Database &db, TranscodeCache &cache, const std::atomic_bool &cancel,
                          const ProgressFn &progress = {}, int prepareThreads = 0);
};

} // namespace mmm

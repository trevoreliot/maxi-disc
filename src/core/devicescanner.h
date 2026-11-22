// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <atomic>

namespace mmm {

// A file on a device, as stat() saw it.
struct DeviceFile
{
    QString relPath;     // from the mount point, '/'-separated, as on the device
    qint64 size = 0;
    qint64 mtimeMs = 0;
    friend bool operator==(const DeviceFile &, const DeviceFile &) = default;
};

struct DeviceScan
{
    QHash<QString, DeviceFile> files;  // keyed by Database::pathKey(relPath) (case-folded)
    QStringList tempFiles;             // leftovers of an interrupted sync (relative paths)
    QString error;                     // the device couldn't be read
    bool canceled = false;
};

// Lists the files under a device's music folder (M9: what a sync plan compares with; M9b:
// what the on-device browser shows). Stat only: no file is opened. Thread-safe.
class DeviceScanner
{
public:
    // Name prefix of the files a sync writes before renaming them into place.
    static QString tempPrefix() { return QStringLiteral(".mmm-part-"); }

    // Walks `<mountPoint>/<musicDir>` (the whole device if musicDir is ""), listing symlinks
    // but not following them, and also stats `extraRelPaths` (manifest files outside that
    // folder, e.g. after the profile's music folder changed). Polls `cancel` (may be null).
    static DeviceScan scan(const QString &mountPoint, const QString &musicDir, const QStringList &extraRelPaths = {},
                           const std::atomic_bool *cancel = nullptr);
};

} // namespace mmm

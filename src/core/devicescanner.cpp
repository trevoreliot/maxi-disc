// SPDX-License-Identifier: GPL-3.0-or-later
#include "devicescanner.h"

#include "database.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

namespace mmm {

DeviceScan DeviceScanner::scan(const QString &mountPoint, const QString &musicDir, const QStringList &extraRelPaths,
                               const std::atomic_bool *cancel)
{
    DeviceScan result;
    const QDir root(mountPoint);
    if (mountPoint.isEmpty() || !root.exists()) {
        result.error = QStringLiteral("The device isn't mounted.");
        return result;
    }
    auto add = [&](const QFileInfo &info) {
        const QString rel = root.relativeFilePath(info.filePath());
        if (info.fileName().startsWith(tempPrefix())) {
            result.tempFiles << rel;
            return;
        }
        result.files.insert(Database::pathKey(rel),
                            DeviceFile{rel, info.size(), info.lastModified().toMSecsSinceEpoch()});
    };

    const QString top = musicDir.isEmpty() ? mountPoint : root.filePath(musicDir);
    if (QFileInfo(top).isDir()) {
        // Symlinks are listed (a name the user uses) but not followed into folders; System
        // brings broken ones too.
        QDirIterator it(top, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        int n = 0;
        while (it.hasNext()) {
            it.next();
            if ((++n & 127) == 0 && cancel && cancel->load()) {
                result.canceled = true;
                return result;
            }
            add(it.fileInfo());
        }
    }
    for (const QString &rel : extraRelPaths) {
        if (result.files.contains(Database::pathKey(rel)))
            continue;
        const QFileInfo info(root.filePath(rel));
        if (info.exists() || info.isSymLink())
            add(info);
    }
    return result;
}

} // namespace mmm

// SPDX-License-Identifier: GPL-3.0-or-later
#include "apppaths.h"

#include "appinfo.h"

#include <QDir>
#include <QStandardPaths>

namespace mmm::AppPaths {

QString defaultDatabasePath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir(base).filePath(AppInfo::name() + QStringLiteral("/library.db"));
}

QString coverCacheDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    return QDir(base).filePath(AppInfo::name() + QStringLiteral("/covers"));
}

QString artCacheDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    return QDir(base).filePath(AppInfo::name() + QStringLiteral("/art"));
}

QString transcodeCacheDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    return QDir(base).filePath(AppInfo::name() + QStringLiteral("/transcode"));
}

QString mdRecordDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    return QDir(base).filePath(AppInfo::name() + QStringLiteral("/md-record"));
}

} // namespace mmm::AppPaths

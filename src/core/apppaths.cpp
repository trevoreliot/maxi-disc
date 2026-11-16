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

} // namespace mmm::AppPaths

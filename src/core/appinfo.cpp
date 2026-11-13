// SPDX-License-Identifier: GPL-3.0-or-later
#include "appinfo.h"

#include "mmm/version.h"

namespace mmm {

QString AppInfo::name()
{
    return QStringLiteral("music-md-manager");
}

QString AppInfo::displayName()
{
    return QStringLiteral("Music MD Manager");
}

QString AppInfo::organization()
{
    return QStringLiteral("music-md-manager");
}

QString AppInfo::version()
{
    return QStringLiteral(MMM_VERSION_STRING);
}

} // namespace mmm

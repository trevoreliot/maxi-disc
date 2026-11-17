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

QString formatDuration(qint64 ms)
{
    if (ms < 0)
        return QStringLiteral("--:--");
    const qint64 total = ms / 1000;
    const qint64 h = total / 3600;
    const qint64 m = (total / 60) % 60;
    const qint64 s = total % 60;
    const QLatin1Char zero('0');
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, zero).arg(s, 2, 10, zero);
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, zero);
}

} // namespace mmm

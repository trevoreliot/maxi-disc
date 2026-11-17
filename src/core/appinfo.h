// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace mmm {

// Static application identity used by QCoreApplication, settings, MPRIS, etc.
struct AppInfo
{
    static QString name();
    static QString displayName();
    static QString organization();
    static QString version();
};

// "3:07", "1:02:03"; negative → "--:--". Shared by the UI, MPRIS and other clients.
QString formatDuration(qint64 ms);

} // namespace mmm

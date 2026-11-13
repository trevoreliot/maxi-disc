// SPDX-License-Identifier: GPL-3.0-or-later
#include "appbridge.h"

#include "appinfo.h"

AppBridge::AppBridge(QObject *parent)
    : QObject(parent)
{
}

QString AppBridge::displayName() const
{
    return mmm::AppInfo::displayName();
}

QString AppBridge::version() const
{
    return mmm::AppInfo::version();
}

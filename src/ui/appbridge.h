// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

// Exposes static app info to QML as the `AppBridge` singleton.
class AppBridge : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString displayName READ displayName CONSTANT FINAL)
    Q_PROPERTY(QString version READ version CONSTANT FINAL)

public:
    explicit AppBridge(QObject *parent = nullptr);

    QString displayName() const;
    QString version() const;

    Q_INVOKABLE QString formatDuration(qint64 ms) const;
};

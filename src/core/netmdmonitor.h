// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QTimer>

#include "mdtypes.h"

struct libusb_context;

namespace mmm {

// Notices NetMD recorders being plugged in and out (M10): lists USB devices every couple of
// seconds and keeps the ones libnetmd knows (by vendor/product id). Listing is a quick read of
// what the kernel already knows, with its own libusb context, so it never touches a recorder
// that's open, and works without access rights (the device still shows; opening it fails).
class NetMdMonitor : public QObject
{
    Q_OBJECT

public:
    explicit NetMdMonitor(int intervalMs = 2000, QObject *parent = nullptr);
    ~NetMdMonitor() override;

    void start();
    QList<NetMdDevice> devices() const { return m_devices; }
    void checkNow();

    // "Sony MZ-NE410" for a known id, else "NetMD recorder".
    static QString modelName(quint16 vendorId, quint16 productId);

Q_SIGNALS:
    void devicesChanged();

private:
    libusb_context *m_ctx = nullptr;
    QTimer m_timer;
    QList<NetMdDevice> m_devices;
};

} // namespace mmm

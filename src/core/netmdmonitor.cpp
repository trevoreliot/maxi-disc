// SPDX-License-Identifier: GPL-3.0-or-later
#include "netmdmonitor.h"

#include <QHash>
#include <QLoggingCategory>

#include <libusb.h>

extern "C" {
#include <netmd_dev.h>
}

#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(lcNetMd)

namespace mmm {

NetMdMonitor::NetMdMonitor(int intervalMs, QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(intervalMs);
    connect(&m_timer, &QTimer::timeout, this, &NetMdMonitor::checkNow);
}

NetMdMonitor::~NetMdMonitor()
{
    if (m_ctx)
        libusb_exit(m_ctx);
}

void NetMdMonitor::start()
{
    if (!m_ctx && libusb_init(&m_ctx) != 0) {
        qCWarning(lcNetMd) << "libusb unavailable: NetMD recorders won't be detected";
        m_ctx = nullptr;
        return;
    }
    checkNow();
    m_timer.start();
}

QString NetMdMonitor::modelName(quint16 vendorId, quint16 productId)
{
    // The common ones (libnetmd's table, netmd_dev.c); the rest are "NetMD recorder".
    static const QHash<quint32, QString> names{
        {0x054c0075, QStringLiteral("Sony MZ-N1")},          {0x054c0084, QStringLiteral("Sony MZ-N505")},
        {0x054c0085, QStringLiteral("Sony MZ-S1")},          {0x054c0086, QStringLiteral("Sony MZ-N707")},
        {0x054c00c6, QStringLiteral("Sony MZ-N10")},         {0x054c00c7, QStringLiteral("Sony MZ-N910")},
        {0x054c00c8, QStringLiteral("Sony MZ-N710")},        {0x054c00c9, QStringLiteral("Sony MZ-N510")},
        {0x054c00ca, QStringLiteral("Sony MZ-NE410")},       {0x054c00eb, QStringLiteral("Sony MZ-NE810")},
        {0x054c0188, QStringLiteral("Sony MZ-N920")},        {0x054c0081, QStringLiteral("Sony MDS-JE780")},
        {0x054c0113, QStringLiteral("Aiwa AM-NX1")},         {0x054c014c, QStringLiteral("Aiwa AM-NX9")},
        {0x04dd7202, QStringLiteral("Sharp IM-MT880H")},     {0x04dd9013, QStringLiteral("Sharp IM-DR410")},
        {0x04dd9014, QStringLiteral("Sharp IM-DR420")},
    };
    return names.value((quint32(vendorId) << 16) | productId, QStringLiteral("NetMD recorder"));
}

void NetMdMonitor::checkNow()
{
    if (!m_ctx)
        return;
    libusb_device **list = nullptr;
    const ssize_t count = libusb_get_device_list(m_ctx, &list);
    QList<NetMdDevice> found;
    for (ssize_t i = 0; i < count; ++i) {
        libusb_device_descriptor desc{};
        if (libusb_get_device_descriptor(list[i], &desc) != 0 || !netmd_is_known_device(desc.idVendor, desc.idProduct))
            continue;
        NetMdDevice d;
        d.vendorId = desc.idVendor;
        d.productId = desc.idProduct;
        d.bus = libusb_get_bus_number(list[i]);
        d.address = libusb_get_device_address(list[i]);
        d.id = QStringLiteral("netmd:%1:%2:%3:%4")
                   .arg(d.vendorId, 4, 16, QLatin1Char('0'))
                   .arg(d.productId, 4, 16, QLatin1Char('0'))
                   .arg(d.bus)
                   .arg(d.address);
        d.name = modelName(d.vendorId, d.productId);
        found << d;
    }
    if (count >= 0)
        libusb_free_device_list(list, 1);
    std::sort(found.begin(), found.end(), [](const NetMdDevice &a, const NetMdDevice &b) { return a.id < b.id; });
    if (found == m_devices)
        return;
    m_devices = found;
    Q_EMIT devicesChanged();
}

} // namespace mmm

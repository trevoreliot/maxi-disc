// SPDX-License-Identifier: GPL-3.0-or-later
// The only file that talks to libnetmd (third_party/libnetmd, see UPSTREAM.md there).
#include "netmdbackend.h"

#include <QLoggingCategory>

#include <libusb.h>

extern "C" {
#include <libnetmd.h>
#include <log.h>
}

#include <array>

Q_LOGGING_CATEGORY(lcNetMd, "mmm.netmd")

namespace mmm {

namespace {

constexpr int kMaxTracks = 255; // the TOC's limit

QString fromDisc(const char *bytes, int length)
{
    // NetMD titles are half-width ASCII (Japanese decks add katakana; see follow-ups).
    return QString::fromLatin1(bytes, length);
}

class LibNetMdBackend final : public NetMdBackend
{
public:
    LibNetMdBackend()
    {
        // libnetmd logs every USB exchange at its higher levels: only when asked for.
        netmd_set_log_level(qEnvironmentVariableIntValue("MMM_NETMD_DEBUG") > 0 ? NETMD_LOG_ALL : NETMD_LOG_NONE);
    }
    ~LibNetMdBackend() override { close(); }

    NetMdError open(const NetMdDevice &device) override
    {
        close();
        if (netmd_init(&m_list) != NETMD_NO_ERROR) {
            m_list = nullptr;
            return NetMdError::Failed;
        }
        netmd_device *match = nullptr;
        for (netmd_device *d = m_list; d; d = d->link) {
            if (libusb_get_bus_number(d->usb_dev) == device.bus && libusb_get_device_address(d->usb_dev) == device.address)
                match = d;
        }
        if (!match) {
            netmd_clean(&m_list);
            return NetMdError::NotFound;
        }
        if (netmd_open(match, &m_handle) != NETMD_NO_ERROR || !m_handle) {
            const int usb = netmd_last_usb_error();
            qCInfo(lcNetMd) << "netmd_open failed:" << libusb_error_name(usb);
            m_handle = nullptr;
            netmd_clean(&m_list);
            switch (usb) {
            case LIBUSB_ERROR_ACCESS:
                return NetMdError::PermissionDenied;
            case LIBUSB_ERROR_BUSY:
                return NetMdError::Busy;
            case LIBUSB_ERROR_NO_DEVICE:
            case LIBUSB_ERROR_NOT_FOUND:
                return NetMdError::NotFound;
            default:
                return NetMdError::Failed;
            }
        }
        return NetMdError::None;
    }

    void close() override
    {
        if (m_handle) {
            netmd_close(m_handle);
            m_handle = nullptr;
        }
        if (m_list)
            netmd_clean(&m_list);
    }

    bool isOpen() const override { return m_handle != nullptr; }

    NetMdError readDisc(MdDisc *disc) override
    {
        if (!m_handle)
            return NetMdError::NotFound;
        MdDisc out;
        // Capacity first: without a disc the recorder rejects it (patched libnetmd reports that).
        netmd_disc_capacity cap{};
        switch (netmd_get_disc_capacity(m_handle, &cap)) {
        case NETMD_NO_ERROR:
            break;
        case NETMD_ERROR:
            return NetMdError::NoDisc; // the recorder rejected it
        default:
            return NetMdError::Failed; // the transfer failed
        }
        auto ms = [](const netmd_time &t) {
            return ((qint64(t.hour) * 60 + t.minute) * 60 + t.second) * 1000 + qint64(t.frame) * 1000 / 512;
        };
        out.capacity = MdCapacity{ms(cap.recorded), ms(cap.total), ms(cap.available), true};
        if (const NetMdError e = readHeader(&out.rawHeader); e != NetMdError::None)
            return e;
        // Track count: titles are asked for until the recorder says there's no such track
        // (what netmdcli does; libnetmd has no track-count call).
        std::array<char, 1024> buf{};
        for (int i = 0; i < kMaxTracks; ++i) {
            buf.fill(0);
            const int n = netmd_request_title(m_handle, static_cast<uint16_t>(i), buf.data(), buf.size());
            if (n < 0)
                break;
            MdTrack t;
            t.index = i;
            t.title = fromDisc(buf.data(), n);
            netmd_track time{};
            if (netmd_request_track_time(m_handle, static_cast<uint16_t>(i), &time))
                t.durationMs = ((qint64(time.hour) * 60 + time.minute) * 60 + time.second) * 1000 + qint64(time.tenth) * 100;
            unsigned char encoding = 0;
            unsigned char channel = 0;
            netmd_request_track_bitrate(m_handle, static_cast<uint16_t>(i), &encoding, &channel);
            t.encoding = encoding == NETMD_ENCODING_SP ? MdEncoding::Sp
                : encoding == NETMD_ENCODING_LP2       ? MdEncoding::Lp2
                : encoding == NETMD_ENCODING_LP4       ? MdEncoding::Lp4
                                                       : MdEncoding::Unknown;
            t.mono = channel == NETMD_CHANNELS_MONO;
            unsigned char flags = 0;
            if (netmd_request_track_flags(m_handle, static_cast<uint16_t>(i), &flags) >= 0)
                t.protectedTrack = flags == NETMD_TRACK_FLAG_PROTECTED;
            out.tracks << t;
        }
        *disc = out;
        return NetMdError::None;
    }

    NetMdError readHeader(QString *raw) override
    {
        if (!m_handle)
            return NetMdError::NotFound;
        std::array<char, 512> buf{};
        const int n = netmd_request_disc_title(m_handle, buf.data(), buf.size());
        if (n == -2)
            return NetMdError::Failed; // a transfer error: not the same as "untitled"
        *raw = n > 0 ? fromDisc(buf.data(), n) : QString(); // -1: untitled
        return NetMdError::None;
    }

    NetMdError readTrackTitle(int index, QString *title) override
    {
        if (!m_handle)
            return NetMdError::NotFound;
        std::array<char, 1024> buf{};
        const int n = netmd_request_title(m_handle, static_cast<uint16_t>(index), buf.data(), buf.size());
        if (n < 0)
            return NetMdError::Failed;
        *title = fromDisc(buf.data(), n);
        return NetMdError::None;
    }

    // Title changes are bracketed by "cache TOC" / "sync TOC" like libnetmd's own write path
    // and Web MiniDisc do (the recorder commits the TOC on sync).
    NetMdError writeHeader(const QString &raw) override
    {
        if (!m_handle)
            return NetMdError::NotFound;
        QByteArray bytes = raw.toLatin1();
        if (bytes.size() > 255)
            return NetMdError::Rejected; // the command has a one-byte length
        netmd_cache_toc(m_handle);
        const int result = netmd_set_disc_title(m_handle, bytes.data(), static_cast<size_t>(bytes.size()));
        const int synced = netmd_sync_toc(m_handle);
        qCDebug(lcNetMd) << "set disc title" << raw << "->" << result << "sync" << synced;
        if (result < 0)
            return NetMdError::Rejected;
        return synced < 0 ? NetMdError::Failed : NetMdError::None;
    }

    NetMdError writeTrackTitle(int index, const QString &title) override
    {
        if (!m_handle)
            return NetMdError::NotFound;
        const QByteArray bytes = title.toLatin1();
        if (bytes.size() > 254)
            return NetMdError::Rejected;
        netmd_cache_toc(m_handle);
        const int ok = netmd_set_title(m_handle, static_cast<uint16_t>(index), bytes.constData());
        const int synced = netmd_sync_toc(m_handle);
        qCDebug(lcNetMd) << "set title" << index << title << "->" << ok << "sync" << synced;
        if (ok != 1)
            return NetMdError::Rejected;
        return synced < 0 ? NetMdError::Failed : NetMdError::None;
    }

private:
    netmd_device *m_list = nullptr;
    netmd_dev_handle *m_handle = nullptr;
};

} // namespace

std::unique_ptr<NetMdBackend> makeLibNetMdBackend()
{
    return std::make_unique<LibNetMdBackend>();
}

} // namespace mmm

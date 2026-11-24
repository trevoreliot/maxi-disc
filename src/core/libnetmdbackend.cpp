// SPDX-License-Identifier: GPL-3.0-or-later
// The only file that talks to libnetmd (third_party/libnetmd, see UPSTREAM.md there).
#include "netmdbackend.h"

#include <QLoggingCategory>

#include <libusb.h>

extern "C" {
#include <libnetmd.h>
#include <log.h>
#include <secure.h>
}

#include <QFile>

#include <array>
#include <vector>

Q_LOGGING_CATEGORY(lcNetMd, "mmm.netmd")

namespace mmm {

namespace {

constexpr int kMaxTracks = 255; // the TOC's limit

// Upload key material: the published constants every NetMD host uses (from linux-minidisc's
// netmdcli, GPL-2.0-or-later; Web MiniDisc uses the same).
constexpr std::array<unsigned char, 32> kEkbChain{0x25, 0x45, 0x06, 0x4d, 0xea, 0xca, 0x14, 0xf9, 0x96, 0xbd, 0xc8,
                                                  0xa4, 0x06, 0xc2, 0x2b, 0x81, 0x49, 0xba, 0xf0, 0xdf, 0x26, 0x9d,
                                                  0xb7, 0x1d, 0x49, 0xba, 0xf0, 0xdf, 0x26, 0x9d, 0xb7, 0x1d};
constexpr std::array<unsigned char, 24> kEkbSignature{0xe8, 0xef, 0x73, 0x45, 0x8d, 0x5b, 0x8b, 0xf8,
                                                      0xe8, 0xef, 0x73, 0x45, 0x8d, 0x5b, 0x8b, 0xf8,
                                                      0x38, 0x5b, 0x49, 0x36, 0x7b, 0x42, 0x0c, 0x58};
constexpr std::array<unsigned char, 16> kRootKey{0x13, 0x37, 0x13, 0x37, 0x13, 0x37, 0x13, 0x37,
                                                 0x13, 0x37, 0x13, 0x37, 0x13, 0x37, 0x13, 0x37};
constexpr std::array<unsigned char, 8> kKek{0x14, 0xe3, 0x83, 0x4e, 0xe2, 0xd3, 0xcc, 0xa5};
constexpr std::array<unsigned char, 20> kContentId{0x01, 0x0F, 0x50, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x48,
                                                   0xA2, 0x8D, 0x3E, 0x1A, 0x3B, 0x0C, 0x44, 0xAF, 0x2f, 0xa0};

int uploadProgress(size_t sent, size_t total, void *user)
{
    const auto *progress = static_cast<const NetMdBackend::UploadProgress *>(user);
    if (progress && *progress)
        (*progress)(static_cast<qint64>(sent), static_cast<qint64>(total));
    return 0; // never stop part-way (see NetMdBackend::uploadTrack)
}

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
        netmd_set_last_usb_error(0);
        return NetMdError::None;
    }

    void close() override
    {
        if (m_handle) {
            // After a USB error (not an unplug) a recorder can stop answering until it's replugged:
            // a port reset does the same, so the next open finds it working.
            const int usb = netmd_last_usb_error();
            if (usb != 0 && usb != LIBUSB_ERROR_NO_DEVICE) {
                const int reset = libusb_reset_device(reinterpret_cast<libusb_device_handle *>(m_handle));
                qCInfo(lcNetMd) << "USB error" << libusb_error_name(usb) << "earlier: reset the recorder:"
                                << libusb_error_name(reset);
            }
            netmd_close(m_handle);
            m_handle = nullptr;
            netmd_set_last_usb_error(0);
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

    // The sequence linux-minidisc's netmdcli "send" uses: secure session, key exchange,
    // download setup, the encrypted track, then title and commit.
    NetMdError uploadTrack(const MdEncodedAudio &audio, const QString &title, const UploadProgress &progress,
                           int *trackIndex) override
    {
        if (!m_handle)
            return NetMdError::NotFound;
        // How the mode goes over the wire (the values must be libnetmd's: checked here).
        static_assert(NETMD_WIREFORMAT_PCM == 0x00 && NETMD_WIREFORMAT_LP2 == 0x94 && NETMD_WIREFORMAT_LP4 == 0xa8);
        static_assert(NETMD_DISKFORMAT_SP_STEREO == 6 && NETMD_DISKFORMAT_LP2 == 2 && NETMD_DISKFORMAT_LP4 == 0);
        const MdWireSpec spec = mdWireSpec(audio.mode);
        if (audio.bytes <= 0 || audio.bytes % spec.frameBytes != 0) {
            qCWarning(lcNetMd) << "upload:" << audio.bytes << "bytes isn't whole" << mdModeName(audio.mode) << "frames";
            return NetMdError::Failed;
        }
        const auto wireformat = static_cast<netmd_wireformat>(spec.wireFormat);
        // The audio (PCM or ATRAC3 frames), with room after it: netmd_prepare_packets pads the last block (netmdcli
        // allocates 2048 spare bytes too).
        std::vector<unsigned char> data;
        {
            QFile f(audio.path);
            if (!f.open(QIODevice::ReadOnly))
                return NetMdError::Failed;
            const qint64 size = f.size();
            try {
                data.assign(static_cast<size_t>(size) + 2048, 0);
            } catch (const std::bad_alloc &) {
                // libnetmd encrypts a whole track in memory (twice its size at the peak).
                qCWarning(lcNetMd) << "not enough memory for a track of" << size << "bytes";
                return NetMdError::Failed;
            }
            if (f.read(reinterpret_cast<char *>(data.data()), size) != size)
                return NetMdError::Failed;
        }
        const size_t pcmBytes = data.size() - 2048;

        // Every step is logged (mmm.netmd): which one a recorder refuses is what to look at.
        auto check = [](const char *step, netmd_error e) {
            if (e == NETMD_NO_ERROR)
                qCDebug(lcNetMd) << "upload:" << step << "ok";
            else
                qCWarning(lcNetMd) << "upload:" << step << "failed:" << netmd_strerror(e) << "(netmd error" << int(e) << ")";
            return e == NETMD_NO_ERROR;
        };
        check("leave stale session (an error is normal)", netmd_secure_leave_session(m_handle));
        check("set track protection", netmd_secure_set_track_protection(m_handle, 0x01));
        auto leave = [this, &check] {
            check("forget session key", netmd_secure_session_key_forget(m_handle));
            check("leave session", netmd_secure_leave_session(m_handle));
        };
        if (!check("enter session", netmd_secure_enter_session(m_handle))) {
            netmd_secure_leave_session(m_handle);
            return NetMdError::Rejected;
        }
        std::array<unsigned char, 16> key0{}, key1{};
        std::copy_n(kEkbChain.begin(), 16, key0.begin());
        std::copy_n(kEkbChain.begin() + 16, 16, key1.begin());
        std::array<unsigned char, 24> signature = kEkbSignature;
        netmd_keychain second{reinterpret_cast<char *>(key1.data()), nullptr};
        netmd_keychain first{reinterpret_cast<char *>(key0.data()), &second};
        netmd_ekb ekb{0x26422642, &first, 9, reinterpret_cast<char *>(signature.data())};
        std::array<unsigned char, 8> hostNonce{}, devNonce{}, sessionKey{};
        std::array<unsigned char, 8> kek = kKek;
        std::array<unsigned char, 20> contentId = kContentId;
        if (!check("send key data", netmd_secure_send_key_data(m_handle, &ekb))) {
            leave();
            return NetMdError::Rejected;
        }
        netmd_create_nonce(hostNonce.data(), hostNonce.size());
        if (!check("session key exchange", netmd_secure_session_key_exchange(m_handle, hostNonce.data(), devNonce.data()))) {
            leave();
            return NetMdError::Rejected;
        }
        netmd_retailmac(kRootKey.data(), hostNonce.data(), devNonce.data(), sessionKey.data());
        if (!check("setup download", netmd_secure_setup_download(m_handle, contentId.data(), kek.data(), sessionKey.data()))) {
            leave();
            return NetMdError::Rejected;
        }

        netmd_track_packets *packets = nullptr;
        size_t packetCount = 0;
        size_t frames = 0;
        size_t packetLength = 0;
        if (netmd_prepare_packets(data.data(), pcmBytes, &packets, &packetCount, &frames, NETMD_CHANNELS_STEREO,
                                  &packetLength, kek.data(), wireformat)
            != NETMD_NO_ERROR) {
            netmd_cleanup_packets(&packets);
            leave();
            return NetMdError::Failed;
        }
        std::vector<unsigned char>().swap(data); // the encrypted packets are all that's needed now

        uint16_t track = 0;
        std::array<unsigned char, 8> uuid{};
        std::array<unsigned char, 20> newContentId{};
        const netmd_error sent = netmd_secure_send_track_ex(
            m_handle, wireformat, static_cast<unsigned char>(spec.discFormat), static_cast<unsigned int>(frames), packets,
            packetLength, sessionKey.data(), &track, uuid.data(), newContentId.data(), &uploadProgress,
            const_cast<UploadProgress *>(&progress));
        netmd_cleanup_packets(&packets);
        qCDebug(lcNetMd) << "upload: new track" << track << "(" << frames << "frames," << packetLength << "bytes)";
        if (!check("send track", sent)) {
            leave();
            return sent == NETMD_USB_ERROR                  ? NetMdError::Disconnected
                : sent == NETMD_COMMAND_FAILED_REJECTED     ? NetMdError::Rejected
                                                            : NetMdError::Failed;
        }

        // Title (a failure leaves it untitled: still commit the audio), then commit.
        const QByteArray t = title.toLatin1().left(254);
        netmd_cache_toc(m_handle);
        if (netmd_set_title(m_handle, track, t.constData()) != 1)
            qCInfo(lcNetMd) << "couldn't title the new track" << track;
        netmd_sync_toc(m_handle);
        const netmd_error committed = netmd_secure_commit_track(m_handle, track, sessionKey.data());
        check("commit track", committed);
        leave();
        if (trackIndex)
            *trackIndex = track;
        // Only a refusal is "rejected" (write-protected / full); anything else is a plain failure.
        return committed == NETMD_NO_ERROR                ? NetMdError::None
            : committed == NETMD_COMMAND_FAILED_REJECTED  ? NetMdError::Rejected
                                                          : NetMdError::Failed;
    }

    NetMdError deleteTrack(int index) override
    {
        if (!m_handle)
            return NetMdError::NotFound;
        if (index < 0 || index > 0xffff)
            return NetMdError::Failed;
        const int r = netmd_delete_track(m_handle, static_cast<uint16_t>(index));
        qCDebug(lcNetMd) << "delete track" << index << "->" << r;
        return editResult(r);
    }

    NetMdError moveTrack(int from, int to) override
    {
        if (!m_handle)
            return NetMdError::NotFound;
        if (from < 0 || from > 0xffff || to < 0 || to > 0xffff)
            return NetMdError::Failed;
        const int r = netmd_move_track(m_handle, static_cast<uint16_t>(from), static_cast<uint16_t>(to));
        qCDebug(lcNetMd) << "move track" << from << "->" << to << ":" << r;
        return editResult(r);
    }

    NetMdError eraseDisc() override
    {
        if (!m_handle)
            return NetMdError::NotFound;
        netmd_stop(m_handle); // like Web MiniDisc: a playing deck may refuse (refused if stopped: fine)
        const int r = netmd_erase_disc(m_handle);
        qCDebug(lcNetMd) << "erase disc ->" << r;
        return editResult(r);
    }

private:
    // libnetmd patch 17's results: > 0 done, NETMDERR_CMD_INVALID refused, NETMDERR_CMD_FAILED
    // not supported, else the transfer failed (the recorder stopped answering).
    static NetMdError editResult(int r)
    {
        if (r > 0)
            return NetMdError::None;
        switch (r) {
        case NETMDERR_CMD_INVALID:
            return NetMdError::Rejected;
        case NETMDERR_CMD_FAILED:
            return NetMdError::Failed;
        default:
            return NetMdError::Disconnected;
        }
    }

    netmd_device *m_list = nullptr;
    netmd_dev_handle *m_handle = nullptr;
};

} // namespace

std::unique_ptr<NetMdBackend> makeLibNetMdBackend()
{
    return std::make_unique<LibNetMdBackend>();
}

} // namespace mmm

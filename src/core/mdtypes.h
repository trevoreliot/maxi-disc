// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QMetaType>
#include <QString>

// NetMD MiniDisc values (M10): a recorder, its disc, and what can go wrong. Plain values,
// passed between the NetMD worker thread and the UI.
namespace mmm {

// A NetMD recorder on USB, as the monitor sees it (before it's opened).
struct NetMdDevice
{
    QString id;           // "netmd:054c:00ca:<bus>:<address>": stable while plugged in
    quint16 vendorId = 0;
    quint16 productId = 0;
    quint8 bus = 0;
    quint8 address = 0;
    QString name;         // "Sony MZ-NE410" (from the id table) or "NetMD recorder"
    friend bool operator==(const NetMdDevice &, const NetMdDevice &) = default;
};

enum class MdEncoding { Unknown, Sp, Lp2, Lp4 };

struct MdTrack
{
    int index = 0;          // 0-based (the recorder shows index + 1)
    QString title;          // "" if untitled
    qint64 durationMs = 0;
    MdEncoding encoding = MdEncoding::Unknown;
    bool mono = false;
    bool protectedTrack = false; // copy-protected (SCMS): can't be moved off the disc
    friend bool operator==(const MdTrack &, const MdTrack &) = default;
};

// A track group (MDLP discs): a named range of tracks, stored in the disc's title header.
struct MdGroup
{
    QString title;
    int first = -1;         // 0-based, inclusive; -1 for a group without tracks
    int last = -1;
    bool contains(int track) const { return first >= 0 && track >= first && track <= last; }
    friend bool operator==(const MdGroup &, const MdGroup &) = default;
};

struct MdCapacity
{
    qint64 recordedMs = 0;
    qint64 totalMs = 0;
    qint64 availableMs = 0;
    bool valid = false;
    friend bool operator==(const MdCapacity &, const MdCapacity &) = default;
};

struct MdDisc
{
    QString title;          // "" if untitled
    QString rawHeader;      // the disc title as stored ("0;Title//1-3;Group//")
    QList<MdGroup> groups;
    QList<MdTrack> tracks;
    MdCapacity capacity;
    friend bool operator==(const MdDisc &, const MdDisc &) = default;
};

enum class NetMdError {
    None,
    NotFound,         // the recorder isn't connected (any more)
    PermissionDenied, // no access to the USB device: the udev rule (docs/netmd-setup.md)
    Busy,             // another program has it (Web MiniDisc Pro)
    NoDisc,
    Rejected,         // the recorder refused (write-protected disc, title area full, ...)
    Disconnected,     // unplugged during a command
    Failed,           // anything else (transfer error, unexpected reply)
};

QString describe(NetMdError e);

} // namespace mmm

Q_DECLARE_METATYPE(mmm::NetMdDevice)
Q_DECLARE_METATYPE(mmm::MdDisc)
Q_DECLARE_METATYPE(mmm::NetMdError)

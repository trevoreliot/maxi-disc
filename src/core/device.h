// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

#include <functional>

namespace mmm {

// A sync target the app can see. M8: USB mass storage (flash drives, DAPs, SD cards in a
// reader). M10+ adds NetMD MiniDisc recorders to the same list.
struct Device
{
    enum class Kind { UsbStorage, NetMd };

    QString id;          // stable across replugs, keys the profile: "uuid:6478-1AB0", "serial:…:p1", "drive:…"
    QString objectPath;  // where it is right now (UDisks2 block device path); unique even for cloned sticks
    Kind kind = Kind::UsbStorage;
    QString name;        // filesystem label, else "Vendor Model", else the device node
    QString label;       // filesystem label
    QString vendor;
    QString model;
    QString deviceNode;  // "/dev/sdc1"
    QString fsType;      // "vfat", "exfat", ...
    QString mountPoint;  // "" when not mounted
    qint64 totalBytes = -1;
    qint64 freeBytes = -1; // -1 when not mounted or unknown
    bool readOnly = false;
    bool busy = false;     // mounting or unmounting

    bool mounted() const { return !mountPoint.isEmpty(); }
    friend bool operator==(const Device &, const Device &) = default;
};

struct StorageSpace
{
    qint64 totalBytes = -1;
    qint64 freeBytes = -1;
};
// Free/total space of a mounted filesystem (QStorageInfo by default; tests pass their own).
using StorageSpaceFn = std::function<StorageSpace(const QString &mountPoint)>;
StorageSpace storageSpace(const QString &mountPoint);

} // namespace mmm

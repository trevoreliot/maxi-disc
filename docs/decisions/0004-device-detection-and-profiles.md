# 0004 — Devices come from UDisks2; profiles live in the library database, keyed by filesystem UUID

- **Status:** accepted and implemented (2026-11-22, M8): `src/core/udisksmonitor.cpp`
  (`UdisksMonitor::parse`), `src/core/devicemanager.cpp`, `src/core/database.cpp` (schema v4,
  `device_profiles`); tests `tst_udisksmonitor`, `tst_devicemanager`, `tst_deviceprofile`
- **Decided by:** Claude Code during M8 (planned with Gemini 4 Neon); approved by the user as
  part of the M8 plan. The user can revisit
- **Revisit if:** a real device is missing from the list (or an internal disk shows up), a
  profile doesn't come back after reformatting a stick, or profiles should travel between computers

## Context

M8 needs to see USB drives and DAPs as they're plugged in, let the user mount them, and
remember per-device settings (layout, formats, transcode target, artwork) for M9's sync.
The PLAN names UDisks2 over QtDBus. The user's machine has internal SATA/NVMe disks (must not
be listed), an empty USB card reader, and an unmounted USB exFAT drive (so mounting from the
app is needed). NetMD MiniDisc recorders (M10) will join the same device list.

## Decision

1. **Detection: UDisks2's ObjectManager** (`GetManagedObjects` + `InterfacesAdded/Removed` +
   `PropertiesChanged` for mount points), all calls asynchronous. A volume is listed when:
   it has a `Block` and a `Filesystem` interface (so whole disks with partition tables, swap
   and empty readers are skipped); `HintIgnore` and `HintSystem` are false (internal disks,
   EFI/recovery partitions); its size is > 0; its filesystem is one a player or computer reads
   (vfat, exfat, ntfs, ext2/3/4, btrfs, f2fs, hfsplus: not iso9660/udf); its drive isn't
   optical, has media if removable, and is on **USB** (`ConnectionBus == "usb"`, whether or not
   `Removable` is set: DAPs and USB enclosures often say false) or **SDIO** (a card in a built-in
   reader).
2. **Identity: `uuid:<filesystem UUID>`**, else `serial:<drive serial>:p<partition>`, else
   `drive:<UDisks drive id>:p<partition>`. The FAT/exFAT volume serial survives replugging and
   moving between USB ports; it changes when the stick is reformatted (then it's a new device).
   The UDisks object path is kept separately for mount calls, so two cloned sticks (same UUID)
   both show up and share one profile.
3. **Profiles: in the library database** (`device_profiles`, schema v4), not in a file on the
   device. They work while the drive is unmounted or read-only, add no hidden files that cheap
   players index or choke on, and work for MiniDiscs, which have no filesystem. M9's record of
   what was synced to each device should live there too.
4. **Mounting** goes through UDisks2 (`Filesystem.Mount/Unmount`), which asks the user via
   polkit when needed; failures are shown, not retried.

## Costs

- A profile doesn't follow a stick to another computer (or a fresh database).
- Reformatting a stick makes it a new device (its old profile stays in the database, unused).
- Machines without udisksd (minimal setups, some containers) see no devices.

## How to revisit

- **Filter rules:** `UdisksMonitor::parse()`; `tst_udisksmonitor` parseKeepsRemovableUsbOnly
  lists the cases (the user's real exFAT drive is one of them).
- **Profiles on the device:** write a small JSON file (e.g. `.music-md-manager.json`) on save and
  read it in `DeviceManager::rebuild()` when the database has none: export/import, not the main store.
- **Identity:** the fallback chain is in `parse()`; changing it orphans existing profiles
  (migrate the `device_id` column).

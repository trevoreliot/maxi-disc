# libnetmd (vendored)

- **From:** https://github.com/linux-minidisc/linux-minidisc, directory `libnetmd/`
- **Commit:** `2b82dd76270a74c143b6acaeb8e88ddd6035cf7d` (2022-07-11, "Merge pull request #83 from
  glaubitz/netmd-download-support-rebased"); last release tag upstream: 0.9.16 (2019)
- **Copied:** the library's `.c` / `.h` files, `LICENCE.TXT`, and the repository's `COPYING.LIB`.
  Not copied: `documentation/`, `utilities/`, the qmake files, the other tools.
- **Why vendored:** no distribution package, upstream builds with qmake only, the build must work
  offline, and a few fixes below are needed (docs/decisions/0006).

## Licence

Per file, as upstream states it: `libnetmd.c`, `common.c`, `playercontrol.c`, `trackinformation.c`
are LGPL-2.1-or-later; `netmd_dev.c` is GPL-2.0-or-later; `LICENCE.TXT` is the GPL-2 text; the
headers carry no notice. music-md-manager is GPL-3.0-or-later, which both "or later" licences
allow. Local changes are under the licence of the file they're in.

## Local patches (each marked `music-md-manager patch` in the source)

1. `netmd_dev.c` `netmd_init`: take a reference on each stored `libusb_device` and free libusb's
   device list (it leaked); `netmd_clean` drops the references.
2. `netmd_dev.c` `netmd_open`: remember the libusb error (`netmd_last_usb_error()`) so the app can
   tell "permission denied" (udev rule) from "busy" (another app has it); close the handle if
   claiming the interface fails.
3. `netmd_dev.c` `netmd_close`: close the handle even when releasing the interface fails (unplugged).
4. `netmd_dev.c`: `netmd_is_known_device(vendor, product)` exposes the device table.
5. `libnetmd.c`: `request_disc_title` is exported as `netmd_request_disc_title` (the raw disc
   header, which the app parses itself).
6. `playercontrol.c` `netmd_get_disc_capacity`: returns `NETMD_ERROR` unless the reply is
   "accepted" and long enough for the three times (46 bytes; seen from an MZ-NE410 in the M10
   user test), instead of parsing an uninitialised buffer.
7. `playercontrol.c` / `netmd_dev.h`: missing `<string.h>` / `<stdint.h>` includes.
8. `libnetmd.c` `netmd_request_track_time`: also fills a new `hour` field (`struct netmd_track`),
   so tracks over an hour (LP4) aren't shown as minutes modulo 60.
9. `libnetmd.c` `netmd_set_title`: frees its request on the error path (leaked); `netmd_set_disc_title`
   frees its request (leaked).
10. `libnetmd.c` `netmd_request_disc_title`: returns -2 on a transfer error (was 0, the same as an
    empty title: a glitch before a disc rename looked like "no groups"); reads into 512 bytes
    (was 255: headers over 230 bytes couldn't be read back).
11. `playercontrol.c` `netmd_get_disc_capacity`: `NETMD_USB_ERROR` for a failed transfer,
    `NETMD_ERROR` for a rejected request (no disc).
12. `netmd_dev.c`: `netmd_init` checks `libusb_init` (returns `NETMD_USB_ERROR`); `netmd_clean`
    clears the static context after `libusb_exit`.

13. `secure.c` / `secure.h` (M11, uploads): the track transfer goes out in 64 KB bulk writes with a
    progress callback (`netmd_secure_send_track_ex`; was one write per ~8 MB packet, no progress)
    and stops on a USB error (was ignored, then it waited for an answer); `netmd_create_nonce` and
    `netmd_retailmac` (the host side of the key exchange, ported from netmdcli, GPL-2.0-or-later)
    so the app needn't use libgcrypt itself.

14. `secure.c` `netmd_recv_secure_msg`: waits through INTERIM replies for the final one (unless
    INTERIM is expected). Found in the M11 user test: an MZ-NE410 sometimes answers "commit track"
    with INTERIM while still writing the track; libnetmd reported "Unknown Error" (netmd-js waits).

14b. `secure.c` `netmd_recv_secure_msg`: also keeps waiting through receive timeouts (up to 6 more
    ~30 s waits) and logs unexpected statuses: a "commit track" failure in the M11 user test was
    reported as "Unknown Error" with no detail.
14c. `common.c`: USB control-transfer timeouts 1 s → 20 s (an MZ-NE410 busy after "commit track"
    let the 1 s poll time out, then stopped answering until replugged), poll errors logged by name
    and kept for `netmd_last_usb_error()` (now also set by exchanges; `netmd_set_last_usb_error`).
15. `log.c`: logs to stderr (was stdout), so one redirect captures it with the app's log.

`CMakeLists.txt` is ours (upstream has qmake only).
